// The Vulkan device under the engine's Vulkan backend (GAT_VK): the GpuDevice of GpuDevice.h, on
// MoltenVK (a CAMetalLayer over the window's content, as the Metal device has). It draws as the
// Metal device does (MetalDevice.mm); what Vulkan needs on top of that is here.
//
// Command buffers: the first thing drawn (or uploaded) after a submit takes one of three slots,
// each with a command buffer, a command buffer for uploads that runs before it, and a ring buffer
// for the data copied from memory (vertex arrays, indices, constants, texture uploads). A slot is
// used again when the GPU has finished it (a timeline semaphore counts the command buffers). A
// frame is usually one command buffer; reading the frame back submits it early (as with Metal).
//
// Uploads: copies cannot be made while drawing, so textures are uploaded in the slot's upload
// command buffer, which runs before the slot's draws. A texture drawn with before the upload gets
// new storage (as with Metal: the draws keep the old one), so this keeps the order the engine
// gave. Buffers are mapped memory, changed by the CPU (new storage when the GPU may read them).
//
// Drawing: dynamic rendering (no render pass objects); the textures are in SHADER_READ_ONLY
// layout except while uploaded, copied or drawn into. Each draw pushes its descriptors (constants,
// streams as storage buffers, textures) and the streams' first words as push constants; the
// pipelines are cached by their state. The viewport is upside down (negative height), so the
// projections are those of Direct3D and Metal and the windings mean what they mean there.
//
// Barriers are simple: everything waits for everything around a render pass, a copy or a layout
// change. They are not in the draws, and render passes start only at render target changes,
// copies and reads.

#import <AppKit/AppKit.h>
#import <QuartzCore/CAMetalLayer.h>
#define VK_USE_PLATFORM_METAL_EXT
#include <vulkan/vulkan.h>
#include <glslang/Include/glslang_c_interface.h>
#include <glslang/Public/resource_limits_c.h>
#include <SDL3/SDL.h>

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "VulkanDevice.h"
#include "ShaderARBToGLSL.h"

struct VlkTexture : GpuTexture {
  VkImage img = VK_NULL_HANDLE;
  VkDeviceMemory mem = VK_NULL_HANDLE;
  VkImageView view = VK_NULL_HANDLE;
  int iW = 0, iH = 0, ctMips = 0;
  GpuTextureFormat eFormat = GPUTF_NONE;
  uint64_t ulLastUse = 0;   // command buffer that last drew with it
  bool bTarget = false;     // drawn into by the GPU (render target, frame copy): never replaced
  // a render target's depth buffer
  VkImage imgDepth = VK_NULL_HANDLE;
  VkDeviceMemory memDepth = VK_NULL_HANDLE;
  VkImageView viewDepth = VK_NULL_HANDLE;
};

struct VlkBuffer : GpuBuffer {
  VkBuffer buf = VK_NULL_HANDLE;
  VkDeviceMemory mem = VK_NULL_HANDLE;
  uint8_t *pub = NULL;      // mapped
  size_t slSize = 0;
  uint64_t ulLastUse = 0;
};

struct VlkProgram : GpuProgram {
  VkShaderModule mod = VK_NULL_HANDLE;
  bool bVertex = false;
  uint32_t ulID = 0;        // for the pipeline cache
};

// a view with the Metal layer, over the window's content (it lets the mouse through to SDL's)
@interface LCVulkanView : NSView
@end
@implementation LCVulkanView
- (BOOL)wantsUpdateLayer { return YES; }
- (CALayer *)makeBackingLayer { return [CAMetalLayer layer]; }
- (NSView *)hitTest:(NSPoint)pt { return nil; }
@end

static LCVulkanView *_view;
static CAMetalLayer *_layer;
static int _iScale = 1;

static VkInstance _inst = VK_NULL_HANDLE;
static VkDebugUtilsMessengerEXT _msgr = VK_NULL_HANDLE;
static VkSurfaceKHR _surface = VK_NULL_HANDLE;
static VkPhysicalDevice _phys = VK_NULL_HANDLE;
static VkPhysicalDeviceProperties _props;
static VkPhysicalDeviceMemoryProperties _memprops;
static bool _bBC = false;
static VkDevice _dev = VK_NULL_HANDLE;
static uint32_t _iQueueFamily = 0;
static VkQueue _queue = VK_NULL_HANDLE;
static PFN_vkCmdPushDescriptorSetKHR _pvkCmdPushDescriptorSet = NULL;

// the window's swapchain and depth buffer
static VkSwapchainKHR _swapchain = VK_NULL_HANDLE;
static VkFormat _fmtColor = VK_FORMAT_B8G8R8A8_UNORM;   // of the swapchain, the render targets and the frame copies
static const VkFormat FMT_DEPTH = VK_FORMAT_D32_SFLOAT;
static VkExtent2D _extSwap = {0, 0};
static std::vector<VkImage> _aimgSwap;
static std::vector<VkImageView> _aviewSwap;
static std::vector<VkSemaphore> _asemRendered;           // per swapchain image, for the present
static bool _bVSync = true, _bSwapVSync = true;
static bool _bSwapDirty = false;
static int _iImage = -1;                                  // acquired swapchain image (-1: none)
static VkImageLayout _eImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
static bool _bDrawableNew = false;                        // not drawn to yet: clear it when the pass starts
static VkImage _imgDepth = VK_NULL_HANDLE;
static VkDeviceMemory _memDepth = VK_NULL_HANDLE;
static VkImageView _viewDepth = VK_NULL_HANDLE;
static VkImageLayout _eDepthLayout = VK_IMAGE_LAYOUT_UNDEFINED;
static bool _bDepthDrawn = false;                         // has a frame's depths
// semaphores for acquiring the images, and the command buffer that waited on each
static const int CT_ACQUIRE = 4;
static VkSemaphore _asemAcquire[CT_ACQUIRE];
static uint64_t _aulAcquireUser[CT_ACQUIRE];
static int _iAcquire = 0;
static VkSemaphore _semAcquirePending = VK_NULL_HANDLE;   // to wait on in the next submit

// command buffers, a slot per command buffer given to the GPU (at most three at a time)
static const int CT_SLOTS = 3;
static const size_t SL_RING = 32*1024*1024;
struct Slot {
  VkCommandPool pool = VK_NULL_HANDLE;
  VkCommandBuffer cmd = VK_NULL_HANDLE, cmdUpload = VK_NULL_HANDLE;
  VkBuffer bufRing = VK_NULL_HANDLE;
  VkDeviceMemory memRing = VK_NULL_HANDLE;
  uint8_t *pubRing = NULL;
  uint64_t ulSerial = 0;
};
static Slot _aslot[CT_SLOTS];
static int _iSlot = 0;
static size_t _slRingUsed = 0;
static size_t _slStaged = 0;               // given buffers of their own in this command buffer
static VkSemaphore _semTimeline = VK_NULL_HANDLE;
static uint64_t _ulSerial = 0;             // of the current command buffer
static VkCommandBuffer _cmd = VK_NULL_HANDLE;   // being recorded (of _aslot[_iSlot])
static bool _bUploadOpen = false;
static std::unordered_set<VkImage> _setUploading;   // in TRANSFER_DST in the upload command buffer

// what is let go of when the GPU is done with it
struct Garbage {
  uint64_t ulSerial;
  VkImage img;
  VkImageView view;
  VkDeviceMemory mem;
  VkBuffer buf;
};
static std::vector<Garbage> _agarb;

// the pass being drawn
static bool _bInPass = false;
static VlkTexture *_ptexPass = NULL;       // its render target (NULL: the window)
static VlkTexture *_ptexTarget = NULL;     // render target of the next draws (NULL: the window)
static int _iTargetW = 0, _iTargetH = 0;

// render state the engine set, and what the command buffer has
enum Topology { TOP_TRIANGLES = 0, TOP_LINES, TOP_POINTS };
struct State {
  int aiViewport[4] = {0, 0, 1, 1};
  float fMinZ = 0, fMaxZ = 1;
  int aiScissor[4] = {0, 0, 1, 1};
  bool bBlend = false;
  GpuBlend eSrc = GPUB_ONE, eDst = GPUB_ZERO;
  uint8_t ubColorMask = 15;
  bool bDepthTest = false, bDepthWrite = false;
  GpuCompare eDepthFunc = GPUC_LESS_EQUAL;
  GpuCull eCull = GPUCULL_NONE;
  bool bFrontCCW = true;
  bool bWireframe = false;
  bool bDepthBias = false;
  VlkProgram *pprVertex = NULL, *pprFragment = NULL;
};
static State _st;
static bool _bPassFresh = true;      // the command buffer has none of the state of the pass yet
static VkPipeline _pipeSet = VK_NULL_HANDLE;
static State _stSet;
static VkRect2D _rcScissorSet;

struct Stream {
  const void *pvData = NULL;   // from memory, copied at draw
  size_t slSize = 0;
  uint64_t ulCopied = 0;       // command buffer it was copied for (then it is at bufCopy, slCopy)
  VkBuffer bufCopy = VK_NULL_HANDLE;
  size_t slCopy = 0;
  VlkBuffer *pbuf = NULL;      // or a GPU buffer
  size_t slOffset = 0;
};
static Stream _astr[GPUS_COUNT];

struct Unit {
  VlkTexture *ptex = NULL;
  GpuSampler smp = {};
};
static Unit _aunit[GPU_MAXTEXUNITS];

static std::vector<uint8_t> _aubVertexConst, _aubFragmentConst;

// what PushState() kept
struct SavedState {
  State st;
  Stream astr[GPUS_COUNT];
  Unit aunit[GPU_MAXTEXUNITS];
  std::vector<uint8_t> aubVertexConst, aubFragmentConst;
};
static std::vector<SavedState> _asavStates;

// the layout of the draws, caches
static VkDescriptorSetLayout _dsl = VK_NULL_HANDLE;
static VkPipelineLayout _pl = VK_NULL_HANDLE;
static VkPipelineCache _pc = VK_NULL_HANDLE;
struct PipelineKey {
  uint64_t a, b;
  bool operator==(const PipelineKey &k) const { return a == k.a && b == k.b; }
};
struct PipelineKeyHash {
  size_t operator()(const PipelineKey &k) const { return std::hash<uint64_t>()(k.a*31 + k.b); }
};
static std::unordered_map<PipelineKey, VkPipeline, PipelineKeyHash> _mapPipelines;
static std::unordered_map<uint64_t, VkSampler> _mapSamplers;
static uint32_t _ulLastProgramID = 0;
// SPIR-V of the sources compiled (kept from device to device)
static std::unordered_map<std::string, std::vector<uint32_t>> _mapSPIRV;
static bool _bGlslang = false;

// what is bound where nothing is
static VlkBuffer *_pbufDummy = NULL;
static VlkTexture *_ptexWhite = NULL;
static VkSampler _smpDummy = VK_NULL_HANDLE;

// quads drawn as triangles
static VlkBuffer *_pbufQuadIndices = NULL;
static int _ctQuadIndices = 0;

// the engine's textures and buffers: their Vulkan objects go with the device (the engine keeps
// the objects and gives them storage again on the next device)
static std::unordered_set<VlkTexture *> _setTextures;
static std::unordered_set<VlkBuffer *> _setBuffers;

static void vlkFinish(void);
static void NewStorage(VlkTexture *ptex);
static void UploadMip(VlkTexture *ptex, int iMip, int iW, int iH, const void *pvData, size_t slSize);
static void vlkSetVertexConstants(const void *pvData, size_t slSize);
static void vlkSetFragmentConstants(const void *pvData, size_t slSize);
static void vlkSetRenderTarget(GpuTexture *ptexGpu);
static bool vlkTextureHasStorage(VlkTexture *ptex, int iW, int iH, int ctMips, GpuTextureFormat eFormat);


static bool Check(VkResult r, const char *strWhat)
{
  if (r == VK_SUCCESS) {
    return true;
  }
  fprintf(stderr, "Vulkan: %s failed (%d)\n", strWhat, (int)r);
  return false;
}

static NSView *ContentView(SDL_Window *pwnd)
{
  NSWindow *nswnd = (__bridge NSWindow *)SDL_GetPointerProperty(SDL_GetWindowProperties(pwnd),
    SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, NULL);
  return nswnd != nil ? nswnd.contentView : nil;
}

// the size the window's frame should have now
static VkExtent2D DrawableSize(void)
{
  const NSSize sz = _view.bounds.size;
  VkExtent2D ext;
  ext.width  = (uint32_t)fmax(1.0, sz.width*_iScale);
  ext.height = (uint32_t)fmax(1.0, sz.height*_iScale);
  return ext;
}


// MEMORY, IMAGES AND BUFFERS

static uint32_t MemoryType(uint32_t ulTypeBits, VkMemoryPropertyFlags ulProps)
{
  for (uint32_t i = 0; i < _memprops.memoryTypeCount; i++) {
    if ((ulTypeBits & (1u << i)) && (_memprops.memoryTypes[i].propertyFlags & ulProps) == ulProps) {
      return i;
    }
  }
  return UINT32_MAX;
}

static bool MakeBuffer(size_t slSize, VkBufferUsageFlags ulUsage, VkBuffer &buf, VkDeviceMemory &mem, uint8_t **ppub)
{
  VkBufferCreateInfo bci = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  bci.size = slSize > 0 ? slSize : 16;
  bci.usage = ulUsage;
  bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  if (!Check(vkCreateBuffer(_dev, &bci, NULL, &buf), "vkCreateBuffer")) {
    return false;
  }
  VkMemoryRequirements mr;
  vkGetBufferMemoryRequirements(_dev, buf, &mr);
  VkMemoryAllocateInfo mai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  mai.allocationSize = mr.size;
  mai.memoryTypeIndex = MemoryType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  if (!Check(vkAllocateMemory(_dev, &mai, NULL, &mem), "vkAllocateMemory (buffer)")) {
    vkDestroyBuffer(_dev, buf, NULL);
    buf = VK_NULL_HANDLE;
    return false;
  }
  vkBindBufferMemory(_dev, buf, mem, 0);
  vkMapMemory(_dev, mem, 0, VK_WHOLE_SIZE, 0, (void **)ppub);
  return true;
}

static bool MakeImage(int iW, int iH, int ctMips, VkFormat fmt, VkImageUsageFlags ulUsage, VkImageAspectFlags ulAspect,
  VkImage &img, VkDeviceMemory &mem, VkImageView &view)
{
  VkImageCreateInfo ici = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  ici.imageType = VK_IMAGE_TYPE_2D;
  ici.format = fmt;
  ici.extent = {(uint32_t)iW, (uint32_t)iH, 1};
  ici.mipLevels = ctMips;
  ici.arrayLayers = 1;
  ici.samples = VK_SAMPLE_COUNT_1_BIT;
  ici.tiling = VK_IMAGE_TILING_OPTIMAL;
  ici.usage = ulUsage;
  ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  if (!Check(vkCreateImage(_dev, &ici, NULL, &img), "vkCreateImage")) {
    return false;
  }
  VkMemoryRequirements mr;
  vkGetImageMemoryRequirements(_dev, img, &mr);
  VkMemoryAllocateInfo mai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  mai.allocationSize = mr.size;
  mai.memoryTypeIndex = MemoryType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  if (!Check(vkAllocateMemory(_dev, &mai, NULL, &mem), "vkAllocateMemory (image)")) {
    vkDestroyImage(_dev, img, NULL);
    img = VK_NULL_HANDLE;
    return false;
  }
  vkBindImageMemory(_dev, img, mem, 0);
  VkImageViewCreateInfo vci = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  vci.image = img;
  vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
  vci.format = fmt;
  vci.subresourceRange = {ulAspect, 0, (uint32_t)ctMips, 0, 1};
  return Check(vkCreateImageView(_dev, &vci, NULL, &view), "vkCreateImageView");
}

// let go of them when the GPU is done with what is recorded now
static void Discard(VkImage img, VkImageView view, VkDeviceMemory mem, VkBuffer buf)
{
  if (_dev == VK_NULL_HANDLE || (img == VK_NULL_HANDLE && view == VK_NULL_HANDLE && mem == VK_NULL_HANDLE && buf == VK_NULL_HANDLE)) {
    return;
  }
  _agarb.push_back({_ulSerial, img, view, mem, buf});
}

static void DiscardTexture(VlkTexture *ptex)
{
  Discard(ptex->img, ptex->view, ptex->mem, VK_NULL_HANDLE);
  Discard(ptex->imgDepth, ptex->viewDepth, ptex->memDepth, VK_NULL_HANDLE);
  ptex->img = ptex->imgDepth = VK_NULL_HANDLE;
  ptex->view = ptex->viewDepth = VK_NULL_HANDLE;
  ptex->mem = ptex->memDepth = VK_NULL_HANDLE;
}

static uint64_t Completed(void)
{
  if (_semTimeline == VK_NULL_HANDLE) {
    return UINT64_MAX;
  }
  uint64_t ul = 0;
  vkGetSemaphoreCounterValue(_dev, _semTimeline, &ul);
  return ul;
}

static void WaitFor(uint64_t ulSerial)
{
  if (ulSerial == 0 || Completed() >= ulSerial) {
    return;
  }
  VkSemaphoreWaitInfo wi = {VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO};
  wi.semaphoreCount = 1;
  wi.pSemaphores = &_semTimeline;
  wi.pValues = &ulSerial;
  vkWaitSemaphores(_dev, &wi, UINT64_MAX);
}

static void CollectGarbage(bool bAll)
{
  const uint64_t ulDone = Completed();
  size_t iKept = 0;
  for (size_t i = 0; i < _agarb.size(); i++) {
    const Garbage &g = _agarb[i];
    if (!bAll && g.ulSerial > ulDone) {
      _agarb[iKept++] = g;
      continue;
    }
    if (g.view != VK_NULL_HANDLE) vkDestroyImageView(_dev, g.view, NULL);
    if (g.img != VK_NULL_HANDLE) vkDestroyImage(_dev, g.img, NULL);
    if (g.buf != VK_NULL_HANDLE) vkDestroyBuffer(_dev, g.buf, NULL);
    if (g.mem != VK_NULL_HANDLE) vkFreeMemory(_dev, g.mem, NULL);
  }
  _agarb.resize(iKept);
}

// whether the GPU may still read it (or a command buffer not given to the GPU yet)
static bool InUse(uint64_t ulLastUse)
{
  return ulLastUse != 0 && ulLastUse > Completed();
}

// everything before waits for everything after, and the image goes to the layout
static void Barrier(VkCommandBuffer cmd, VkImage img, VkImageAspectFlags ulAspect, VkImageLayout eOld, VkImageLayout eNew)
{
  VkImageMemoryBarrier imb = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  imb.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
  imb.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
  imb.oldLayout = eOld;
  imb.newLayout = eNew;
  imb.srcQueueFamilyIndex = imb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  imb.image = img;
  imb.subresourceRange = {ulAspect, 0, VK_REMAINING_MIP_LEVELS, 0, 1};
  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0,
    0, NULL, 0, NULL, 1, &imb);
}

static void BarrierAll(VkCommandBuffer cmd)
{
  VkMemoryBarrier mb = {VK_STRUCTURE_TYPE_MEMORY_BARRIER};
  mb.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
  mb.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0,
    1, &mb, 0, NULL, 0, NULL);
}


// COMMAND BUFFERS

static void BeginCommandBuffer(void)
{
  if (_cmd != VK_NULL_HANDLE) {
    return;
  }
  _iSlot = (_iSlot + 1) % CT_SLOTS;
  Slot &slot = _aslot[_iSlot];
  WaitFor(slot.ulSerial);
  CollectGarbage(false);
  vkResetCommandPool(_dev, slot.pool, 0);
  _slRingUsed = 0;
  _slStaged = 0;
  _ulSerial++;
  slot.ulSerial = _ulSerial;
  VkCommandBufferBeginInfo bi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(slot.cmd, &bi);
  _cmd = slot.cmd;
  _bUploadOpen = false;
}

// the upload command buffer of the current slot (it runs before the draws)
static VkCommandBuffer UploadCommands(void)
{
  BeginCommandBuffer();
  Slot &slot = _aslot[_iSlot];
  if (!_bUploadOpen) {
    VkCommandBufferBeginInfo bi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(slot.cmdUpload, &bi);
    _bUploadOpen = true;
  }
  return slot.cmdUpload;
}

// copy data into the slot's part of the ring buffer (or a buffer of its own if it is full)
static void Upload(const void *pvData, size_t slSize, VkBuffer &buf, size_t &slOffset)
{
  BeginCommandBuffer();
  const size_t slAligned = (slSize + 255) & ~(size_t)255;
  if (_slRingUsed + slAligned > SL_RING) {
    VkDeviceMemory mem;
    uint8_t *pub;
    if (!MakeBuffer(slSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT
      | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, buf, mem, &pub)) {
      buf = _pbufDummy->buf;
      slOffset = 0;
      return;
    }
    memcpy(pub, pvData, slSize);
    Discard(VK_NULL_HANDLE, VK_NULL_HANDLE, mem, buf);
    _slStaged += slSize;
    slOffset = 0;
    return;
  }
  Slot &slot = _aslot[_iSlot];
  buf = slot.bufRing;
  slOffset = _slRingUsed;
  memcpy(slot.pubRing + slOffset, pvData, slSize);
  _slRingUsed += slAligned;
}

static void EndPass(void);

// give the command buffers to the GPU (signaling the semaphore too, if any)
static void Submit(VkSemaphore semSignal)
{
  EndPass();
  if (_cmd == VK_NULL_HANDLE) {
    return;
  }
  Slot &slot = _aslot[_iSlot];
  VkCommandBuffer acmd[2];
  uint32_t ctCmd = 0;
  if (_bUploadOpen) {
    for (VkImage img : _setUploading) {
      Barrier(slot.cmdUpload, img, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
    _setUploading.clear();
    vkEndCommandBuffer(slot.cmdUpload);
    acmd[ctCmd++] = slot.cmdUpload;
    _bUploadOpen = false;
  }
  vkEndCommandBuffer(_cmd);
  acmd[ctCmd++] = _cmd;

  const uint64_t aulSignal[2] = {_ulSerial, 0};
  VkSemaphore asemSignal[2] = {_semTimeline, semSignal};
  const uint64_t ulWait = 0;
  VkTimelineSemaphoreSubmitInfo tsi = {VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO};
  tsi.signalSemaphoreValueCount = semSignal != VK_NULL_HANDLE ? 2 : 1;
  tsi.pSignalSemaphoreValues = aulSignal;
  VkSubmitInfo si = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
  si.pNext = &tsi;
  const VkPipelineStageFlags ulWaitStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
  const VkSemaphore semWait = _semAcquirePending;
  if (semWait != VK_NULL_HANDLE) {
    tsi.waitSemaphoreValueCount = 1;
    tsi.pWaitSemaphoreValues = &ulWait;
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &semWait;
    si.pWaitDstStageMask = &ulWaitStage;
    for (int i = 0; i < CT_ACQUIRE; i++) {
      if (_asemAcquire[i] == _semAcquirePending) _aulAcquireUser[i] = _ulSerial;
    }
    _semAcquirePending = VK_NULL_HANDLE;
  }
  si.commandBufferCount = ctCmd;
  si.pCommandBuffers = acmd;
  si.signalSemaphoreCount = tsi.signalSemaphoreValueCount;
  si.pSignalSemaphores = asemSignal;
  Check(vkQueueSubmit(_queue, 1, &si, VK_NULL_HANDLE), "vkQueueSubmit");
  _cmd = VK_NULL_HANDLE;
}

// submit what is recorded (and wait for the GPU to run it)
static void Flush(bool bWait)
{
  Submit(VK_NULL_HANDLE);
  if (bWait) {
    WaitFor(_ulSerial);
  }
}


// THE SWAPCHAIN

static void DestroySwapchainImages(void)
{
  for (VkImageView view : _aviewSwap) {
    Discard(VK_NULL_HANDLE, view, VK_NULL_HANDLE, VK_NULL_HANDLE);
  }
  _aviewSwap.clear();
  _aimgSwap.clear();
  for (VkSemaphore sem : _asemRendered) {
    vkDestroySemaphore(_dev, sem, NULL);
  }
  _asemRendered.clear();
}

static bool MakeSwapchain(void)
{
  vkDeviceWaitIdle(_dev);
  VkSurfaceCapabilitiesKHR caps;
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(_phys, _surface, &caps);
  VkExtent2D ext = DrawableSize();
  if (ext.width < caps.minImageExtent.width) ext.width = caps.minImageExtent.width;
  if (ext.height < caps.minImageExtent.height) ext.height = caps.minImageExtent.height;
  if (ext.width > caps.maxImageExtent.width) ext.width = caps.maxImageExtent.width;
  if (ext.height > caps.maxImageExtent.height) ext.height = caps.maxImageExtent.height;
  if (ext.width == 0 || ext.height == 0) {
    return false;
  }
  _layer.contentsScale = _iScale;

  uint32_t ctModes = 0;
  vkGetPhysicalDeviceSurfacePresentModesKHR(_phys, _surface, &ctModes, NULL);
  std::vector<VkPresentModeKHR> aeModes(ctModes);
  vkGetPhysicalDeviceSurfacePresentModesKHR(_phys, _surface, &ctModes, aeModes.data());
  VkPresentModeKHR eMode = VK_PRESENT_MODE_FIFO_KHR;
  if (!_bVSync) {
    for (VkPresentModeKHR e : aeModes) {
      if (e == VK_PRESENT_MODE_IMMEDIATE_KHR) eMode = e;
    }
  }

  VkSwapchainCreateInfoKHR sci = {VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
  sci.surface = _surface;
  sci.minImageCount = caps.minImageCount > 3 ? caps.minImageCount : 3;
  if (caps.maxImageCount > 0 && sci.minImageCount > caps.maxImageCount) sci.minImageCount = caps.maxImageCount;
  sci.imageFormat = _fmtColor;
  sci.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
  sci.imageExtent = ext;
  sci.imageArrayLayers = 1;
  sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  sci.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
  sci.compositeAlpha = (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)
    ? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR : VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
  sci.presentMode = eMode;
  sci.clipped = VK_TRUE;
  sci.oldSwapchain = _swapchain;
  VkSwapchainKHR swapchain;
  if (!Check(vkCreateSwapchainKHR(_dev, &sci, NULL, &swapchain), "vkCreateSwapchainKHR")) {
    return false;
  }
  DestroySwapchainImages();
  if (_swapchain != VK_NULL_HANDLE) {
    vkDestroySwapchainKHR(_dev, _swapchain, NULL);
  }
  _swapchain = swapchain;
  _extSwap = ext;
  _bSwapVSync = _bVSync;
  _bSwapDirty = false;

  uint32_t ctImages = 0;
  vkGetSwapchainImagesKHR(_dev, _swapchain, &ctImages, NULL);
  _aimgSwap.resize(ctImages);
  vkGetSwapchainImagesKHR(_dev, _swapchain, &ctImages, _aimgSwap.data());
  for (VkImage img : _aimgSwap) {
    VkImageViewCreateInfo vci = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vci.image = img;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = _fmtColor;
    vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkImageView view = VK_NULL_HANDLE;
    vkCreateImageView(_dev, &vci, NULL, &view);
    _aviewSwap.push_back(view);
    VkSemaphoreCreateInfo semci = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkSemaphore sem = VK_NULL_HANDLE;
    vkCreateSemaphore(_dev, &semci, NULL, &sem);
    _asemRendered.push_back(sem);
  }

  // the depth buffer of the window's frames
  Discard(_imgDepth, _viewDepth, _memDepth, VK_NULL_HANDLE);
  _imgDepth = VK_NULL_HANDLE;
  if (!MakeImage(ext.width, ext.height, 1, FMT_DEPTH, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
    VK_IMAGE_ASPECT_DEPTH_BIT, _imgDepth, _memDepth, _viewDepth)) {
    return false;
  }
  _eDepthLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  _bDepthDrawn = false;
  return true;
}

// take the next image of the window (false if there is none)
static bool Acquire(void)
{
  if (_iImage >= 0) {
    return true;
  }
  const VkExtent2D ext = DrawableSize();
  if (_swapchain == VK_NULL_HANDLE || _bSwapDirty || _bSwapVSync != _bVSync
   || ext.width != _extSwap.width || ext.height != _extSwap.height) {
    // (nothing recorded draws into the window now: no image is taken)
    if (!MakeSwapchain()) {
      return false;
    }
  }
  for (int iTry = 0; iTry < 2; iTry++) {
    _iAcquire = (_iAcquire + 1) % CT_ACQUIRE;
    WaitFor(_aulAcquireUser[_iAcquire]);
    VkSemaphore sem = _asemAcquire[_iAcquire];
    uint32_t iImage = 0;
    const VkResult r = vkAcquireNextImageKHR(_dev, _swapchain, UINT64_MAX, sem, VK_NULL_HANDLE, &iImage);
    if (r == VK_SUCCESS || r == VK_SUBOPTIMAL_KHR) {
      if (r == VK_SUBOPTIMAL_KHR) _bSwapDirty = true;
      _iImage = (int)iImage;
      _semAcquirePending = sem;
      _eImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      _bDrawableNew = true;
      return true;
    }
    if (r != VK_ERROR_OUT_OF_DATE_KHR || !MakeSwapchain()) {
      Check(r, "vkAcquireNextImageKHR");
      return false;
    }
  }
  return false;
}


// RENDER PASSES

// the pass of the current target, started if needed (false if there is nothing to draw on)
static bool BeginPass(void)
{
  if (_bInPass) {
    return true;
  }
  if (_ptexTarget == NULL && !Acquire()) {
    return false;
  }
  BeginCommandBuffer();
  BarrierAll(_cmd);
  VkRenderingAttachmentInfo rai = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
  VkRenderingAttachmentInfo raiDepth = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
  rai.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  rai.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  raiDepth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
  raiDepth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  // a render target keeps what was drawn into it (callers clear it)
  if (_ptexTarget != NULL) {
    VlkTexture *ptex = _ptexTarget;
    Barrier(_cmd, ptex->img, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    _iTargetW = ptex->iW;
    _iTargetH = ptex->iH;
    rai.imageView = ptex->view;
    rai.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    raiDepth.imageView = ptex->viewDepth;
    raiDepth.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    ptex->ulLastUse = _ulSerial;
  } else {
    if (_eImageLayout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
      Barrier(_cmd, _aimgSwap[_iImage], VK_IMAGE_ASPECT_COLOR_BIT, _bDrawableNew ? VK_IMAGE_LAYOUT_UNDEFINED : _eImageLayout,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
      _eImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }
    if (_eDepthLayout != VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL) {
      Barrier(_cmd, _imgDepth, VK_IMAGE_ASPECT_DEPTH_BIT, _eDepthLayout, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
      _eDepthLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    }
    _iTargetW = (int)_extSwap.width;
    _iTargetH = (int)_extSwap.height;
    rai.imageView = _aviewSwap[_iImage];
    rai.loadOp = _bDrawableNew ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
    rai.clearValue.color = {{0, 0, 0, 1}};
    raiDepth.imageView = _viewDepth;
    raiDepth.loadOp = _bDrawableNew ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
    raiDepth.clearValue.depthStencil = {1.0f, 0};
    _bDrawableNew = false;
    _bDepthDrawn = true;
  }
  VkRenderingInfo ri = {VK_STRUCTURE_TYPE_RENDERING_INFO};
  ri.renderArea = {{0, 0}, {(uint32_t)_iTargetW, (uint32_t)_iTargetH}};
  ri.layerCount = 1;
  ri.colorAttachmentCount = 1;
  ri.pColorAttachments = &rai;
  ri.pDepthAttachment = &raiDepth;
  vkCmdBeginRendering(_cmd, &ri);
  _bInPass = true;
  _ptexPass = _ptexTarget;
  _bPassFresh = true;
  return true;
}

static void EndPass(void)
{
  if (!_bInPass) {
    return;
  }
  vkCmdEndRendering(_cmd);
  if (_ptexPass != NULL) {
    Barrier(_cmd, _ptexPass->img, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  }
  _bInPass = false;
  _ptexPass = NULL;
}


// DEVICE

static bool vlkInit(SDL_Window *pwnd, int iScale);
static void vlkEnd(void);

bool vlkIsAvailable(void)
{
  // the Vulkan loader and glslang are weakly linked: they may not be on this system
  if (dlsym(RTLD_DEFAULT, "vkCreateInstance") == NULL || dlsym(RTLD_DEFAULT, "glslang_initialize_process") == NULL) {
    return false;
  }
  VkApplicationInfo ai = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
  ai.apiVersion = VK_API_VERSION_1_3;
  const char *astrExt[] = {VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME};
  VkInstanceCreateInfo ici = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  ici.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
  ici.pApplicationInfo = &ai;
  ici.enabledExtensionCount = 1;
  ici.ppEnabledExtensionNames = astrExt;
  VkInstance inst;
  if (vkCreateInstance(&ici, NULL, &inst) != VK_SUCCESS) {
    return false;
  }
  uint32_t ct = 0;
  vkEnumeratePhysicalDevices(inst, &ct, NULL);
  bool bFound = false;
  std::vector<VkPhysicalDevice> aphys(ct);
  vkEnumeratePhysicalDevices(inst, &ct, aphys.data());
  for (VkPhysicalDevice phys : aphys) {
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(phys, &props);
    if (props.apiVersion >= VK_API_VERSION_1_3) bFound = true;
  }
  vkDestroyInstance(inst, NULL);
  return bFound;
}

static VKAPI_ATTR VkBool32 VKAPI_CALL DebugMessage(VkDebugUtilsMessageSeverityFlagBitsEXT eSeverity,
  VkDebugUtilsMessageTypeFlagsEXT ulType, const VkDebugUtilsMessengerCallbackDataEXT *pData, void *pvUser)
{
  fprintf(stderr, "Vulkan validation: %s\n", pData->pMessage);
  return VK_FALSE;
}

static bool vlkHasDXT(void)
{
  return _bBC;
}

static bool vlkInit(SDL_Window *pwnd, int iScale)
{
  @autoreleasepool {
    NSView *viewContent = ContentView(pwnd);
    if (viewContent == nil) {
      return false;
    }
    _view = [[LCVulkanView alloc] initWithFrame:viewContent.bounds];
    _view.wantsLayer = YES;
    _view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    [viewContent addSubview:_view];
    _layer = (CAMetalLayer *)_view.layer;
    _layer.opaque = YES;
    _layer.framebufferOnly = NO;   // copied from and read back too
    _iScale = iScale > 0 ? iScale : 1;
    _layer.contentsScale = _iScale;
  }

  // the instance, with the validation layer if asked for (LC_VK_VALIDATION=1)
  const bool bValidation = getenv("LC_VK_VALIDATION") != NULL && atoi(getenv("LC_VK_VALIDATION")) != 0;
  std::vector<const char *> astrExt = {VK_KHR_SURFACE_EXTENSION_NAME, VK_EXT_METAL_SURFACE_EXTENSION_NAME,
    VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME};
  if (bValidation) astrExt.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
  const char *strLayer = "VK_LAYER_KHRONOS_validation";
  VkApplicationInfo ai = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
  ai.pApplicationName = "Last Chaos";
  ai.pEngineName = "Serious Engine";
  ai.apiVersion = VK_API_VERSION_1_3;
  VkInstanceCreateInfo ici = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  ici.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
  ici.pApplicationInfo = &ai;
  ici.enabledExtensionCount = (uint32_t)astrExt.size();
  ici.ppEnabledExtensionNames = astrExt.data();
  ici.enabledLayerCount = bValidation ? 1 : 0;
  ici.ppEnabledLayerNames = &strLayer;
  VkResult r = vkCreateInstance(&ici, NULL, &_inst);
  if (r != VK_SUCCESS && bValidation) {
    fprintf(stderr, "Vulkan: no validation layer (VK_LAYER_PATH?), going on without it\n");
    astrExt.pop_back();
    ici.enabledExtensionCount = (uint32_t)astrExt.size();
    ici.enabledLayerCount = 0;
    r = vkCreateInstance(&ici, NULL, &_inst);
  }
  if (!Check(r, "vkCreateInstance")) {
    vlkEnd();
    return false;
  }
  if (bValidation && ici.enabledLayerCount > 0) {
    VkDebugUtilsMessengerCreateInfoEXT mci = {VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
    mci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    mci.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
      | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    mci.pfnUserCallback = DebugMessage;
    PFN_vkCreateDebugUtilsMessengerEXT pfn = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(_inst, "vkCreateDebugUtilsMessengerEXT");
    if (pfn != NULL) pfn(_inst, &mci, NULL, &_msgr);
  }

  VkMetalSurfaceCreateInfoEXT msci = {VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT};
  msci.pLayer = _layer;
  if (!Check(vkCreateMetalSurfaceEXT(_inst, &msci, NULL, &_surface), "vkCreateMetalSurfaceEXT")) {
    vlkEnd();
    return false;
  }

  // the first device that draws on the window
  uint32_t ct = 0;
  vkEnumeratePhysicalDevices(_inst, &ct, NULL);
  std::vector<VkPhysicalDevice> aphys(ct);
  vkEnumeratePhysicalDevices(_inst, &ct, aphys.data());
  for (VkPhysicalDevice phys : aphys) {
    uint32_t ctFamilies = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(phys, &ctFamilies, NULL);
    std::vector<VkQueueFamilyProperties> aqfp(ctFamilies);
    vkGetPhysicalDeviceQueueFamilyProperties(phys, &ctFamilies, aqfp.data());
    for (uint32_t i = 0; i < ctFamilies && _phys == VK_NULL_HANDLE; i++) {
      VkBool32 bPresent = VK_FALSE;
      vkGetPhysicalDeviceSurfaceSupportKHR(phys, i, _surface, &bPresent);
      if ((aqfp[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && bPresent) {
        _phys = phys;
        _iQueueFamily = i;
      }
    }
    if (_phys != VK_NULL_HANDLE) break;
  }
  if (_phys == VK_NULL_HANDLE) {
    fprintf(stderr, "Vulkan: no device draws on the window\n");
    vlkEnd();
    return false;
  }
  vkGetPhysicalDeviceProperties(_phys, &_props);
  vkGetPhysicalDeviceMemoryProperties(_phys, &_memprops);
  VkPhysicalDeviceFeatures featHas;
  vkGetPhysicalDeviceFeatures(_phys, &featHas);
  _bBC = featHas.textureCompressionBC == VK_TRUE;

  // the swapchain's format: BGRA as Metal's (else what the surface has)
  uint32_t ctFormats = 0;
  vkGetPhysicalDeviceSurfaceFormatsKHR(_phys, _surface, &ctFormats, NULL);
  std::vector<VkSurfaceFormatKHR> asf(ctFormats);
  vkGetPhysicalDeviceSurfaceFormatsKHR(_phys, _surface, &ctFormats, asf.data());
  _fmtColor = ctFormats > 0 ? asf[0].format : VK_FORMAT_B8G8R8A8_UNORM;
  for (const VkSurfaceFormatKHR &sf : asf) {
    if (sf.format == VK_FORMAT_B8G8R8A8_UNORM) _fmtColor = sf.format;
  }

  // the device
  uint32_t ctExt = 0;
  vkEnumerateDeviceExtensionProperties(_phys, NULL, &ctExt, NULL);
  std::vector<VkExtensionProperties> aep(ctExt);
  vkEnumerateDeviceExtensionProperties(_phys, NULL, &ctExt, aep.data());
  std::vector<const char *> astrDevExt = {VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME};
  for (const VkExtensionProperties &ep : aep) {
    if (strcmp(ep.extensionName, "VK_KHR_portability_subset") == 0) astrDevExt.push_back("VK_KHR_portability_subset");
  }
  const float fPriority = 1.0f;
  VkDeviceQueueCreateInfo qci = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
  qci.queueFamilyIndex = _iQueueFamily;
  qci.queueCount = 1;
  qci.pQueuePriorities = &fPriority;
  VkPhysicalDeviceVulkan13Features f13 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
  f13.dynamicRendering = VK_TRUE;
  VkPhysicalDeviceVulkan12Features f12 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
  f12.pNext = &f13;
  f12.timelineSemaphore = VK_TRUE;
  VkPhysicalDeviceFeatures2 f2 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
  f2.pNext = &f12;
  f2.features.fillModeNonSolid = featHas.fillModeNonSolid;
  f2.features.largePoints = featHas.largePoints;
  f2.features.samplerAnisotropy = featHas.samplerAnisotropy;
  f2.features.shaderClipDistance = featHas.shaderClipDistance;
  f2.features.textureCompressionBC = featHas.textureCompressionBC;
  f2.features.robustBufferAccess = featHas.robustBufferAccess;
  VkDeviceCreateInfo dci = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
  dci.pNext = &f2;
  dci.queueCreateInfoCount = 1;
  dci.pQueueCreateInfos = &qci;
  dci.enabledExtensionCount = (uint32_t)astrDevExt.size();
  dci.ppEnabledExtensionNames = astrDevExt.data();
  if (!Check(vkCreateDevice(_phys, &dci, NULL, &_dev), "vkCreateDevice")) {
    vlkEnd();
    return false;
  }
  vkGetDeviceQueue(_dev, _iQueueFamily, 0, &_queue);
  _pvkCmdPushDescriptorSet = (PFN_vkCmdPushDescriptorSetKHR)vkGetDeviceProcAddr(_dev, "vkCmdPushDescriptorSetKHR");

  // command buffers, ring buffers and the semaphores
  VkSemaphoreTypeCreateInfo stci = {VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
  stci.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
  stci.initialValue = 0;
  VkSemaphoreCreateInfo semci = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
  semci.pNext = &stci;
  vkCreateSemaphore(_dev, &semci, NULL, &_semTimeline);
  _ulSerial = 0;
  semci.pNext = NULL;
  for (int i = 0; i < CT_ACQUIRE; i++) {
    vkCreateSemaphore(_dev, &semci, NULL, &_asemAcquire[i]);
    _aulAcquireUser[i] = 0;
  }
  for (int i = 0; i < CT_SLOTS; i++) {
    Slot &slot = _aslot[i];
    VkCommandPoolCreateInfo cpci = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    cpci.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    cpci.queueFamilyIndex = _iQueueFamily;
    vkCreateCommandPool(_dev, &cpci, NULL, &slot.pool);
    VkCommandBufferAllocateInfo cbai = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cbai.commandPool = slot.pool;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    vkAllocateCommandBuffers(_dev, &cbai, &slot.cmd);
    vkAllocateCommandBuffers(_dev, &cbai, &slot.cmdUpload);
    if (!MakeBuffer(SL_RING, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT
      | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, slot.bufRing, slot.memRing, &slot.pubRing)) {
      vlkEnd();
      return false;
    }
    slot.ulSerial = 0;
  }

  // the layout of the draws (GLSL_* in ShaderARBToGLSL.h)
  VkDescriptorSetLayoutBinding adslb[GLSL_BINDINGS];
  for (uint32_t i = 0; i < GLSL_BINDINGS; i++) {
    VkDescriptorSetLayoutBinding &b = adslb[i];
    b = {};
    b.binding = i;
    b.descriptorCount = 1;
    if (i == GLSL_BINDING_VERTEXCONST) {
      b.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      b.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    } else if (i == GLSL_BINDING_FRAGMENTCONST) {
      b.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      b.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    } else if (i < GLSL_BINDING_TEXTURES) {
      b.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      b.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    } else {
      b.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
      b.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }
  }
  VkDescriptorSetLayoutCreateInfo dslci = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  dslci.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
  dslci.bindingCount = GLSL_BINDINGS;
  dslci.pBindings = adslb;
  vkCreateDescriptorSetLayout(_dev, &dslci, NULL, &_dsl);
  VkPushConstantRange pcr = {VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(GpuStreamBases)};
  VkPipelineLayoutCreateInfo plci = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  plci.setLayoutCount = 1;
  plci.pSetLayouts = &_dsl;
  plci.pushConstantRangeCount = 1;
  plci.pPushConstantRanges = &pcr;
  vkCreatePipelineLayout(_dev, &plci, NULL, &_pl);
  VkPipelineCacheCreateInfo pcci = {VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
  vkCreatePipelineCache(_dev, &pcci, NULL, &_pc);

  if (!_bGlslang) {
    glslang_initialize_process();
    _bGlslang = true;
  }

  // what is bound where nothing is: zeros, a white texel
  _pbufDummy = new VlkBuffer;
  MakeBuffer(256, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, _pbufDummy->buf, _pbufDummy->mem, &_pbufDummy->pub);
  memset(_pbufDummy->pub, 0, 256);
  _pbufDummy->slSize = 256;
  VkSamplerCreateInfo smci = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
  smci.maxLod = 0.25f;
  vkCreateSampler(_dev, &smci, NULL, &_smpDummy);

  _st = State();
  _bPassFresh = true;
  _bVSync = _bSwapVSync = true;
  if (!MakeSwapchain()) {
    vlkEnd();
    return false;
  }
  _ptexWhite = new VlkTexture;
  _ptexWhite->iW = _ptexWhite->iH = _ptexWhite->ctMips = 1;
  _ptexWhite->eFormat = GPUTF_RGBA8;
  NewStorage(_ptexWhite);
  const uint32_t ulWhite = 0xFFFFFFFF;
  UploadMip(_ptexWhite, 0, 1, 1, &ulWhite, 4);
  fprintf(stderr, "Vulkan: %s, drawable %ux%u\n", _props.deviceName, _extSwap.width, _extSwap.height);
  return true;
}

static void DeleteTextureNow(VlkTexture *ptex)
{
  if (ptex == NULL) return;
  if (ptex->view != VK_NULL_HANDLE) vkDestroyImageView(_dev, ptex->view, NULL);
  if (ptex->img != VK_NULL_HANDLE) vkDestroyImage(_dev, ptex->img, NULL);
  if (ptex->mem != VK_NULL_HANDLE) vkFreeMemory(_dev, ptex->mem, NULL);
  delete ptex;
}

static void DeleteBufferNow(VlkBuffer *pbuf)
{
  if (pbuf == NULL) return;
  if (pbuf->buf != VK_NULL_HANDLE) vkDestroyBuffer(_dev, pbuf->buf, NULL);
  if (pbuf->mem != VK_NULL_HANDLE) vkFreeMemory(_dev, pbuf->mem, NULL);
  delete pbuf;
}

static void vlkEnd(void)
{
  if (_dev != VK_NULL_HANDLE) {
    vlkFinish();
    vkDeviceWaitIdle(_dev);
    CollectGarbage(true);
    _ptexTarget = NULL;
    _iImage = -1;
    _semAcquirePending = VK_NULL_HANDLE;
    _asavStates.clear();
    for (int i = 0; i < GPUS_COUNT; i++) _astr[i] = Stream();
    for (int i = 0; i < GPU_MAXTEXUNITS; i++) _aunit[i] = Unit();
    for (auto &it : _mapPipelines) vkDestroyPipeline(_dev, it.second, NULL);
    _mapPipelines.clear();
    for (auto &it : _mapSamplers) vkDestroySampler(_dev, it.second, NULL);
    _mapSamplers.clear();
    _pipeSet = VK_NULL_HANDLE;
    for (VlkTexture *ptex : _setTextures) {
      DiscardTexture(ptex);
      ptex->ulLastUse = 0;
    }
    for (VlkBuffer *pbuf : _setBuffers) {
      Discard(VK_NULL_HANDLE, VK_NULL_HANDLE, pbuf->mem, pbuf->buf);
      pbuf->buf = VK_NULL_HANDLE;
      pbuf->mem = VK_NULL_HANDLE;
      pbuf->pub = NULL;
      pbuf->ulLastUse = 0;
    }
    CollectGarbage(true);
    DeleteBufferNow(_pbufQuadIndices);  _pbufQuadIndices = NULL;  _ctQuadIndices = 0;
    DeleteBufferNow(_pbufDummy);  _pbufDummy = NULL;
    DeleteTextureNow(_ptexWhite);  _ptexWhite = NULL;
    if (_smpDummy != VK_NULL_HANDLE) vkDestroySampler(_dev, _smpDummy, NULL);
    _smpDummy = VK_NULL_HANDLE;
    if (_pc != VK_NULL_HANDLE) vkDestroyPipelineCache(_dev, _pc, NULL);
    if (_pl != VK_NULL_HANDLE) vkDestroyPipelineLayout(_dev, _pl, NULL);
    if (_dsl != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(_dev, _dsl, NULL);
    _pc = VK_NULL_HANDLE;  _pl = VK_NULL_HANDLE;  _dsl = VK_NULL_HANDLE;
    DestroySwapchainImages();
    Discard(_imgDepth, _viewDepth, _memDepth, VK_NULL_HANDLE);
    _imgDepth = VK_NULL_HANDLE;  _viewDepth = VK_NULL_HANDLE;  _memDepth = VK_NULL_HANDLE;
    CollectGarbage(true);
    if (_swapchain != VK_NULL_HANDLE) vkDestroySwapchainKHR(_dev, _swapchain, NULL);
    _swapchain = VK_NULL_HANDLE;
    for (int i = 0; i < CT_SLOTS; i++) {
      Slot &slot = _aslot[i];
      if (slot.bufRing != VK_NULL_HANDLE) vkDestroyBuffer(_dev, slot.bufRing, NULL);
      if (slot.memRing != VK_NULL_HANDLE) vkFreeMemory(_dev, slot.memRing, NULL);
      if (slot.pool != VK_NULL_HANDLE) vkDestroyCommandPool(_dev, slot.pool, NULL);
      slot = Slot();
    }
    for (int i = 0; i < CT_ACQUIRE; i++) {
      if (_asemAcquire[i] != VK_NULL_HANDLE) vkDestroySemaphore(_dev, _asemAcquire[i], NULL);
      _asemAcquire[i] = VK_NULL_HANDLE;
    }
    if (_semTimeline != VK_NULL_HANDLE) vkDestroySemaphore(_dev, _semTimeline, NULL);
    _semTimeline = VK_NULL_HANDLE;
    vkDestroyDevice(_dev, NULL);
    _dev = VK_NULL_HANDLE;
  }
  if (_surface != VK_NULL_HANDLE) vkDestroySurfaceKHR(_inst, _surface, NULL);
  _surface = VK_NULL_HANDLE;
  if (_msgr != VK_NULL_HANDLE) {
    PFN_vkDestroyDebugUtilsMessengerEXT pfn = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(_inst, "vkDestroyDebugUtilsMessengerEXT");
    if (pfn != NULL) pfn(_inst, _msgr, NULL);
    _msgr = VK_NULL_HANDLE;
  }
  if (_inst != VK_NULL_HANDLE) vkDestroyInstance(_inst, NULL);
  _inst = VK_NULL_HANDLE;
  _phys = VK_NULL_HANDLE;
  @autoreleasepool {
    [_view removeFromSuperview];
    _view = nil;
    _layer = nil;
  }
}

static const char *vlkDeviceName(void)
{
  return _dev != VK_NULL_HANDLE ? _props.deviceName : "";
}

static void vlkSetDrawableScale(int iScale)
{
  _iScale = iScale > 0 ? iScale : 1;
}

static void vlkSetVSync(bool bWait)
{
  _bVSync = bWait;
}

static void vlkPresent(void)
{
  EndPass();
  if (_iImage < 0) {
    Submit(VK_NULL_HANDLE);
    return;
  }
  BeginCommandBuffer();
  Barrier(_cmd, _aimgSwap[_iImage], VK_IMAGE_ASPECT_COLOR_BIT, _eImageLayout, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
  VkSemaphore semRendered = _asemRendered[_iImage];
  Submit(semRendered);
  VkPresentInfoKHR pi = {VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
  pi.waitSemaphoreCount = 1;
  pi.pWaitSemaphores = &semRendered;
  pi.swapchainCount = 1;
  pi.pSwapchains = &_swapchain;
  const uint32_t iImage = (uint32_t)_iImage;
  pi.pImageIndices = &iImage;
  const VkResult r = vkQueuePresentKHR(_queue, &pi);
  if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) {
    _bSwapDirty = true;
  } else {
    Check(r, "vkQueuePresentKHR");
  }
  _iImage = -1;
}

static void vlkFinish(void)
{
  Flush(true);
}


// RENDER TARGETS AND READING THE FRAME

static void vlkTextureRenderTarget(GpuTexture *ptexGpu, int iW, int iH)
{
  VlkTexture *ptex = static_cast<VlkTexture *>(ptexGpu);
  if (_ptexTarget == ptex) {
    vlkSetRenderTarget(NULL);
  }
  DiscardTexture(ptex);
  MakeImage(iW, iH, 1, _fmtColor, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
    | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_COLOR_BIT, ptex->img, ptex->mem, ptex->view);
  MakeImage(iW, iH, 1, FMT_DEPTH, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT,
    ptex->imgDepth, ptex->memDepth, ptex->viewDepth);
  VkCommandBuffer cmd = UploadCommands();
  if (ptex->img != VK_NULL_HANDLE) {
    Barrier(cmd, ptex->img, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  }
  if (ptex->imgDepth != VK_NULL_HANDLE) {
    Barrier(cmd, ptex->imgDepth, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
  }
  ptex->iW = iW;  ptex->iH = iH;
  ptex->ctMips = 1;
  ptex->eFormat = GPUTF_RGBA8;
  ptex->bTarget = true;
  ptex->ulLastUse = 0;
}

static void vlkSetRenderTarget(GpuTexture *ptexGpu)
{
  VlkTexture *ptex = static_cast<VlkTexture *>(ptexGpu);
  if (ptex == _ptexTarget) {
    return;
  }
  EndPass();
  _ptexTarget = (ptex != NULL && ptex->img != VK_NULL_HANDLE && ptex->bTarget && ptex->imgDepth != VK_NULL_HANDLE) ? ptex : NULL;
}

static GpuTexture *vlkGetRenderTarget(void)
{
  return _ptexTarget;
}

// the image of the window's frame (taken if nothing was drawn yet), for copies and reads; the
// command buffer is open and no pass is
static VkImage FrameImage(void)
{
  if (!Acquire()) {
    return VK_NULL_HANDLE;
  }
  // not drawn to yet: it gets cleared, as the first pass would
  if (_bDrawableNew) {
    VlkTexture *ptexTarget = _ptexTarget;
    _ptexTarget = NULL;
    BeginPass();
    EndPass();
    _ptexTarget = ptexTarget;
  }
  EndPass();
  BeginCommandBuffer();
  return _aimgSwap[_iImage];
}

static void vlkCopyFrameToTexture(GpuTexture *ptexGpu, int iX, int iY, int iW, int iH)
{
  VlkTexture *ptex = static_cast<VlkTexture *>(ptexGpu);
  EndPass();
  VkImage imgFrame = FrameImage();
  if (imgFrame == VK_NULL_HANDLE) {
    return;
  }
  if (iX < 0) { iW += iX;  iX = 0; }
  if (iY < 0) { iH += iY;  iY = 0; }
  if (iX + iW > (int)_extSwap.width)  iW = (int)_extSwap.width - iX;
  if (iY + iH > (int)_extSwap.height) iH = (int)_extSwap.height - iY;
  if (iW <= 0 || iH <= 0) {
    return;
  }
  // new storage when the size changes (the draws before keep the old one)
  VkImageLayout eOld = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  if (ptex->img == VK_NULL_HANDLE || !ptex->bTarget || ptex->iW != iW || ptex->iH != iH) {
    if (_ptexTarget == ptex) {
      vlkSetRenderTarget(NULL);
    }
    DiscardTexture(ptex);
    if (!MakeImage(iW, iH, 1, _fmtColor, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
      VK_IMAGE_ASPECT_COLOR_BIT, ptex->img, ptex->mem, ptex->view)) {
      return;
    }
    ptex->iW = iW;  ptex->iH = iH;
    ptex->ctMips = 1;
    ptex->eFormat = GPUTF_RGBA8;
    ptex->bTarget = true;
    eOld = VK_IMAGE_LAYOUT_UNDEFINED;
  }
  Barrier(_cmd, ptex->img, VK_IMAGE_ASPECT_COLOR_BIT, eOld, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
  Barrier(_cmd, imgFrame, VK_IMAGE_ASPECT_COLOR_BIT, _eImageLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
  VkImageCopy ic = {};
  ic.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  ic.srcOffset = {iX, iY, 0};
  ic.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  ic.extent = {(uint32_t)iW, (uint32_t)iH, 1};
  vkCmdCopyImage(_cmd, imgFrame, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, ptex->img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &ic);
  Barrier(_cmd, imgFrame, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
  _eImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  Barrier(_cmd, ptex->img, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  ptex->ulLastUse = _ulSerial;
}

// copy what the frame has into a buffer and wait for it (false if there is no frame). The depth
// buffer stays from frame to frame: before anything is drawn it has the last frame's depths (as
// OpenGL's back buffer has them).
static bool ReadFrame(bool bDepth, int iX, int iY, int iW, int iH, std::vector<uint8_t> &aub)
{
  if (bDepth && (!_bDepthDrawn || _imgDepth == VK_NULL_HANDLE)) {
    return false;
  }
  EndPass();
  VkImage img = bDepth ? _imgDepth : FrameImage();
  if (img == VK_NULL_HANDLE) {
    return false;
  }
  BeginCommandBuffer();
  const size_t slSize = (size_t)iW*iH*4;
  VkBuffer buf;
  VkDeviceMemory mem;
  uint8_t *pub;
  if (!MakeBuffer(slSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT, buf, mem, &pub)) {
    return false;
  }
  const VkImageAspectFlags ulAspect = bDepth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
  VkImageLayout &eLayout = bDepth ? _eDepthLayout : _eImageLayout;
  Barrier(_cmd, img, ulAspect, eLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
  VkBufferImageCopy bic = {};
  bic.imageSubresource = {ulAspect, 0, 0, 1};
  bic.imageOffset = {iX, iY, 0};
  bic.imageExtent = {(uint32_t)iW, (uint32_t)iH, 1};
  vkCmdCopyImageToBuffer(_cmd, img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buf, 1, &bic);
  const VkImageLayout eBack = bDepth ? VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  Barrier(_cmd, img, ulAspect, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, eBack);
  eLayout = eBack;
  // the image stays for the rest of the frame
  Flush(true);
  aub.assign(pub, pub + slSize);
  vkDestroyBuffer(_dev, buf, NULL);
  vkFreeMemory(_dev, mem, NULL);
  return true;
}

static bool vlkReadFrameRGB(int iX, int iY, int iW, int iH, uint8_t *pubRGB)
{
  if (_dev == VK_NULL_HANDLE) {
    return false;
  }
  if (!Acquire()) {
    return false;
  }
  if (iX < 0 || iY < 0 || iW <= 0 || iH <= 0 || iX + iW > (int)_extSwap.width || iY + iH > (int)_extSwap.height) {
    return false;
  }
  std::vector<uint8_t> aub;
  if (!ReadFrame(false, iX, iY, iW, iH, aub)) {
    return false;
  }
  const bool bBGRA = _fmtColor == VK_FORMAT_B8G8R8A8_UNORM || _fmtColor == VK_FORMAT_B8G8R8A8_SRGB;
  const uint8_t *pub = aub.data();
  for (int i = 0; i < iW*iH; i++, pub += 4) {
    *pubRGB++ = pub[bBGRA ? 2 : 0];
    *pubRGB++ = pub[1];
    *pubRGB++ = pub[bBGRA ? 0 : 2];
  }
  return true;
}

static bool vlkReadFrameDepths(const int *piXY, int ctPoints, float *pfDepths)
{
  if (ctPoints <= 0) {
    return true;
  }
  if (_imgDepth == VK_NULL_HANDLE) {
    return false;
  }
  // one copy of the rectangle around all the points
  const int iFrameW = (int)_extSwap.width, iFrameH = (int)_extSwap.height;
  int iX0 = iFrameW, iY0 = iFrameH, iX1 = -1, iY1 = -1;
  for (int i = 0; i < ctPoints; i++) {
    const int iX = piXY[i*2], iY = piXY[i*2+1];
    if (iX < 0 || iY < 0 || iX >= iFrameW || iY >= iFrameH) continue;
    if (iX < iX0) iX0 = iX;
    if (iY < iY0) iY0 = iY;
    if (iX > iX1) iX1 = iX;
    if (iY > iY1) iY1 = iY;
  }
  for (int i = 0; i < ctPoints; i++) {
    pfDepths[i] = 0.0f;   // (outside: hidden)
  }
  if (iX1 < iX0) {
    return true;
  }
  const int iW = iX1-iX0+1, iH = iY1-iY0+1;
  std::vector<uint8_t> aub;
  if (!ReadFrame(true, iX0, iY0, iW, iH, aub)) {
    return false;
  }
  for (int i = 0; i < ctPoints; i++) {
    const int iX = piXY[i*2], iY = piXY[i*2+1];
    if (iX < iX0 || iY < iY0 || iX > iX1 || iY > iY1) continue;
    memcpy(&pfDepths[i], &aub[((size_t)(iY-iY0)*iW + (iX-iX0))*4], sizeof(float));
  }
  return true;
}


// RENDER STATE

static void vlkSetViewport(int iX, int iY, int iW, int iH, float fMinZ, float fMaxZ)
{
  _st.aiViewport[0] = iX;  _st.aiViewport[1] = iY;
  _st.aiViewport[2] = iW;  _st.aiViewport[3] = iH;
  _st.fMinZ = fMinZ;  _st.fMaxZ = fMaxZ;
}

static void vlkSetScissor(int iX, int iY, int iW, int iH)
{
  _st.aiScissor[0] = iX;  _st.aiScissor[1] = iY;
  _st.aiScissor[2] = iW;  _st.aiScissor[3] = iH;
}

static void vlkSetBlend(bool bBlend, GpuBlend eSrc, GpuBlend eDst)
{
  _st.bBlend = bBlend;  _st.eSrc = eSrc;  _st.eDst = eDst;
}

static void vlkSetColorMask(bool bR, bool bG, bool bB, bool bA)
{
  _st.ubColorMask = (bR ? 1 : 0) | (bG ? 2 : 0) | (bB ? 4 : 0) | (bA ? 8 : 0);
}

static void vlkSetDepth(bool bTest, bool bWrite, GpuCompare eFunc)
{
  _st.bDepthTest = bTest;  _st.bDepthWrite = bWrite;  _st.eDepthFunc = eFunc;
}

static void vlkSetCull(GpuCull eCull, bool bFrontCCW)
{
  _st.eCull = eCull;  _st.bFrontCCW = bFrontCCW;
}

static void vlkSetWireframe(bool bWireframe)
{
  _st.bWireframe = bWireframe;
}

static void vlkSetDepthBias(bool bBias)
{
  _st.bDepthBias = bBias;
}

static void vlkSetProgram(GpuProgram *pprVertex, GpuProgram *pprFragment)
{
  _st.pprVertex = static_cast<VlkProgram *>(pprVertex);  _st.pprFragment = static_cast<VlkProgram *>(pprFragment);
}

static void vlkSetTexture(int iUnit, GpuTexture *ptex, const GpuSampler &smp)
{
  _aunit[iUnit].ptex = static_cast<VlkTexture *>(ptex);
  _aunit[iUnit].smp = smp;
}

static void vlkSetTextureNone(int iUnit)
{
  _aunit[iUnit].ptex = NULL;
}

static void vlkSetTextureWrap(int iUnit, bool bClampU, bool bClampV)
{
  _aunit[iUnit].smp.ubClampU = bClampU ? 1 : 0;
  _aunit[iUnit].smp.ubClampV = bClampV ? 1 : 0;
}

static void vlkSetStreamData(GpuStream eStream, const void *pvData, size_t slSize)
{
  Stream &str = _astr[eStream];
  str.pvData = pvData;
  str.slSize = slSize;
  str.ulCopied = 0;
  str.bufCopy = VK_NULL_HANDLE;
  str.pbuf = NULL;
}

static void vlkSetStreamBuffer(GpuStream eStream, GpuBuffer *pbuf, size_t slOffset)
{
  Stream &str = _astr[eStream];
  str.pvData = NULL;
  str.ulCopied = 0;
  str.bufCopy = VK_NULL_HANDLE;
  str.pbuf = static_cast<VlkBuffer *>(pbuf);
  str.slOffset = slOffset;
}

static void vlkClearStream(GpuStream eStream)
{
  _astr[eStream] = Stream();
}

static uint32_t vlkGetStreamMask(void)
{
  uint32_t ulMask = 0;
  for (int iStream = 0; iStream < GPUS_COUNT; iStream++) {
    const Stream &str = _astr[iStream];
    if (str.pbuf != NULL || (str.pvData != NULL && str.slSize > 0)) {
      ulMask |= 1u << iStream;
    }
  }
  return ulMask;
}

static void vlkPushState(void)
{
  _asavStates.emplace_back();
  SavedState &sav = _asavStates.back();
  sav.st = _st;
  for (int i = 0; i < GPUS_COUNT; i++) sav.astr[i] = _astr[i];
  for (int i = 0; i < GPU_MAXTEXUNITS; i++) sav.aunit[i] = _aunit[i];
  sav.aubVertexConst = _aubVertexConst;
  sav.aubFragmentConst = _aubFragmentConst;
}

static void vlkPopState(void)
{
  if (_asavStates.empty()) {
    return;
  }
  SavedState &sav = _asavStates.back();
  _st = sav.st;
  for (int i = 0; i < GPUS_COUNT; i++) _astr[i] = sav.astr[i];
  for (int i = 0; i < GPU_MAXTEXUNITS; i++) _aunit[i] = sav.aunit[i];
  _aubVertexConst.swap(sav.aubVertexConst);
  _aubFragmentConst.swap(sav.aubFragmentConst);
  _asavStates.pop_back();
}

static void vlkSetVertexConstants(const void *pvData, size_t slSize)
{
  _aubVertexConst.assign((const uint8_t *)pvData, (const uint8_t *)pvData + slSize);
}

static void vlkSetFragmentConstants(const void *pvData, size_t slSize)
{
  _aubFragmentConst.assign((const uint8_t *)pvData, (const uint8_t *)pvData + slSize);
}


static VkBlendFactor BlendFactor(GpuBlend e)
{
  switch (e) {
  case GPUB_ONE:           return VK_BLEND_FACTOR_ONE;
  case GPUB_ZERO:          return VK_BLEND_FACTOR_ZERO;
  case GPUB_SRC_COLOR:     return VK_BLEND_FACTOR_SRC_COLOR;
  case GPUB_INV_SRC_COLOR: return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
  case GPUB_DST_COLOR:     return VK_BLEND_FACTOR_DST_COLOR;
  case GPUB_INV_DST_COLOR: return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
  case GPUB_SRC_ALPHA:     return VK_BLEND_FACTOR_SRC_ALPHA;
  case GPUB_INV_SRC_ALPHA: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  case GPUB_DST_ALPHA:     return VK_BLEND_FACTOR_DST_ALPHA;
  case GPUB_INV_DST_ALPHA: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
  }
  return VK_BLEND_FACTOR_ONE;
}

static VkCompareOp CompareOp(GpuCompare e)
{
  switch (e) {
  case GPUC_NEVER:         return VK_COMPARE_OP_NEVER;
  case GPUC_LESS:          return VK_COMPARE_OP_LESS;
  case GPUC_LESS_EQUAL:    return VK_COMPARE_OP_LESS_OR_EQUAL;
  case GPUC_EQUAL:         return VK_COMPARE_OP_EQUAL;
  case GPUC_NOT_EQUAL:     return VK_COMPARE_OP_NOT_EQUAL;
  case GPUC_GREATER_EQUAL: return VK_COMPARE_OP_GREATER_OR_EQUAL;
  case GPUC_GREATER:       return VK_COMPARE_OP_GREATER;
  case GPUC_ALWAYS:        return VK_COMPARE_OP_ALWAYS;
  }
  return VK_COMPARE_OP_ALWAYS;
}

static VkPipeline Pipeline(Topology eTopology)
{
  const State &st = _st;
  GpuBlend eSrc = st.eSrc, eDst = st.eDst;
  if (!st.bBlend) {
    eSrc = GPUB_ONE;
    eDst = GPUB_ZERO;
  }
  const bool bWireframe = st.bWireframe && eTopology == TOP_TRIANGLES;
  PipelineKey key;
  key.a = (uint64_t)st.pprVertex->ulID | ((uint64_t)st.pprFragment->ulID << 32);
  key.b = (uint64_t)st.bBlend | ((uint64_t)eSrc << 1) | ((uint64_t)eDst << 5) | ((uint64_t)st.ubColorMask << 9)
    | ((uint64_t)st.bDepthTest << 13) | ((uint64_t)st.bDepthWrite << 14) | ((uint64_t)st.eDepthFunc << 15)
    | ((uint64_t)st.eCull << 18) | ((uint64_t)st.bFrontCCW << 20) | ((uint64_t)bWireframe << 21)
    | ((uint64_t)st.bDepthBias << 22) | ((uint64_t)eTopology << 23);
  auto it = _mapPipelines.find(key);
  if (it != _mapPipelines.end()) {
    return it->second;
  }
  VkPipelineShaderStageCreateInfo assci[2] = {{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO},
                                              {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}};
  assci[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  assci[0].module = st.pprVertex->mod;
  assci[0].pName = "main";
  assci[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  assci[1].module = st.pprFragment->mod;
  assci[1].pName = "main";
  VkPipelineVertexInputStateCreateInfo vis = {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  VkPipelineInputAssemblyStateCreateInfo ias = {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
  ias.topology = eTopology == TOP_LINES ? VK_PRIMITIVE_TOPOLOGY_LINE_LIST
               : eTopology == TOP_POINTS ? VK_PRIMITIVE_TOPOLOGY_POINT_LIST : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkPipelineViewportStateCreateInfo vps = {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
  vps.viewportCount = 1;
  vps.scissorCount = 1;
  VkPipelineRasterizationStateCreateInfo rs = {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
  rs.polygonMode = bWireframe ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
  rs.cullMode = st.eCull == GPUCULL_FRONT ? VK_CULL_MODE_FRONT_BIT : st.eCull == GPUCULL_BACK ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE;
  rs.frontFace = st.bFrontCCW ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE;
  rs.depthBiasEnable = st.bDepthBias;
  rs.lineWidth = 1.0f;
  VkPipelineMultisampleStateCreateInfo ms = {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
  ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  // as in OpenGL, no depth test means no depth writes either
  VkPipelineDepthStencilStateCreateInfo dss = {VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
  dss.depthTestEnable = st.bDepthTest;
  dss.depthWriteEnable = st.bDepthTest && st.bDepthWrite;
  dss.depthCompareOp = st.bDepthTest ? CompareOp(st.eDepthFunc) : VK_COMPARE_OP_ALWAYS;
  VkPipelineColorBlendAttachmentState cba = {};
  cba.blendEnable = st.bBlend;
  cba.srcColorBlendFactor = cba.srcAlphaBlendFactor = BlendFactor(eSrc);
  cba.dstColorBlendFactor = cba.dstAlphaBlendFactor = BlendFactor(eDst);
  cba.colorBlendOp = cba.alphaBlendOp = VK_BLEND_OP_ADD;
  cba.colorWriteMask = ((st.ubColorMask & 1) ? VK_COLOR_COMPONENT_R_BIT : 0) | ((st.ubColorMask & 2) ? VK_COLOR_COMPONENT_G_BIT : 0)
    | ((st.ubColorMask & 4) ? VK_COLOR_COMPONENT_B_BIT : 0) | ((st.ubColorMask & 8) ? VK_COLOR_COMPONENT_A_BIT : 0);
  VkPipelineColorBlendStateCreateInfo cbs = {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
  cbs.attachmentCount = 1;
  cbs.pAttachments = &cba;
  const VkDynamicState aeDynamic[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_DEPTH_BIAS};
  VkPipelineDynamicStateCreateInfo dys = {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
  dys.dynamicStateCount = 3;
  dys.pDynamicStates = aeDynamic;
  VkPipelineRenderingCreateInfo prci = {VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
  prci.colorAttachmentCount = 1;
  prci.pColorAttachmentFormats = &_fmtColor;
  prci.depthAttachmentFormat = FMT_DEPTH;
  VkGraphicsPipelineCreateInfo gpci = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
  gpci.pNext = &prci;
  gpci.stageCount = 2;
  gpci.pStages = assci;
  gpci.pVertexInputState = &vis;
  gpci.pInputAssemblyState = &ias;
  gpci.pViewportState = &vps;
  gpci.pRasterizationState = &rs;
  gpci.pMultisampleState = &ms;
  gpci.pDepthStencilState = &dss;
  gpci.pColorBlendState = &cbs;
  gpci.pDynamicState = &dys;
  gpci.layout = _pl;
  VkPipeline pipe = VK_NULL_HANDLE;
  Check(vkCreateGraphicsPipelines(_dev, _pc, 1, &gpci, NULL, &pipe), "vkCreateGraphicsPipelines");
  _mapPipelines[key] = pipe;
  return pipe;
}

static VkSampler Sampler(const GpuSampler &smp)
{
  const uint64_t ulKey = smp.ubMagLinear | (smp.ubMinLinear << 1) | (smp.ubMip << 2) | (smp.ubClampU << 4)
    | (smp.ubClampV << 5) | ((uint64_t)smp.ubAnisotropy << 8);
  auto it = _mapSamplers.find(ulKey);
  if (it != _mapSamplers.end()) {
    return it->second;
  }
  VkSamplerCreateInfo sci = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
  sci.magFilter = smp.ubMagLinear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
  sci.minFilter = smp.ubMinLinear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
  sci.mipmapMode = smp.ubMip == 2 ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
  sci.addressModeU = smp.ubClampU ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE : VK_SAMPLER_ADDRESS_MODE_REPEAT;
  sci.addressModeV = smp.ubClampV ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE : VK_SAMPLER_ADDRESS_MODE_REPEAT;
  sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  sci.anisotropyEnable = smp.ubAnisotropy > 1 ? VK_TRUE : VK_FALSE;
  sci.maxAnisotropy = smp.ubAnisotropy > 1 ? smp.ubAnisotropy : 1;
  // without mipmaps: the first one only (0.25: the filters still choose between min and mag)
  sci.minLod = 0.0f;
  sci.maxLod = smp.ubMip == 0 ? 0.25f : VK_LOD_CLAMP_NONE;
  VkSampler s = VK_NULL_HANDLE;
  vkCreateSampler(_dev, &sci, NULL, &s);
  _mapSamplers[ulKey] = s;
  return s;
}

// the scissor rectangle inside the render target (false if nothing of it is left)
static bool ClampedScissor(VkRect2D &rc)
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
  rc.offset = {iX0, iY0};
  rc.extent = {(uint32_t)(iX1 - iX0), (uint32_t)(iY1 - iY0)};
  return true;
}

// a stream's data as a descriptor and its first word (storage buffer offsets must be aligned)
static void StreamBuffer(VkBuffer buf, size_t slOffset, VkDescriptorBufferInfo &dbi, uint32_t &ulBase)
{
  const size_t slAlign = _props.limits.minStorageBufferOffsetAlignment > 0 ? _props.limits.minStorageBufferOffsetAlignment : 16;
  const size_t slAligned = slOffset / slAlign * slAlign;
  dbi.buffer = buf;
  dbi.offset = slAligned;
  dbi.range = VK_WHOLE_SIZE;
  ulBase = (uint32_t)((slOffset - slAligned) / 4);
}

// put the state into the command buffer (false if there is nothing to draw on)
static bool PrepareDraw(Topology eTopology)
{
  if (_st.pprVertex == NULL || _st.pprFragment == NULL) {
    return false;
  }
  if (!BeginPass()) {
    return false;
  }
  VkRect2D rcScissor;
  if (!ClampedScissor(rcScissor) || _st.aiViewport[2] <= 0 || _st.aiViewport[3] <= 0) {
    return false;
  }
  VkPipeline pipe = Pipeline(eTopology);
  if (pipe == VK_NULL_HANDLE) {
    return false;
  }
  const bool bAll = _bPassFresh;
  if (bAll || pipe != _pipeSet) {
    vkCmdBindPipeline(_cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
    _pipeSet = pipe;
  }
  if (bAll || memcmp(_st.aiViewport, _stSet.aiViewport, sizeof(_st.aiViewport)) != 0
   || _st.fMinZ != _stSet.fMinZ || _st.fMaxZ != _stSet.fMaxZ) {
    // upside down: y up in the projections, as with Direct3D and Metal
    VkViewport vp;
    vp.x = (float)_st.aiViewport[0];
    vp.y = (float)(_st.aiViewport[1] + _st.aiViewport[3]);
    vp.width = (float)_st.aiViewport[2];
    vp.height = -(float)_st.aiViewport[3];
    vp.minDepth = _st.fMinZ;
    vp.maxDepth = _st.fMaxZ;
    vkCmdSetViewport(_cmd, 0, 1, &vp);
  }
  if (bAll || memcmp(&rcScissor, &_rcScissorSet, sizeof(rcScissor)) != 0) {
    vkCmdSetScissor(_cmd, 0, 1, &rcScissor);
    _rcScissorSet = rcScissor;
  }
  if (bAll || _st.bDepthBias != _stSet.bDepthBias) {
    // glPolygonOffset(-1,-2)
    if (_st.bDepthBias) vkCmdSetDepthBias(_cmd, -2.0f, 0.0f, -1.0f);
    else vkCmdSetDepthBias(_cmd, 0.0f, 0.0f, 0.0f);
  }
  _stSet = _st;
  _bPassFresh = false;

  VkDescriptorBufferInfo adbi[GLSL_BINDING_TEXTURES];
  VkDescriptorImageInfo adii[GPU_MAXTEXUNITS];
  VkWriteDescriptorSet awds[GLSL_BINDINGS];
  GpuStreamBases sb;
  // constants (at least what the shaders' blocks have: the rest of a ring part is there)
  static const uint8_t aubNone[16] = {0};
  for (int i = 0; i < 2; i++) {
    const std::vector<uint8_t> &aub = i == 0 ? _aubVertexConst : _aubFragmentConst;
    VkBuffer buf;
    size_t slOffset;
    Upload(aub.empty() ? aubNone : aub.data(), aub.empty() ? sizeof(aubNone) : aub.size(), buf, slOffset);
    adbi[i].buffer = buf;
    adbi[i].offset = slOffset;
    adbi[i].range = VK_WHOLE_SIZE;
  }
  // vertex streams
  for (int iStream = 0; iStream < GPUS_COUNT; iStream++) {
    Stream &str = _astr[iStream];
    VkDescriptorBufferInfo &dbi = adbi[GLSL_BINDING_STREAMS + iStream];
    if (str.pbuf != NULL && str.pbuf->buf != VK_NULL_HANDLE) {
      StreamBuffer(str.pbuf->buf, str.slOffset, dbi, sb.aulBase[iStream]);
      str.pbuf->ulLastUse = _ulSerial;
    } else if (str.pvData != NULL && str.slSize > 0) {
      if (str.ulCopied != _ulSerial) {
        VkBuffer buf;
        Upload(str.pvData, str.slSize, buf, str.slCopy);
        str.bufCopy = buf;
        str.ulCopied = _ulSerial;
      }
      StreamBuffer(str.bufCopy, str.slCopy, dbi, sb.aulBase[iStream]);
    } else {
      StreamBuffer(_pbufDummy->buf, 0, dbi, sb.aulBase[iStream]);
    }
  }
  // textures
  for (int iUnit = 0; iUnit < GPU_MAXTEXUNITS; iUnit++) {
    Unit &unit = _aunit[iUnit];
    VkDescriptorImageInfo &dii = adii[iUnit];
    dii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    if (unit.ptex != NULL && unit.ptex->img != VK_NULL_HANDLE && unit.ptex != _ptexPass) {
      dii.imageView = unit.ptex->view;
      dii.sampler = Sampler(unit.smp);
      unit.ptex->ulLastUse = _ulSerial;
    } else {
      dii.imageView = _ptexWhite->view;
      dii.sampler = _smpDummy;
    }
  }
  for (uint32_t i = 0; i < GLSL_BINDINGS; i++) {
    VkWriteDescriptorSet &wds = awds[i];
    wds = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    wds.dstBinding = i;
    wds.descriptorCount = 1;
    if (i < GLSL_BINDING_STREAMS) {
      wds.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      wds.pBufferInfo = &adbi[i];
    } else if (i < GLSL_BINDING_TEXTURES) {
      wds.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      wds.pBufferInfo = &adbi[i];
    } else {
      wds.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
      wds.pImageInfo = &adii[i - GLSL_BINDING_TEXTURES];
    }
  }
  _pvkCmdPushDescriptorSet(_cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _pl, 0, GLSL_BINDINGS, awds);
  vkCmdPushConstants(_cmd, _pl, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(sb), &sb);
  return true;
}

static void vlkDrawIndexed(int ctIndices, const uint16_t *puwIndices)
{
  if (ctIndices <= 0 || !PrepareDraw(TOP_TRIANGLES)) {
    return;
  }
  VkBuffer buf;
  size_t slOffset;
  Upload(puwIndices, ctIndices*sizeof(uint16_t), buf, slOffset);
  vkCmdBindIndexBuffer(_cmd, buf, slOffset, VK_INDEX_TYPE_UINT16);
  vkCmdDrawIndexed(_cmd, ctIndices, 1, 0, 0, 0);
}

static void vlkDrawQuads(int ctVertices)
{
  const int ctQuads = ctVertices/4;
  if (ctQuads <= 0 || !PrepareDraw(TOP_TRIANGLES)) {
    return;
  }
  // (0,1,2) and (0,2,3) of each quad, as OpenGL draws them
  const int ctIndices = ctQuads*6;
  if (ctIndices > _ctQuadIndices) {
    int ctNew = _ctQuadIndices > 0 ? _ctQuadIndices : 6*1024;
    while (ctNew < ctIndices) ctNew *= 2;
    if (_pbufQuadIndices != NULL) {
      Discard(VK_NULL_HANDLE, VK_NULL_HANDLE, _pbufQuadIndices->mem, _pbufQuadIndices->buf);
      delete _pbufQuadIndices;
    }
    _pbufQuadIndices = new VlkBuffer;
    MakeBuffer(ctNew*sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, _pbufQuadIndices->buf, _pbufQuadIndices->mem, &_pbufQuadIndices->pub);
    uint32_t *pul = (uint32_t *)_pbufQuadIndices->pub;
    for (int iQuad = 0; iQuad < ctNew/6; iQuad++) {
      const uint32_t ul = iQuad*4;
      pul[iQuad*6+0] = ul+0;  pul[iQuad*6+1] = ul+1;  pul[iQuad*6+2] = ul+2;
      pul[iQuad*6+3] = ul+0;  pul[iQuad*6+4] = ul+2;  pul[iQuad*6+5] = ul+3;
    }
    _ctQuadIndices = ctNew;
  }
  vkCmdBindIndexBuffer(_cmd, _pbufQuadIndices->buf, 0, VK_INDEX_TYPE_UINT32);
  vkCmdDrawIndexed(_cmd, ctIndices, 1, 0, 0, 0);
}

static void vlkDrawPrimitives(GpuPrimitive ePrimitive, int ctVertices)
{
  const Topology eTopology = ePrimitive == GPUP_LINES ? TOP_LINES : ePrimitive == GPUP_POINTS ? TOP_POINTS : TOP_TRIANGLES;
  if (ctVertices <= 0 || !PrepareDraw(eTopology)) {
    return;
  }
  vkCmdDraw(_cmd, ctVertices, 1, 0, 0);
}

static void vlkClear(int iX, int iY, int iW, int iH, bool bColor, const float afColor[4], bool bDepth, float fDepth)
{
  if (!bColor && !bDepth) {
    return;
  }
  if (!BeginPass()) {
    return;
  }
  int iX0 = iX < 0 ? 0 : iX, iY0 = iY < 0 ? 0 : iY;
  int iX1 = iX + iW > _iTargetW ? _iTargetW : iX + iW, iY1 = iY + iH > _iTargetH ? _iTargetH : iY + iH;
  if (iX1 <= iX0 || iY1 <= iY0) {
    return;
  }
  VkClearAttachment aca[2];
  uint32_t ct = 0;
  if (bColor) {
    aca[ct] = {VK_IMAGE_ASPECT_COLOR_BIT, 0, {}};
    memcpy(aca[ct].clearValue.color.float32, afColor, 4*sizeof(float));
    ct++;
  }
  if (bDepth) {
    aca[ct] = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, {}};
    aca[ct].clearValue.depthStencil = {fDepth, 0};
    ct++;
  }
  VkClearRect cr = {{{iX0, iY0}, {(uint32_t)(iX1 - iX0), (uint32_t)(iY1 - iY0)}}, 0, 1};
  vkCmdClearAttachments(_cmd, ct, aca, 1, &cr);
}


// TEXTURES

static GpuTexture *vlkCreateTexture(void)
{
  VlkTexture *ptex = new VlkTexture;
  _setTextures.insert(ptex);
  return ptex;
}

static void vlkDeleteTexture(GpuTexture *ptexGpu)
{
  VlkTexture *ptex = static_cast<VlkTexture *>(ptexGpu);
  if (_ptexTarget == ptex || _ptexPass == ptex) {
    vlkSetRenderTarget(NULL);
    EndPass();
  }
  for (int iUnit = 0; iUnit < GPU_MAXTEXUNITS; iUnit++) {
    if (_aunit[iUnit].ptex == ptex) _aunit[iUnit].ptex = NULL;
  }
  for (SavedState &sav : _asavStates) {
    for (int iUnit = 0; iUnit < GPU_MAXTEXUNITS; iUnit++) {
      if (sav.aunit[iUnit].ptex == ptex) sav.aunit[iUnit].ptex = NULL;
    }
  }
  DiscardTexture(ptex);
  _setTextures.erase(ptex);
  delete ptex;
}

static VkFormat Format(GpuTextureFormat eFormat)
{
  switch (eFormat) {
  case GPUTF_DXT1: return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
  case GPUTF_DXT3: return VK_FORMAT_BC2_UNORM_BLOCK;
  case GPUTF_DXT5: return VK_FORMAT_BC3_UNORM_BLOCK;
  default:         return VK_FORMAT_R8G8B8A8_UNORM;
  }
}

// new storage for the texture, to be uploaded in the upload command buffer
static void NewStorage(VlkTexture *ptex)
{
  DiscardTexture(ptex);
  if (!MakeImage(ptex->iW, ptex->iH, ptex->ctMips, Format(ptex->eFormat), VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
    VK_IMAGE_ASPECT_COLOR_BIT, ptex->img, ptex->mem, ptex->view)) {
    return;
  }
  Barrier(UploadCommands(), ptex->img, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
  _setUploading.insert(ptex->img);
  ptex->bTarget = false;
  ptex->ulLastUse = 0;
}

static void vlkTextureStorage(GpuTexture *ptexGpu, int iW, int iH, int ctMips, GpuTextureFormat eFormat)
{
  VlkTexture *ptex = static_cast<VlkTexture *>(ptexGpu);
  if (vlkTextureHasStorage(ptex, iW, iH, ctMips, eFormat)) {
    return;
  }
  if (_ptexTarget == ptex) {
    vlkSetRenderTarget(NULL);
  }
  ptex->iW = iW;  ptex->iH = iH;
  ptex->ctMips = ctMips;
  ptex->eFormat = eFormat;
  NewStorage(ptex);
}

static bool vlkTextureHasStorage(VlkTexture *ptex, int iW, int iH, int ctMips, GpuTextureFormat eFormat)
{
  return ptex->img != VK_NULL_HANDLE && !ptex->bTarget && ptex->iW == iW && ptex->iH == iH && ptex->ctMips == ctMips
      && ptex->eFormat == eFormat;
}

// copy a mipmap's data into the texture, in the upload command buffer
static void UploadMip(VlkTexture *ptex, int iMip, int iW, int iH, const void *pvData, size_t slSize)
{
  // drawn with before: the draws keep the old storage (the other mipmaps come after this one)
  if (InUse(ptex->ulLastUse)) {
    NewStorage(ptex);
    if (ptex->img == VK_NULL_HANDLE) {
      return;
    }
  }
  VkBuffer buf;
  size_t slOffset;
  Upload(pvData, slSize, buf, slOffset);
  VkCommandBuffer cmd = UploadCommands();
  if (_setUploading.count(ptex->img) == 0) {
    Barrier(cmd, ptex->img, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    _setUploading.insert(ptex->img);
  }
  VkBufferImageCopy bic = {};
  bic.bufferOffset = slOffset;
  bic.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, (uint32_t)iMip, 0, 1};
  bic.imageExtent = {(uint32_t)iW, (uint32_t)iH, 1};
  vkCmdCopyBufferToImage(cmd, buf, ptex->img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bic);
  // much given buffers of their own (as when a world loads): let the GPU have it
  if (_slStaged > 64*1024*1024) {
    Flush(false);
  }
}

static void vlkTextureUpload(GpuTexture *ptexGpu, int iMip, int iW, int iH, const uint32_t *pulRGBA)
{
  VlkTexture *ptex = static_cast<VlkTexture *>(ptexGpu);
  if (ptex->img == VK_NULL_HANDLE || ptex->bTarget || iMip >= ptex->ctMips) {
    return;
  }
  // what OpenGL makes of the formats without color or alpha (32-bit RGBA in memory, R first)
  const uint32_t *pulData = pulRGBA;
  std::vector<uint32_t> aulConverted;
  const GpuTextureFormat eFormat = ptex->eFormat;
  if (eFormat == GPUTF_RGB8 || eFormat == GPUTF_RGB5 || eFormat == GPUTF_L8 || eFormat == GPUTF_A8 || eFormat == GPUTF_LA8) {
    const size_t ctPixels = (size_t)iW*iH;
    aulConverted.resize(ctPixels);
    for (size_t i = 0; i < ctPixels; i++) {
      const uint32_t ul = pulRGBA[i];
      const uint32_t ulR = ul & 0xFF, ulA = ul & 0xFF000000;
      switch (eFormat) {
      case GPUTF_L8:  aulConverted[i] = ulR | (ulR << 8) | (ulR << 16) | 0xFF000000;  break;
      case GPUTF_LA8: aulConverted[i] = ulR | (ulR << 8) | (ulR << 16) | ulA;  break;
      case GPUTF_A8:  aulConverted[i] = 0x00FFFFFF | ulA;  break;
      default:        aulConverted[i] = ul | 0xFF000000;  break;
      }
    }
    pulData = aulConverted.data();
  }
  UploadMip(ptex, iMip, iW, iH, pulData, (size_t)iW*iH*4);
}

static void vlkTextureUploadCompressed(GpuTexture *ptexGpu, int iMip, int iW, int iH, const void *pvData, size_t slSize)
{
  VlkTexture *ptex = static_cast<VlkTexture *>(ptexGpu);
  if (ptex->img == VK_NULL_HANDLE || ptex->bTarget || iMip >= ptex->ctMips) {
    return;
  }
  const int ctBlocksW = (iW + 3)/4, ctBlocksH = (iH + 3)/4;
  const size_t slBlock = ptex->eFormat == GPUTF_DXT1 ? 8 : 16;
  if (slSize < ctBlocksW*ctBlocksH*slBlock) {
    return;
  }
  UploadMip(ptex, iMip, iW, iH, pvData, ctBlocksW*ctBlocksH*slBlock);
}

static int vlkTextureSize(GpuTexture *ptexGpu)
{
  const VlkTexture *ptex = static_cast<VlkTexture *>(ptexGpu);
  const bool bDXT = ptex->eFormat == GPUTF_DXT1 || ptex->eFormat == GPUTF_DXT3 || ptex->eFormat == GPUTF_DXT5;
  int slSize = 0;
  for (int iMip = 0; iMip < ptex->ctMips; iMip++) {
    int iW = ptex->iW >> iMip, iH = ptex->iH >> iMip;
    if (iW < 1) iW = 1;
    if (iH < 1) iH = 1;
    if (bDXT) {
      slSize += ((iW + 3)/4)*((iH + 3)/4)*(ptex->eFormat == GPUTF_DXT1 ? 8 : 16);
    } else {
      slSize += iW*iH*4;
    }
  }
  return slSize;
}


// BUFFERS

static GpuBuffer *vlkCreateBuffer(size_t slSize)
{
  VlkBuffer *pbuf = new VlkBuffer;
  pbuf->slSize = slSize;
  if (_dev != VK_NULL_HANDLE) {
    MakeBuffer(slSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, pbuf->buf, pbuf->mem, &pbuf->pub);
  }
  _setBuffers.insert(pbuf);
  return pbuf;
}

static void vlkDeleteBuffer(GpuBuffer *pbufGpu)
{
  VlkBuffer *pbuf = static_cast<VlkBuffer *>(pbufGpu);
  for (int iStream = 0; iStream < GPUS_COUNT; iStream++) {
    if (_astr[iStream].pbuf == pbuf) _astr[iStream] = Stream();
  }
  for (SavedState &sav : _asavStates) {
    for (int iStream = 0; iStream < GPUS_COUNT; iStream++) {
      if (sav.astr[iStream].pbuf == pbuf) sav.astr[iStream] = Stream();
    }
  }
  Discard(VK_NULL_HANDLE, VK_NULL_HANDLE, pbuf->mem, pbuf->buf);
  _setBuffers.erase(pbuf);
  delete pbuf;
}

static void vlkBufferUpdate(GpuBuffer *pbufGpu, size_t slOffset, const void *pvData, size_t slSize)
{
  VlkBuffer *pbuf = static_cast<VlkBuffer *>(pbufGpu);
  if (pbuf->pub == NULL) {
    return;
  }
  // drawn with before: the draws keep the old storage, the new one starts as a copy of it
  if (InUse(pbuf->ulLastUse)) {
    VkBuffer buf;
    VkDeviceMemory mem;
    uint8_t *pub;
    if (MakeBuffer(pbuf->slSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, buf, mem, &pub)) {
      memcpy(pub, pbuf->pub, pbuf->slSize);
      Discard(VK_NULL_HANDLE, VK_NULL_HANDLE, pbuf->mem, pbuf->buf);
      pbuf->buf = buf;
      pbuf->mem = mem;
      pbuf->pub = pub;
      pbuf->ulLastUse = 0;
    }
  }
  memcpy(pbuf->pub + slOffset, pvData, slSize);
}


// PROGRAMS
// GLSL compiled into SPIR-V by glslang (kept by source, so a device made again does not compile
// them again)

static bool CompileGLSL(bool bVertex, const char *strSource, std::vector<uint32_t> &aulSPIRV, char *strError, size_t slErrorSize)
{
  auto it = _mapSPIRV.find(strSource);
  if (it != _mapSPIRV.end()) {
    aulSPIRV = it->second;
    return true;
  }
  glslang_input_t in = {};
  in.language = GLSLANG_SOURCE_GLSL;
  in.stage = bVertex ? GLSLANG_STAGE_VERTEX : GLSLANG_STAGE_FRAGMENT;
  in.client = GLSLANG_CLIENT_VULKAN;
  // (SPIR-V 1.5: in 1.6 discard needs the demote to helper invocation feature)
  in.client_version = GLSLANG_TARGET_VULKAN_1_2;
  in.target_language = GLSLANG_TARGET_SPV;
  in.target_language_version = GLSLANG_TARGET_SPV_1_5;
  in.code = strSource;
  in.default_version = 450;
  in.default_profile = GLSLANG_NO_PROFILE;
  in.messages = (glslang_messages_t)(GLSLANG_MSG_DEFAULT_BIT | GLSLANG_MSG_SPV_RULES_BIT | GLSLANG_MSG_VULKAN_RULES_BIT);
  in.resource = glslang_default_resource();
  glslang_shader_t *psh = glslang_shader_create(&in);
  bool bOK = glslang_shader_preprocess(psh, &in) && glslang_shader_parse(psh, &in);
  if (!bOK) {
    snprintf(strError, slErrorSize, "%s", glslang_shader_get_info_log(psh));
    glslang_shader_delete(psh);
    return false;
  }
  glslang_program_t *ppr = glslang_program_create();
  glslang_program_add_shader(ppr, psh);
  bOK = glslang_program_link(ppr, GLSLANG_MSG_SPV_RULES_BIT | GLSLANG_MSG_VULKAN_RULES_BIT);
  if (bOK) {
    glslang_program_SPIRV_generate(ppr, in.stage);
    aulSPIRV.resize(glslang_program_SPIRV_get_size(ppr));
    glslang_program_SPIRV_get(ppr, aulSPIRV.data());
    _mapSPIRV[strSource] = aulSPIRV;
  } else {
    snprintf(strError, slErrorSize, "%s", glslang_program_get_info_log(ppr));
  }
  glslang_program_delete(ppr);
  glslang_shader_delete(psh);
  return bOK;
}

static VlkProgram *MakeProgram(bool bVertex, const char *strSource, char *strError, size_t slErrorSize)
{
  if (strError != NULL && slErrorSize > 0) {
    strError[0] = 0;
  }
  std::vector<uint32_t> aulSPIRV;
  if (!CompileGLSL(bVertex, strSource, aulSPIRV, strError, slErrorSize)) {
    return NULL;
  }
  VkShaderModuleCreateInfo smci = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  smci.codeSize = aulSPIRV.size()*sizeof(uint32_t);
  smci.pCode = aulSPIRV.data();
  VlkProgram *ppr = new VlkProgram;
  if (vkCreateShaderModule(_dev, &smci, NULL, &ppr->mod) != VK_SUCCESS) {
    snprintf(strError, slErrorSize, "cannot make the shader module");
    delete ppr;
    return NULL;
  }
  ppr->bVertex = bVertex;
  ppr->ulID = ++_ulLastProgramID;
  return ppr;
}

static void vlkDeleteProgram(GpuProgram *pprGpu)
{
  VlkProgram *ppr = static_cast<VlkProgram *>(pprGpu);
  if (_st.pprVertex == ppr) _st.pprVertex = NULL;
  if (_st.pprFragment == ppr) _st.pprFragment = NULL;
  // (the pipelines made with it have their own copy)
  if (ppr->mod != VK_NULL_HANDLE) vkDestroyShaderModule(_dev, ppr->mod, NULL);
  delete ppr;
}

// the fixed function emulation (constants: GpuFFVertexConst and GpuFFFragmentConst), with the
// stage interface of the programs (ShaderARBToGLSL.h), so either can go with the other; as the
// Metal device's
static const char *_strFixedFunctionVS = GLSL_VERTEX_INTERFACE R"GLSL(
layout(set = 0, binding = 0, std140) uniform FFVertexConst {
  mat4 mProjection;
  mat4 mView;
  mat4 amTexture[4];
  vec4 vClipPlane;     // in view space
  vec4 colConstant;    // without color array
  uint ulFlags;        // 1=color array, 2=clip plane
  uint ulTexCoords;    // 4 bits per texture unit: 1=array, 2=s,t,r,q, 4=texture matrix,
                       // 8=the position in view space (Direct3D's D3DTSS_TCI_CAMERASPACEPOSITION)
  float fPointSize;
  uint ulPad;
} c;

vec4 TexCoord(vec4 vArray, uint ulMode, mat4 mTexture, vec4 vView)
{
  vec4 v = vec4(0.0f, 0.0f, 0.0f, 1.0f);
  if ((ulMode & 8u) != 0u) {
    v = vView;
  } else if ((ulMode & 1u) != 0u) {
    v = vArray;
  }
  if ((ulMode & 4u) != 0u) v = mTexture * v;
  return v;
}

void main()
{
  const uint vid = uint(gl_VertexIndex);
  const vec4 vView = c.mView * vec4(GpuPos(vid), 1.0f);
  gl_Position = c.mProjection * vView;
  o.col0 = (c.ulFlags & 1u) != 0u ? GpuCol(vid) : c.colConstant;
  o.col1 = vec4(0.0f);
  const uint m0 = (c.ulTexCoords >>  0) & 15u, m1 = (c.ulTexCoords >>  4) & 15u;
  const uint m2 = (c.ulTexCoords >>  8) & 15u, m3 = (c.ulTexCoords >> 12) & 15u;
  const vec4 vNone = vec4(0.0f, 0.0f, 0.0f, 1.0f);
  o.tex0 = TexCoord((m0 & 1u) != 0u ? GpuTex0(vid, (m0 & 2u) != 0u) : vNone, m0, c.amTexture[0], vView);
  o.tex1 = TexCoord((m1 & 1u) != 0u ? GpuTex1(vid, (m1 & 2u) != 0u) : vNone, m1, c.amTexture[1], vView);
  o.tex2 = TexCoord((m2 & 1u) != 0u ? GpuTex2(vid, (m2 & 2u) != 0u) : vNone, m2, c.amTexture[2], vView);
  o.tex3 = TexCoord((m3 & 1u) != 0u ? GpuTex3(vid, (m3 & 2u) != 0u) : vNone, m3, c.amTexture[3], vView);
  gl_PointSize = c.fPointSize;
  gl_ClipDistance[0] = (c.ulFlags & 2u) != 0u ? dot(vView, c.vClipPlane) : 1.0f;
}
)GLSL";

static const char *_strFixedFunctionFS = GLSL_FRAGMENT_INTERFACE R"GLSL(
layout(set = 0, binding = 1, std140) uniform FFFragmentConst {
  uvec4 aulTexMode;    // per texture unit: 0=off, 1=modulate, 2=modulate 2x, 3=replace, 4=interpolate
  float fAlphaRef;
  uint bAlphaTest;
  float fLODBias;
  float fFactor;       // for 4
} c;

// as GL_MODULATE, GL_COMBINE with GL_MODULATE and GL_RGB_SCALE 2 (results clamped), GL_REPLACE,
// or GL_COMBINE with GL_INTERPOLATE of the previous color and the texture by the constant (the
// terrain's shadow map blend, TRRender.cpp)
vec4 Stage(vec4 col, vec4 tex, uint ulMode, float fFactor)
{
  if (ulMode == 2u) return vec4(clamp(col.rgb*tex.rgb*2.0f, 0.0f, 1.0f), col.a*tex.a);
  if (ulMode == 3u) return tex;
  if (ulMode == 4u) return vec4(mix(tex.rgb, col.rgb, fFactor), col.a);
  return col*tex;
}

void main()
{
  vec4 col = vin.col0;
  if (c.aulTexMode.x != 0u) col = Stage(col, texture(tex0, vin.tex0.xy/vin.tex0.w, c.fLODBias), c.aulTexMode.x, c.fFactor);
  if (c.aulTexMode.y != 0u) col = Stage(col, texture(tex1, vin.tex1.xy/vin.tex1.w, c.fLODBias), c.aulTexMode.y, c.fFactor);
  if (c.aulTexMode.z != 0u) col = Stage(col, texture(tex2, vin.tex2.xy/vin.tex2.w, c.fLODBias), c.aulTexMode.z, c.fFactor);
  if (c.aulTexMode.w != 0u) col = Stage(col, texture(tex3, vin.tex3.xy/vin.tex3.w, c.fLODBias), c.aulTexMode.w, c.fFactor);
  if (c.bAlphaTest != 0u && col.a < c.fAlphaRef) discard;
  gpu_FragColor = col;
}
)GLSL";

static GpuProgram *vlkCreateFixedFunction(bool bVertex, char *strError, size_t slErrorSize)
{
  return MakeProgram(bVertex, bVertex ? _strFixedFunctionVS : _strFixedFunctionFS, strError, slErrorSize);
}

// the programs from ShaderARBTranslate.cpp
static GpuProgram *vlkCreateProgram(bool bVertex, const char *strSource, char *strError, size_t slErrorSize)
{
  return MakeProgram(bVertex, strSource, strError, slErrorSize);
}


static const GpuDevice _gpuVulkan = {
  "Vulkan",
  vlkInit, vlkEnd, vlkDeviceName, vlkHasDXT, vlkSetDrawableScale, vlkSetVSync, vlkPresent, vlkFinish,
  vlkSetViewport, vlkSetScissor, vlkSetBlend, vlkSetColorMask, vlkSetDepth, vlkSetCull, vlkSetWireframe,
  vlkSetDepthBias, vlkSetProgram, vlkSetTexture, vlkSetTextureNone, vlkSetTextureWrap,
  vlkSetStreamData, vlkSetStreamBuffer, vlkClearStream, vlkGetStreamMask, vlkSetVertexConstants,
  vlkSetFragmentConstants, vlkDrawIndexed, vlkDrawQuads, vlkDrawPrimitives, vlkClear,
  vlkCreateTexture, vlkDeleteTexture, vlkTextureStorage, vlkTextureUpload, vlkTextureUploadCompressed,
  vlkTextureSize,
  vlkTextureRenderTarget, vlkSetRenderTarget, vlkGetRenderTarget, vlkCopyFrameToTexture,
  vlkReadFrameRGB, vlkReadFrameDepths, vlkPushState, vlkPopState,
  vlkCreateBuffer, vlkDeleteBuffer, vlkBufferUpdate,
  vlkCreateFixedFunction, TranslateARBToGLSL, vlkCreateProgram, vlkDeleteProgram,
};

const GpuDevice *vlkDevice(void)
{
  return &_gpuVulkan;
}
