#ifndef SE_INCL_METALDEVICE_H
#define SE_INCL_METALDEVICE_H
#ifdef PRAGMA_ONCE
  #pragma once
#endif

// The Metal device under the engine's Metal backend (GAT_MTL; the gfx* functions are in
// Gfx_wrapper_Metal.cpp, the library parts in Gfx_Metal.cpp). It is Objective-C++
// (MetalDevice.mm) and does not see the engine's headers (Objective-C has its own BOOL), so this
// interface uses plain C++ types only.
//
// The device keeps the render state the engine sets and puts it into the command encoder when
// something is drawn. A frame starts with the first thing drawn after mtlPresent(); everything
// drawn is encoded and runs on the GPU when the frame is presented.
//
// Coordinates are in pixels of the render target, with the origin at the top left (like
// Direct3D). Projections give depth in 0..1 (like Direct3D).

#include <stddef.h>
#include <stdint.h>

struct SDL_Window;

// blend factors (the order of the engine's GfxBlend)
enum MtlBlend {
  MTLB_ONE = 0, MTLB_ZERO, MTLB_SRC_COLOR, MTLB_INV_SRC_COLOR, MTLB_DST_COLOR, MTLB_INV_DST_COLOR,
  MTLB_SRC_ALPHA, MTLB_INV_SRC_ALPHA, MTLB_DST_ALPHA, MTLB_INV_DST_ALPHA,
};
// compare functions (the order of the engine's GfxComp)
enum MtlCompare {
  MTLC_NEVER = 0, MTLC_LESS, MTLC_LESS_EQUAL, MTLC_EQUAL, MTLC_NOT_EQUAL, MTLC_GREATER_EQUAL,
  MTLC_GREATER, MTLC_ALWAYS,
};
enum MtlCull { MTLCULL_NONE = 0, MTLCULL_FRONT, MTLCULL_BACK };

// texture formats the engine asks for (TS.ts_tf* under Metal); the uncompressed are kept as 8-bit
// RGBA, the // ones without color or alpha filled in as OpenGL reads them; and the DXT compressed formats
enum MtlTextureFormat {
  MTLTF_NONE = 0, MTLTF_RGBA8, MTLTF_RGB8, MTLTF_RGB5, MTLTF_RGBA4, MTLTF_RGB5A1,
  MTLTF_LA8, MTLTF_L8, MTLTF_A8,
  MTLTF_DXT1, MTLTF_DXT3, MTLTF_DXT5,
};

// sampling of a texture unit
struct MtlSampler {
  uint8_t ubMagLinear;     // 0=nearest, 1=linear
  uint8_t ubMinLinear;
  uint8_t ubMip;           // 0=no mipmaps, 1=nearest, 2=linear
  uint8_t ubClampU;        // 0=repeat, 1=clamp to edge
  uint8_t ubClampV;
  uint8_t ubAnisotropy;    // 1..16
};

// vertex streams; the shaders read them by vertex index (buffer index = stream + 1)
enum MtlStream {
  MTLS_POSITION = 0,   // float x,y,z
  MTLS_COLOR,          // UBYTE r,g,b,a
  MTLS_NORMAL,         // float x,y,z
  MTLS_TEXCOORD0,      // float s,t (or s,t,r,q)
  MTLS_TEXCOORD1,
  MTLS_TEXCOORD2,
  MTLS_TEXCOORD3,
  MTLS_WEIGHT,         // UBYTE indices[4], weights[4]
  MTLS_TANGENT,        // float x,y,z,w
  MTLS_COUNT,
};
#define MTL_MAXTEXUNITS 4

struct MtlTexture;   // a texture object (the engine keeps its address as TEXOBJ)
struct MtlBuffer;    // vertex data that stays on the GPU
struct MtlProgram;   // shader functions compiled from MSL

// whether the system has a Metal device, and whether it reads DXT (BC1..3) textures
bool mtlIsAvailable(void);
bool mtlHasDXT(void);
// start on the window (pixels drawn per window point: 1, or the display's density on Retina)
bool mtlInit(SDL_Window *pwnd, int iScale);
void mtlEnd(void);
const char *mtlDeviceName(void);
// pixels drawn per window point (window resized or moved, Retina density changed)
void mtlSetDrawableScale(int iScale);
void mtlSetVSync(bool bWait);
// end the frame: present it and let the GPU run it
void mtlPresent(void);
// wait until the GPU has run everything given to it so far
void mtlFinish(void);

// render state
void mtlSetViewport(int iX, int iY, int iW, int iH, float fMinZ, float fMaxZ);
void mtlSetScissor(int iX, int iY, int iW, int iH);
void mtlSetBlend(bool bBlend, MtlBlend eSrc, MtlBlend eDst);
void mtlSetColorMask(bool bR, bool bG, bool bB, bool bA);
void mtlSetDepth(bool bTest, bool bWrite, MtlCompare eFunc);
void mtlSetCull(MtlCull eCull, bool bFrontCCW);
void mtlSetWireframe(bool bWireframe);
void mtlSetDepthBias(bool bBias);
// shader functions to draw with (the engine's fixed function emulation is a program too)
void mtlSetProgram(MtlProgram *pprVertex, MtlProgram *pprFragment);
void mtlSetTexture(int iUnit, MtlTexture *ptex, const MtlSampler &smp);
void mtlSetTextureNone(int iUnit);
// wrapping of the texture set on a unit (the sampling is the unit's, as in Direct3D)
void mtlSetTextureWrap(int iUnit, bool bClampU, bool bClampV);

// vertex data: an array in memory (copied when it is drawn, until set again) or a GPU buffer
void mtlSetStreamData(MtlStream eStream, const void *pvData, size_t slSize);
void mtlSetStreamBuffer(MtlStream eStream, MtlBuffer *pbuf, size_t slOffset);
void mtlClearStream(MtlStream eStream);
// streams that have data (bit per MtlStream)
uint32_t mtlGetStreamMask(void);
// constants for the shaders of the next draws (copied now)
void mtlSetVertexConstants(const void *pvData, size_t slSize);
void mtlSetFragmentConstants(const void *pvData, size_t slSize);
// draw triangles by 16-bit indices, quads of 4 vertices, or vertices in order
enum MtlPrimitive { MTLP_TRIANGLES = 0, MTLP_LINES, MTLP_POINTS };
void mtlDrawIndexed(int ctIndices, const uint16_t *puwIndices);
void mtlDrawQuads(int ctVertices);
void mtlDrawPrimitives(MtlPrimitive ePrimitive, int ctVertices);
// fill a rectangle of the render target with a color and/or a depth
void mtlClear(int iX, int iY, int iW, int iH, bool bColor, const float afColor[4], bool bDepth, float fDepth);

// textures (one mipmap at a time, from 32-bit RGBA)
MtlTexture *mtlCreateTexture(void);
void mtlDeleteTexture(MtlTexture *ptex);
// start (or restart) a texture's storage: size of mipmap 0, number of mipmaps, format
void mtlTextureStorage(MtlTexture *ptex, int iW, int iH, int ctMips, MtlTextureFormat eFormat);
void mtlTextureUpload(MtlTexture *ptex, int iMip, int iW, int iH, const uint32_t *pulRGBA);
// a mipmap of a DXT texture (the blocks as they are in the file)
void mtlTextureUploadCompressed(MtlTexture *ptex, int iMip, int iW, int iH, const void *pvData, size_t slSize);
bool mtlTextureHasStorage(MtlTexture *ptex, int iW, int iH, int ctMips, MtlTextureFormat eFormat);
int  mtlTextureSize(MtlTexture *ptex);   // bytes
int  mtlTextureWidth(MtlTexture *ptex);
int  mtlTextureHeight(MtlTexture *ptex);

// render targets: a texture to draw into instead of the window (BGRA, with a depth buffer of its
// own; drawn from the top left, as in Direct3D)
void mtlTextureRenderTarget(MtlTexture *ptex, int iW, int iH);
// where the next draws go (NULL: the window), and where they go now
void mtlSetRenderTarget(MtlTexture *ptex);
MtlTexture *mtlGetRenderTarget(void);
// copy a rectangle of the window's frame into a texture (made BGRA of that size; a GPU copy)
void mtlCopyFrameToTexture(MtlTexture *ptex, int iX, int iY, int iW, int iH);
// read the window's frame as it is now (they wait for the GPU): a rectangle as 8-bit R,G,B rows
// from the top, and depths at points (x,y pairs); false if there is no frame
bool mtlReadFrameRGB(int iX, int iY, int iW, int iH, uint8_t *pubRGB);
bool mtlReadFrameDepths(const int *piXY, int ctPoints, float *pfDepths);
// keep the state of the draws (programs, render state, streams, textures, constants) and put it back
void mtlPushState(void);
void mtlPopState(void);

// GPU buffers for vertex data
MtlBuffer *mtlCreateBuffer(size_t slSize);
void mtlDeleteBuffer(MtlBuffer *pbuf);
void mtlBufferUpdate(MtlBuffer *pbuf, size_t slOffset, const void *pvData, size_t slSize);

// programs: a library of MSL source and one function of it (NULL and a message if it fails)
MtlProgram *mtlCreateProgram(const char *strSource, const char *strFunction, char *strError, size_t slErrorSize);
void mtlDeleteProgram(MtlProgram *ppr);

#endif  /* include-once check. */
