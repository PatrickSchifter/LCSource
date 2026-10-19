// The Metal device under the engine's Metal backend (see MetalDevice.h).
//
// Frames: the first thing drawn after mtlPresent() takes a command buffer (at most three frames
// are with the GPU at a time) and the next drawable of the window's layer. Data the engine gives
// from memory (vertex arrays, indices, constants) is copied into the frame's part of a ring
// buffer, so the engine may change it right after the draw, as with OpenGL.
//
// Textures and buffers the engine changes after drawing with them are given new storage when the
// GPU may still read the old one (the command buffers keep the old storage alive), so what was
// drawn before the change keeps the contents it was drawn with, as with OpenGL.

#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#include <SDL3/SDL.h>

#include <atomic>
#include <string>
#include <unordered_map>
#include <vector>

#include "MetalDevice.h"

struct MtlTexture {
  id<MTLTexture> tex;
  int iW = 0, iH = 0, ctMips = 0;
  MtlTextureFormat eFormat = MTLTF_NONE;
  uint64_t ulLastUse = 0;   // command buffer that last drew with it
};

struct MtlBuffer {
  id<MTLBuffer> buf;
  size_t slSize = 0;
  uint64_t ulLastUse = 0;
};

struct MtlProgram {
  id<MTLFunction> fn;
  uint32_t ulID = 0;        // for the pipeline cache
};

// a view with the Metal layer, over the window's content (it lets the mouse through to SDL's)
@interface LCMetalView : NSView
@end
@implementation LCMetalView
- (BOOL)wantsUpdateLayer { return YES; }
- (CALayer *)makeBackingLayer { return [CAMetalLayer layer]; }
- (NSView *)hitTest:(NSPoint)pt { return nil; }
@end

static id<MTLDevice> _device;
static id<MTLCommandQueue> _queue;
static LCMetalView *_view;
static CAMetalLayer *_layer;
static int _iScale = 1;

// frames given to the GPU and finished by it
static const int CT_FRAMES = 3;
static dispatch_semaphore_t _semFrames;
static uint64_t _ulSerial = 0;                      // of the current command buffer
static std::atomic<uint64_t> _ulCompleted(0);       // last command buffer the GPU finished
static id<MTLCommandBuffer> _cmd;
static id<MTLRenderCommandEncoder> _enc;
static id<CAMetalDrawable> _drawable;
static bool _bDrawableNew = false;                  // not drawn to yet: clear it when the pass starts
static id<MTLTexture> _texDepth;
static int _iTargetW = 0, _iTargetH = 0;

// ring buffer for data copied from memory, a part per frame
static const size_t SL_RING = 32*1024*1024;
static id<MTLBuffer> _abufRing[CT_FRAMES];
static int _iRing = 0;
static size_t _slRingUsed = 0;

// render state the engine set, and what the encoder has
struct State {
  int aiViewport[4] = {0, 0, 1, 1};
  float fMinZ = 0, fMaxZ = 1;
  int aiScissor[4] = {0, 0, 1, 1};
  bool bBlend = false;
  MtlBlend eSrc = MTLB_ONE, eDst = MTLB_ZERO;
  uint8_t ubColorMask = 15;
  bool bDepthTest = false, bDepthWrite = false;
  MtlCompare eDepthFunc = MTLC_LESS_EQUAL;
  MtlCull eCull = MTLCULL_NONE;
  bool bFrontCCW = true;
  bool bWireframe = false;
  bool bDepthBias = false;
  MtlProgram *pprVertex = NULL, *pprFragment = NULL;
};
static State _st;
static bool _bEncoderFresh = true;   // the encoder has none of the state yet
static id<MTLRenderPipelineState> _psoSet;
static id<MTLDepthStencilState> _dssSet;
static State _stSet;

struct Stream {
  const void *pvData = NULL;   // from memory, copied at draw
  size_t slSize = 0;
  uint64_t ulCopied = 0;       // command buffer it was copied for (then it is at bufCopy, slCopy)
  id<MTLBuffer> bufCopy;
  size_t slCopy = 0;
  MtlBuffer *pbuf = NULL;      // or a GPU buffer
  size_t slOffset = 0;
};
static Stream _astr[MTLS_COUNT];

struct Unit {
  MtlTexture *ptex = NULL;
  MtlSampler smp = {};
};
static Unit _aunit[MTL_MAXTEXUNITS];

static std::vector<uint8_t> _aubVertexConst, _aubFragmentConst;

// caches
static std::unordered_map<uint64_t, id<MTLRenderPipelineState>> _mapPipelines;
static std::unordered_map<uint32_t, id<MTLDepthStencilState>> _mapDepthStates;
static std::unordered_map<uint64_t, id<MTLSamplerState>> _mapSamplers;
static std::unordered_map<std::string, id<MTLLibrary>> _mapLibraries;
static uint32_t _ulLastProgramID = 0;

// what is bound where nothing is
static id<MTLBuffer> _bufDummy;
static id<MTLTexture> _texWhite;
static id<MTLSamplerState> _smpDummy;

// quads drawn as triangles
static id<MTLBuffer> _bufQuadIndices;
static int _ctQuadIndices = 0;

// clearing a rectangle (a triangle over it)
static MtlProgram _prClearVS, _prClearFS;
static const char *_strClearShaders =
  "#include <metal_stdlib>\n"
  "using namespace metal;\n"
  "struct ClearOut { float4 pos [[position]]; };\n"
  "vertex ClearOut clear_vs(uint vid [[vertex_id]], constant float &fDepth [[buffer(0)]]) {\n"
  "  const float2 v = float2((vid<<1)&2, vid&2);\n"
  "  ClearOut o; o.pos = float4(v*2.0-1.0, fDepth, 1.0); return o;\n"
  "}\n"
  "fragment float4 clear_fs(constant float4 &col [[buffer(0)]]) { return col; }\n";


static NSView *ContentView(SDL_Window *pwnd)
{
  NSWindow *nswnd = (__bridge NSWindow *)SDL_GetPointerProperty(SDL_GetWindowProperties(pwnd),
    SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, NULL);
  return nswnd != nil ? nswnd.contentView : nil;
}

static void UpdateDrawableSize(void)
{
  const NSSize sz = _view.bounds.size;
  const CGSize szDrawable = CGSizeMake(fmax(1.0, sz.width*_iScale), fmax(1.0, sz.height*_iScale));
  if (_layer.contentsScale != _iScale) {
    _layer.contentsScale = _iScale;
  }
  if (!CGSizeEqualToSize(_layer.drawableSize, szDrawable)) {
    _layer.drawableSize = szDrawable;
  }
}

bool mtlIsAvailable(void)
{
  @autoreleasepool {
    return MTLCreateSystemDefaultDevice() != nil;
  }
}

bool mtlHasDXT(void)
{
  return _device != nil && _device.supportsBCTextureCompression;
}

bool mtlInit(SDL_Window *pwnd, int iScale)
{
  @autoreleasepool {
    NSView *viewContent = ContentView(pwnd);
    if (viewContent == nil) {
      return false;
    }
    _device = MTLCreateSystemDefaultDevice();
    if (_device == nil) {
      return false;
    }
    _queue = [_device newCommandQueue];
    _view = [[LCMetalView alloc] initWithFrame:viewContent.bounds];
    _view.wantsLayer = YES;
    _view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    [viewContent addSubview:_view];
    _layer = (CAMetalLayer *)_view.layer;
    _layer.device = _device;
    _layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    _layer.framebufferOnly = NO;   // copied from and read back too
    _layer.maximumDrawableCount = 3;
    _layer.opaque = YES;
    _iScale = iScale > 0 ? iScale : 1;
    UpdateDrawableSize();

    _semFrames = dispatch_semaphore_create(CT_FRAMES);
    for (int i = 0; i < CT_FRAMES; i++) {
      _abufRing[i] = [_device newBufferWithLength:SL_RING options:MTLResourceStorageModeShared];
    }
    _bufDummy = [_device newBufferWithLength:256 options:MTLResourceStorageModeShared];
    memset(_bufDummy.contents, 0, 256);
    MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
      width:1 height:1 mipmapped:NO];
    _texWhite = [_device newTextureWithDescriptor:td];
    const uint32_t ulWhite = 0xFFFFFFFF;
    [_texWhite replaceRegion:MTLRegionMake2D(0, 0, 1, 1) mipmapLevel:0 withBytes:&ulWhite bytesPerRow:4];
    _smpDummy = [_device newSamplerStateWithDescriptor:[MTLSamplerDescriptor new]];

    char strError[1024];
    MtlProgram *pprVS = mtlCreateProgram(_strClearShaders, "clear_vs", strError, sizeof(strError));
    MtlProgram *pprFS = mtlCreateProgram(_strClearShaders, "clear_fs", strError, sizeof(strError));
    if (pprVS == NULL || pprFS == NULL) {
      fprintf(stderr, "Metal: %s\n", strError);
      return false;
    }
    _prClearVS = *pprVS;
    _prClearFS = *pprFS;
    delete pprVS;
    delete pprFS;

    _st = State();
    _bEncoderFresh = true;
    fprintf(stderr, "Metal: %s, drawable %dx%d\n", _device.name.UTF8String,
      (int)_layer.drawableSize.width, (int)_layer.drawableSize.height);
    return true;
  }
}

void mtlEnd(void)
{
  @autoreleasepool {
    mtlFinish();
    _mapPipelines.clear();
    _mapDepthStates.clear();
    _mapSamplers.clear();
    _mapLibraries.clear();
    for (int i = 0; i < MTLS_COUNT; i++) {
      _astr[i] = Stream();
    }
    for (int i = 0; i < MTL_MAXTEXUNITS; i++) {
      _aunit[i] = Unit();
    }
    _prClearVS = MtlProgram();
    _prClearFS = MtlProgram();
    _bufQuadIndices = nil;
    _ctQuadIndices = 0;
    for (int i = 0; i < CT_FRAMES; i++) {
      _abufRing[i] = nil;
    }
    _bufDummy = nil;
    _texWhite = nil;
    _smpDummy = nil;
    _texDepth = nil;
    _psoSet = nil;
    _dssSet = nil;
    [_view removeFromSuperview];
    _view = nil;
    _layer = nil;
    _queue = nil;
    _device = nil;
  }
}

const char *mtlDeviceName(void)
{
  return _device != nil ? _device.name.UTF8String : "";
}

void mtlSetDrawableScale(int iScale)
{
  _iScale = iScale > 0 ? iScale : 1;
}

void mtlSetVSync(bool bWait)
{
  _layer.displaySyncEnabled = bWait;
}


// FRAMES

static void BeginCommandBuffer(void)
{
  if (_cmd != nil) {
    return;
  }
  dispatch_semaphore_wait(_semFrames, DISPATCH_TIME_FOREVER);
  _iRing = (_iRing + 1) % CT_FRAMES;
  _slRingUsed = 0;
  _ulSerial++;
  _cmd = [_queue commandBuffer];
  const uint64_t ulSerial = _ulSerial;
  dispatch_semaphore_t sem = _semFrames;
  [_cmd addCompletedHandler:^(id<MTLCommandBuffer> cmd) {
    uint64_t ulDone = _ulCompleted.load();
    while (ulSerial > ulDone && !_ulCompleted.compare_exchange_weak(ulDone, ulSerial)) {}
    dispatch_semaphore_signal(sem);
  }];
}

// the encoder of the render pass, started if needed (nil if there is no drawable)
static bool BeginEncoder(void)
{
  if (_enc != nil) {
    return true;
  }
  BeginCommandBuffer();
  if (_drawable == nil) {
    UpdateDrawableSize();
    _drawable = [_layer nextDrawable];
    if (_drawable == nil) {
      return false;
    }
    _bDrawableNew = true;
  }
  id<MTLTexture> texTarget = _drawable.texture;
  _iTargetW = (int)texTarget.width;
  _iTargetH = (int)texTarget.height;
  if (_texDepth == nil || (int)_texDepth.width != _iTargetW || (int)_texDepth.height != _iTargetH) {
    MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
      width:_iTargetW height:_iTargetH mipmapped:NO];
    td.usage = MTLTextureUsageRenderTarget;
    td.storageMode = MTLStorageModePrivate;
    _texDepth = [_device newTextureWithDescriptor:td];
    _bDrawableNew = true;
  }
  MTLRenderPassDescriptor *rpd = [MTLRenderPassDescriptor renderPassDescriptor];
  rpd.colorAttachments[0].texture = texTarget;
  rpd.colorAttachments[0].loadAction = _bDrawableNew ? MTLLoadActionClear : MTLLoadActionLoad;
  rpd.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 1);
  rpd.colorAttachments[0].storeAction = MTLStoreActionStore;
  rpd.depthAttachment.texture = _texDepth;
  rpd.depthAttachment.loadAction = _bDrawableNew ? MTLLoadActionClear : MTLLoadActionLoad;
  rpd.depthAttachment.clearDepth = 1.0;
  rpd.depthAttachment.storeAction = MTLStoreActionStore;
  _enc = [_cmd renderCommandEncoderWithDescriptor:rpd];
  _bDrawableNew = false;
  _bEncoderFresh = true;
  return true;
}

static void EndEncoder(void)
{
  if (_enc != nil) {
    [_enc endEncoding];
    _enc = nil;
  }
}

void mtlPresent(void)
{
  @autoreleasepool {
    EndEncoder();
    if (_cmd != nil) {
      if (_drawable != nil) {
        [_cmd presentDrawable:_drawable];
      }
      [_cmd commit];
      _cmd = nil;
    }
    _drawable = nil;
  }
}

void mtlFinish(void)
{
  @autoreleasepool {
    EndEncoder();
    if (_cmd != nil) {
      id<MTLCommandBuffer> cmd = _cmd;
      [cmd commit];
      _cmd = nil;
      [cmd waitUntilCompleted];
    }
  }
}


// copy data into the frame's part of the ring buffer (or a buffer of its own if it is full)
static void Upload(const void *pvData, size_t slSize, id<MTLBuffer> __strong &buf, size_t &slOffset)
{
  const size_t slAligned = (slSize + 255) & ~(size_t)255;
  if (_slRingUsed + slAligned > SL_RING) {
    buf = [_device newBufferWithBytes:pvData length:slSize options:MTLResourceStorageModeShared];
    slOffset = 0;
    return;
  }
  buf = _abufRing[_iRing];
  slOffset = _slRingUsed;
  memcpy((uint8_t *)buf.contents + slOffset, pvData, slSize);
  _slRingUsed += slAligned;
}


// RENDER STATE

void mtlSetViewport(int iX, int iY, int iW, int iH, float fMinZ, float fMaxZ)
{
  _st.aiViewport[0] = iX;  _st.aiViewport[1] = iY;
  _st.aiViewport[2] = iW;  _st.aiViewport[3] = iH;
  _st.fMinZ = fMinZ;  _st.fMaxZ = fMaxZ;
}

void mtlSetScissor(int iX, int iY, int iW, int iH)
{
  _st.aiScissor[0] = iX;  _st.aiScissor[1] = iY;
  _st.aiScissor[2] = iW;  _st.aiScissor[3] = iH;
}

void mtlSetBlend(bool bBlend, MtlBlend eSrc, MtlBlend eDst)
{
  _st.bBlend = bBlend;  _st.eSrc = eSrc;  _st.eDst = eDst;
}

void mtlSetColorMask(bool bR, bool bG, bool bB, bool bA)
{
  _st.ubColorMask = (bR ? 1 : 0) | (bG ? 2 : 0) | (bB ? 4 : 0) | (bA ? 8 : 0);
}

void mtlSetDepth(bool bTest, bool bWrite, MtlCompare eFunc)
{
  _st.bDepthTest = bTest;  _st.bDepthWrite = bWrite;  _st.eDepthFunc = eFunc;
}

void mtlSetCull(MtlCull eCull, bool bFrontCCW)
{
  _st.eCull = eCull;  _st.bFrontCCW = bFrontCCW;
}

void mtlSetWireframe(bool bWireframe)
{
  _st.bWireframe = bWireframe;
}

void mtlSetDepthBias(bool bBias)
{
  _st.bDepthBias = bBias;
}

void mtlSetProgram(MtlProgram *pprVertex, MtlProgram *pprFragment)
{
  _st.pprVertex = pprVertex;  _st.pprFragment = pprFragment;
}

void mtlSetTexture(int iUnit, MtlTexture *ptex, const MtlSampler &smp)
{
  _aunit[iUnit].ptex = ptex;
  _aunit[iUnit].smp = smp;
}

void mtlSetTextureNone(int iUnit)
{
  _aunit[iUnit].ptex = NULL;
}

void mtlSetStreamData(MtlStream eStream, const void *pvData, size_t slSize)
{
  Stream &str = _astr[eStream];
  str.pvData = pvData;
  str.slSize = slSize;
  str.ulCopied = 0;
  str.bufCopy = nil;
  str.pbuf = NULL;
}

void mtlSetStreamBuffer(MtlStream eStream, MtlBuffer *pbuf, size_t slOffset)
{
  Stream &str = _astr[eStream];
  str.pvData = NULL;
  str.ulCopied = 0;
  str.bufCopy = nil;
  str.pbuf = pbuf;
  str.slOffset = slOffset;
}

void mtlClearStream(MtlStream eStream)
{
  _astr[eStream] = Stream();
}

void mtlSetVertexConstants(const void *pvData, size_t slSize)
{
  _aubVertexConst.assign((const uint8_t *)pvData, (const uint8_t *)pvData + slSize);
}

void mtlSetFragmentConstants(const void *pvData, size_t slSize)
{
  _aubFragmentConst.assign((const uint8_t *)pvData, (const uint8_t *)pvData + slSize);
}


static MTLBlendFactor BlendFactor(MtlBlend e)
{
  switch (e) {
  case MTLB_ONE:           return MTLBlendFactorOne;
  case MTLB_ZERO:          return MTLBlendFactorZero;
  case MTLB_SRC_COLOR:     return MTLBlendFactorSourceColor;
  case MTLB_INV_SRC_COLOR: return MTLBlendFactorOneMinusSourceColor;
  case MTLB_DST_COLOR:     return MTLBlendFactorDestinationColor;
  case MTLB_INV_DST_COLOR: return MTLBlendFactorOneMinusDestinationColor;
  case MTLB_SRC_ALPHA:     return MTLBlendFactorSourceAlpha;
  case MTLB_INV_SRC_ALPHA: return MTLBlendFactorOneMinusSourceAlpha;
  case MTLB_DST_ALPHA:     return MTLBlendFactorDestinationAlpha;
  case MTLB_INV_DST_ALPHA: return MTLBlendFactorOneMinusDestinationAlpha;
  }
  return MTLBlendFactorOne;
}

static MTLCompareFunction CompareFunction(MtlCompare e)
{
  switch (e) {
  case MTLC_NEVER:         return MTLCompareFunctionNever;
  case MTLC_LESS:          return MTLCompareFunctionLess;
  case MTLC_LESS_EQUAL:    return MTLCompareFunctionLessEqual;
  case MTLC_EQUAL:         return MTLCompareFunctionEqual;
  case MTLC_NOT_EQUAL:     return MTLCompareFunctionNotEqual;
  case MTLC_GREATER_EQUAL: return MTLCompareFunctionGreaterEqual;
  case MTLC_GREATER:       return MTLCompareFunctionGreater;
  case MTLC_ALWAYS:        return MTLCompareFunctionAlways;
  }
  return MTLCompareFunctionAlways;
}

static id<MTLRenderPipelineState> Pipeline(MtlProgram *pprVertex, MtlProgram *pprFragment, bool bBlend,
  MtlBlend eSrc, MtlBlend eDst, uint8_t ubColorMask)
{
  if (!bBlend) {
    eSrc = MTLB_ONE;
    eDst = MTLB_ZERO;
  }
  const uint64_t ulKey = (uint64_t)pprVertex->ulID | ((uint64_t)pprFragment->ulID << 20)
    | ((uint64_t)bBlend << 40) | ((uint64_t)eSrc << 41) | ((uint64_t)eDst << 45) | ((uint64_t)ubColorMask << 49);
  auto it = _mapPipelines.find(ulKey);
  if (it != _mapPipelines.end()) {
    return it->second;
  }
  MTLRenderPipelineDescriptor *pd = [MTLRenderPipelineDescriptor new];
  pd.vertexFunction = pprVertex->fn;
  pd.fragmentFunction = pprFragment->fn;
  MTLRenderPipelineColorAttachmentDescriptor *ca = pd.colorAttachments[0];
  ca.pixelFormat = MTLPixelFormatBGRA8Unorm;
  ca.blendingEnabled = bBlend;
  ca.sourceRGBBlendFactor = ca.sourceAlphaBlendFactor = BlendFactor(eSrc);
  ca.destinationRGBBlendFactor = ca.destinationAlphaBlendFactor = BlendFactor(eDst);
  ca.writeMask = ((ubColorMask & 1) ? MTLColorWriteMaskRed : 0) | ((ubColorMask & 2) ? MTLColorWriteMaskGreen : 0)
    | ((ubColorMask & 4) ? MTLColorWriteMaskBlue : 0) | ((ubColorMask & 8) ? MTLColorWriteMaskAlpha : 0);
  pd.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
  NSError *err = nil;
  id<MTLRenderPipelineState> pso = [_device newRenderPipelineStateWithDescriptor:pd error:&err];
  if (pso == nil) {
    fprintf(stderr, "Metal: cannot make a pipeline: %s\n", err.localizedDescription.UTF8String);
  }
  _mapPipelines[ulKey] = pso;
  return pso;
}

static id<MTLDepthStencilState> DepthState(bool bTest, bool bWrite, MtlCompare eFunc)
{
  const uint32_t ulKey = (bTest ? 1 : 0) | (bWrite ? 2 : 0) | ((uint32_t)eFunc << 2);
  auto it = _mapDepthStates.find(ulKey);
  if (it != _mapDepthStates.end()) {
    return it->second;
  }
  MTLDepthStencilDescriptor *dd = [MTLDepthStencilDescriptor new];
  // as in OpenGL, no depth test means no depth writes either
  dd.depthCompareFunction = bTest ? CompareFunction(eFunc) : MTLCompareFunctionAlways;
  dd.depthWriteEnabled = bTest && bWrite;
  id<MTLDepthStencilState> dss = [_device newDepthStencilStateWithDescriptor:dd];
  _mapDepthStates[ulKey] = dss;
  return dss;
}

static id<MTLSamplerState> Sampler(const MtlSampler &smp)
{
  const uint64_t ulKey = smp.ubMagLinear | (smp.ubMinLinear << 1) | (smp.ubMip << 2) | (smp.ubClampU << 4)
    | (smp.ubClampV << 5) | ((uint64_t)smp.ubAnisotropy << 8);
  auto it = _mapSamplers.find(ulKey);
  if (it != _mapSamplers.end()) {
    return it->second;
  }
  MTLSamplerDescriptor *sd = [MTLSamplerDescriptor new];
  sd.magFilter = smp.ubMagLinear ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
  sd.minFilter = smp.ubMinLinear ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
  sd.mipFilter = smp.ubMip == 0 ? MTLSamplerMipFilterNotMipmapped
               : smp.ubMip == 1 ? MTLSamplerMipFilterNearest : MTLSamplerMipFilterLinear;
  sd.sAddressMode = smp.ubClampU ? MTLSamplerAddressModeClampToEdge : MTLSamplerAddressModeRepeat;
  sd.tAddressMode = smp.ubClampV ? MTLSamplerAddressModeClampToEdge : MTLSamplerAddressModeRepeat;
  sd.maxAnisotropy = smp.ubAnisotropy > 1 ? smp.ubAnisotropy : 1;
  id<MTLSamplerState> ss = [_device newSamplerStateWithDescriptor:sd];
  _mapSamplers[ulKey] = ss;
  return ss;
}

// the scissor rectangle inside the render target (false if nothing of it is left)
static bool ClampedScissor(MTLScissorRect &rc)
{
  int iX0 = _st.aiScissor[0], iY0 = _st.aiScissor[1];
  int iX1 = iX0 + _st.aiScissor[2], iY1 = iY0 + _st.aiScissor[3];
  if (iX0 < 0) iX0 = 0;
  if (iY0 < 0) iY0 = 0;
  if (iX1 > _iTargetW) iX1 = _iTargetW;
  if (iY1 > _iTargetH) iY1 = _iTargetH;
  if (iX1 <= iX0 || iY1 <= iY0) {
    return false;
  }
  rc.x = iX0;  rc.y = iY0;
  rc.width = iX1 - iX0;  rc.height = iY1 - iY0;
  return true;
}

// put the state into the encoder (false if there is nothing to draw on)
static bool PrepareDraw(void)
{
  if (_st.pprVertex == NULL || _st.pprFragment == NULL) {
    return false;
  }
  if (!BeginEncoder()) {
    return false;
  }
  MTLScissorRect rcScissor;
  if (!ClampedScissor(rcScissor) || _st.aiViewport[2] <= 0 || _st.aiViewport[3] <= 0) {
    return false;
  }
  id<MTLRenderPipelineState> pso = Pipeline(_st.pprVertex, _st.pprFragment, _st.bBlend, _st.eSrc, _st.eDst, _st.ubColorMask);
  if (pso == nil) {
    return false;
  }
  const bool bAll = _bEncoderFresh;
  if (bAll || pso != _psoSet) {
    [_enc setRenderPipelineState:pso];
    _psoSet = pso;
  }
  id<MTLDepthStencilState> dss = DepthState(_st.bDepthTest, _st.bDepthWrite, _st.eDepthFunc);
  if (bAll || dss != _dssSet) {
    [_enc setDepthStencilState:dss];
    _dssSet = dss;
  }
  if (bAll || memcmp(_st.aiViewport, _stSet.aiViewport, sizeof(_st.aiViewport)) != 0
   || _st.fMinZ != _stSet.fMinZ || _st.fMaxZ != _stSet.fMaxZ) {
    const MTLViewport vp = {(double)_st.aiViewport[0], (double)_st.aiViewport[1],
      (double)_st.aiViewport[2], (double)_st.aiViewport[3], _st.fMinZ, _st.fMaxZ};
    [_enc setViewport:vp];
  }
  [_enc setScissorRect:rcScissor];
  if (bAll || _st.eCull != _stSet.eCull) {
    [_enc setCullMode:_st.eCull == MTLCULL_FRONT ? MTLCullModeFront
                    : _st.eCull == MTLCULL_BACK ? MTLCullModeBack : MTLCullModeNone];
  }
  if (bAll || _st.bFrontCCW != _stSet.bFrontCCW) {
    [_enc setFrontFacingWinding:_st.bFrontCCW ? MTLWindingCounterClockwise : MTLWindingClockwise];
  }
  if (bAll || _st.bWireframe != _stSet.bWireframe) {
    [_enc setTriangleFillMode:_st.bWireframe ? MTLTriangleFillModeLines : MTLTriangleFillModeFill];
  }
  if (bAll || _st.bDepthBias != _stSet.bDepthBias) {
    if (_st.bDepthBias) {
      [_enc setDepthBias:-2.0f slopeScale:-1.0f clamp:0.0f];   // glPolygonOffset(-1,-2)
    } else {
      [_enc setDepthBias:0.0f slopeScale:0.0f clamp:0.0f];
    }
  }
  _stSet = _st;
  _bEncoderFresh = false;

  // vertex streams (buffer 0 is for the constants)
  for (int iStream = 0; iStream < MTLS_COUNT; iStream++) {
    Stream &str = _astr[iStream];
    if (str.pbuf != NULL) {
      [_enc setVertexBuffer:str.pbuf->buf offset:str.slOffset atIndex:iStream+1];
      str.pbuf->ulLastUse = _ulSerial;
    } else if (str.pvData != NULL && str.slSize > 0) {
      if (str.ulCopied != _ulSerial) {
        id<MTLBuffer> buf;
        Upload(str.pvData, str.slSize, buf, str.slCopy);
        str.bufCopy = buf;
        str.ulCopied = _ulSerial;
      }
      [_enc setVertexBuffer:str.bufCopy offset:str.slCopy atIndex:iStream+1];
    } else {
      [_enc setVertexBuffer:_bufDummy offset:0 atIndex:iStream+1];
    }
  }
  // textures
  for (int iUnit = 0; iUnit < MTL_MAXTEXUNITS; iUnit++) {
    Unit &unit = _aunit[iUnit];
    if (unit.ptex != NULL && unit.ptex->tex != nil) {
      [_enc setFragmentTexture:unit.ptex->tex atIndex:iUnit];
      [_enc setFragmentSamplerState:Sampler(unit.smp) atIndex:iUnit];
      unit.ptex->ulLastUse = _ulSerial;
    } else {
      [_enc setFragmentTexture:_texWhite atIndex:iUnit];
      [_enc setFragmentSamplerState:_smpDummy atIndex:iUnit];
    }
  }
  // constants
  static const uint8_t aubNone[16] = {0};
  if (_aubVertexConst.size() <= 4096) {
    [_enc setVertexBytes:_aubVertexConst.empty() ? aubNone : _aubVertexConst.data()
      length:_aubVertexConst.empty() ? sizeof(aubNone) : _aubVertexConst.size() atIndex:0];
  } else {
    id<MTLBuffer> buf;
    size_t slOffset;
    Upload(_aubVertexConst.data(), _aubVertexConst.size(), buf, slOffset);
    [_enc setVertexBuffer:buf offset:slOffset atIndex:0];
  }
  [_enc setFragmentBytes:_aubFragmentConst.empty() ? aubNone : _aubFragmentConst.data()
    length:_aubFragmentConst.empty() ? sizeof(aubNone) : _aubFragmentConst.size() atIndex:0];
  return true;
}

void mtlDrawIndexed(int ctIndices, const uint16_t *puwIndices)
{
  if (ctIndices <= 0) {
    return;
  }
  @autoreleasepool {
    if (!PrepareDraw()) {
      return;
    }
    id<MTLBuffer> buf;
    size_t slOffset;
    Upload(puwIndices, ctIndices*sizeof(uint16_t), buf, slOffset);
    [_enc drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexCount:ctIndices indexType:MTLIndexTypeUInt16
      indexBuffer:buf indexBufferOffset:slOffset];
  }
}

void mtlDrawQuads(int ctVertices)
{
  const int ctQuads = ctVertices/4;
  if (ctQuads <= 0) {
    return;
  }
  @autoreleasepool {
    if (!PrepareDraw()) {
      return;
    }
    // (0,1,2) and (0,2,3) of each quad, as OpenGL draws them
    const int ctIndices = ctQuads*6;
    if (ctIndices > _ctQuadIndices) {
      int ctNew = _ctQuadIndices > 0 ? _ctQuadIndices : 6*1024;
      while (ctNew < ctIndices) ctNew *= 2;
      _bufQuadIndices = [_device newBufferWithLength:ctNew*sizeof(uint32_t) options:MTLResourceStorageModeShared];
      uint32_t *pul = (uint32_t *)_bufQuadIndices.contents;
      for (int iQuad = 0; iQuad < ctNew/6; iQuad++) {
        const uint32_t ul = iQuad*4;
        pul[iQuad*6+0] = ul+0;  pul[iQuad*6+1] = ul+1;  pul[iQuad*6+2] = ul+2;
        pul[iQuad*6+3] = ul+0;  pul[iQuad*6+4] = ul+2;  pul[iQuad*6+5] = ul+3;
      }
      _ctQuadIndices = ctNew;
    }
    [_enc drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexCount:ctIndices indexType:MTLIndexTypeUInt32
      indexBuffer:_bufQuadIndices indexBufferOffset:0];
  }
}

void mtlDrawPrimitives(MtlPrimitive ePrimitive, int ctVertices)
{
  if (ctVertices <= 0) {
    return;
  }
  @autoreleasepool {
    if (!PrepareDraw()) {
      return;
    }
    const MTLPrimitiveType ePrim = ePrimitive == MTLP_LINES ? MTLPrimitiveTypeLine
                                 : ePrimitive == MTLP_POINTS ? MTLPrimitiveTypePoint : MTLPrimitiveTypeTriangle;
    [_enc drawPrimitives:ePrim vertexStart:0 vertexCount:ctVertices];
  }
}

void mtlClear(int iX, int iY, int iW, int iH, bool bColor, const float afColor[4], bool bDepth, float fDepth)
{
  if (!bColor && !bDepth) {
    return;
  }
  @autoreleasepool {
    if (!BeginEncoder()) {
      return;
    }
    // a triangle over the rectangle, with its own state
    const State stEngine = _st;
    _st.aiViewport[0] = _st.aiScissor[0] = iX;
    _st.aiViewport[1] = _st.aiScissor[1] = iY;
    _st.aiViewport[2] = _st.aiScissor[2] = iW;
    _st.aiViewport[3] = _st.aiScissor[3] = iH;
    _st.fMinZ = 0;  _st.fMaxZ = 1;
    _st.bBlend = false;
    _st.ubColorMask = bColor ? 15 : 0;
    _st.bDepthTest = bDepth;  _st.bDepthWrite = bDepth;  _st.eDepthFunc = MTLC_ALWAYS;
    _st.eCull = MTLCULL_NONE;
    _st.bWireframe = false;
    _st.bDepthBias = false;
    _st.pprVertex = &_prClearVS;  _st.pprFragment = &_prClearFS;
    const std::vector<uint8_t> aubVertexConst = _aubVertexConst, aubFragmentConst = _aubFragmentConst;
    mtlSetVertexConstants(&fDepth, sizeof(fDepth));
    static const float afNone[4] = {0, 0, 0, 0};
    mtlSetFragmentConstants(bColor ? afColor : afNone, 4*sizeof(float));
    if (PrepareDraw()) {
      [_enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    }
    _st = stEngine;
    _aubVertexConst = aubVertexConst;
    _aubFragmentConst = aubFragmentConst;
  }
}


// TEXTURES

MtlTexture *mtlCreateTexture(void)
{
  return new MtlTexture;
}

void mtlDeleteTexture(MtlTexture *ptex)
{
  for (int iUnit = 0; iUnit < MTL_MAXTEXUNITS; iUnit++) {
    if (_aunit[iUnit].ptex == ptex) _aunit[iUnit].ptex = NULL;
  }
  delete ptex;
}

// whether the GPU may still read it (or a frame not given to the GPU yet)
static bool InUse(uint64_t ulLastUse)
{
  return ulLastUse != 0 && ulLastUse > _ulCompleted.load();
}

static MTLPixelFormat PixelFormat(MtlTextureFormat eFormat)
{
  switch (eFormat) {
  case MTLTF_DXT1: return MTLPixelFormatBC1_RGBA;
  case MTLTF_DXT3: return MTLPixelFormatBC2_RGBA;
  case MTLTF_DXT5: return MTLPixelFormatBC3_RGBA;
  default:         return MTLPixelFormatRGBA8Unorm;
  }
}

static void NewStorage(MtlTexture *ptex)
{
  MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:PixelFormat(ptex->eFormat)
    width:ptex->iW height:ptex->iH mipmapped:NO];
  td.mipmapLevelCount = ptex->ctMips;
  td.usage = MTLTextureUsageShaderRead;
  ptex->tex = [_device newTextureWithDescriptor:td];
  ptex->ulLastUse = 0;
}

void mtlTextureStorage(MtlTexture *ptex, int iW, int iH, int ctMips, MtlTextureFormat eFormat)
{
  @autoreleasepool {
    if (mtlTextureHasStorage(ptex, iW, iH, ctMips, eFormat)) {
      return;
    }
    ptex->iW = iW;  ptex->iH = iH;
    ptex->ctMips = ctMips;
    ptex->eFormat = eFormat;
    NewStorage(ptex);
  }
}

bool mtlTextureHasStorage(MtlTexture *ptex, int iW, int iH, int ctMips, MtlTextureFormat eFormat)
{
  return ptex->tex != nil && ptex->iW == iW && ptex->iH == iH && ptex->ctMips == ctMips && ptex->eFormat == eFormat;
}

void mtlTextureUpload(MtlTexture *ptex, int iMip, int iW, int iH, const uint32_t *pulRGBA)
{
  if (ptex->tex == nil || iMip >= ptex->ctMips) {
    return;
  }
  @autoreleasepool {
    // drawn with before: the draws keep the old storage (the other mipmaps come after this one)
    if (InUse(ptex->ulLastUse)) {
      NewStorage(ptex);
    }
    // what OpenGL makes of the formats without color or alpha (32-bit RGBA in memory, R first)
    const uint32_t *pulData = pulRGBA;
    std::vector<uint32_t> aulConverted;
    const MtlTextureFormat eFormat = ptex->eFormat;
    if (eFormat == MTLTF_RGB8 || eFormat == MTLTF_RGB5 || eFormat == MTLTF_L8 || eFormat == MTLTF_A8 || eFormat == MTLTF_LA8) {
      const size_t ctPixels = (size_t)iW*iH;
      aulConverted.resize(ctPixels);
      for (size_t i = 0; i < ctPixels; i++) {
        const uint32_t ul = pulRGBA[i];
        const uint32_t ulR = ul & 0xFF, ulA = ul & 0xFF000000;
        switch (eFormat) {
        case MTLTF_L8:  aulConverted[i] = ulR | (ulR << 8) | (ulR << 16) | 0xFF000000;  break;
        case MTLTF_LA8: aulConverted[i] = ulR | (ulR << 8) | (ulR << 16) | ulA;  break;
        case MTLTF_A8:  aulConverted[i] = 0x00FFFFFF | ulA;  break;
        default:        aulConverted[i] = ul | 0xFF000000;  break;
        }
      }
      pulData = aulConverted.data();
    }
    [ptex->tex replaceRegion:MTLRegionMake2D(0, 0, iW, iH) mipmapLevel:iMip withBytes:pulData bytesPerRow:iW*4];
  }
}

void mtlTextureUploadCompressed(MtlTexture *ptex, int iMip, int iW, int iH, const void *pvData, size_t slSize)
{
  if (ptex->tex == nil || iMip >= ptex->ctMips) {
    return;
  }
  @autoreleasepool {
    if (InUse(ptex->ulLastUse)) {
      NewStorage(ptex);
    }
    const int ctBlocksW = (iW + 3)/4, ctBlocksH = (iH + 3)/4;
    const size_t slBlock = ptex->eFormat == MTLTF_DXT1 ? 8 : 16;
    if (slSize < ctBlocksW*ctBlocksH*slBlock) {
      return;
    }
    [ptex->tex replaceRegion:MTLRegionMake2D(0, 0, iW, iH) mipmapLevel:iMip withBytes:pvData
      bytesPerRow:ctBlocksW*slBlock];
  }
}

int mtlTextureSize(MtlTexture *ptex)
{
  const bool bDXT = ptex->eFormat == MTLTF_DXT1 || ptex->eFormat == MTLTF_DXT3 || ptex->eFormat == MTLTF_DXT5;
  int slSize = 0;
  for (int iMip = 0; iMip < ptex->ctMips; iMip++) {
    int iW = ptex->iW >> iMip, iH = ptex->iH >> iMip;
    if (iW < 1) iW = 1;
    if (iH < 1) iH = 1;
    if (bDXT) {
      slSize += ((iW + 3)/4)*((iH + 3)/4)*(ptex->eFormat == MTLTF_DXT1 ? 8 : 16);
    } else {
      slSize += iW*iH*4;
    }
  }
  return slSize;
}

int mtlTextureWidth(MtlTexture *ptex)  { return ptex->iW; }
int mtlTextureHeight(MtlTexture *ptex) { return ptex->iH; }


// BUFFERS

MtlBuffer *mtlCreateBuffer(size_t slSize)
{
  @autoreleasepool {
    MtlBuffer *pbuf = new MtlBuffer;
    pbuf->slSize = slSize;
    pbuf->buf = [_device newBufferWithLength:slSize > 0 ? slSize : 16 options:MTLResourceStorageModeShared];
    return pbuf;
  }
}

void mtlDeleteBuffer(MtlBuffer *pbuf)
{
  for (int iStream = 0; iStream < MTLS_COUNT; iStream++) {
    if (_astr[iStream].pbuf == pbuf) _astr[iStream] = Stream();
  }
  delete pbuf;
}

void mtlBufferUpdate(MtlBuffer *pbuf, size_t slOffset, const void *pvData, size_t slSize)
{
  @autoreleasepool {
    // drawn with before: the draws keep the old storage, the new one starts as a copy of it
    if (InUse(pbuf->ulLastUse)) {
      id<MTLBuffer> bufOld = pbuf->buf;
      pbuf->buf = [_device newBufferWithBytes:bufOld.contents length:bufOld.length options:MTLResourceStorageModeShared];
      pbuf->ulLastUse = 0;
    }
    memcpy((uint8_t *)pbuf->buf.contents + slOffset, pvData, slSize);
  }
}


// PROGRAMS

MtlProgram *mtlCreateProgram(const char *strSource, const char *strFunction, char *strError, size_t slErrorSize)
{
  @autoreleasepool {
    if (strError != NULL && slErrorSize > 0) {
      strError[0] = 0;
    }
    id<MTLLibrary> lib;
    auto it = _mapLibraries.find(strSource);
    if (it != _mapLibraries.end()) {
      lib = it->second;
    } else {
      NSError *err = nil;
      MTLCompileOptions *co = [MTLCompileOptions new];
      lib = [_device newLibraryWithSource:[NSString stringWithUTF8String:strSource] options:co error:&err];
      if (lib == nil) {
        if (strError != NULL) {
          snprintf(strError, slErrorSize, "%s", err.localizedDescription.UTF8String);
        }
        return NULL;
      }
      _mapLibraries[strSource] = lib;
    }
    id<MTLFunction> fn = [lib newFunctionWithName:[NSString stringWithUTF8String:strFunction]];
    if (fn == nil) {
      if (strError != NULL) {
        snprintf(strError, slErrorSize, "no function '%s'", strFunction);
      }
      return NULL;
    }
    MtlProgram *ppr = new MtlProgram;
    ppr->fn = fn;
    ppr->ulID = ++_ulLastProgramID;
    return ppr;
  }
}

void mtlDeleteProgram(MtlProgram *ppr)
{
  if (_st.pprVertex == ppr) _st.pprVertex = NULL;
  if (_st.pprFragment == ppr) _st.pprFragment = NULL;
  delete ppr;
}
