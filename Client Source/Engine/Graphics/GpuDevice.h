#ifndef SE_INCL_GPUDEVICE_H
#define SE_INCL_GPUDEVICE_H
#ifdef PRAGMA_ONCE
  #pragma once
#endif

// The device under the engine's backends without fixed function (Metal, GAT_MTL; Vulkan): the
// gfx* functions for them are in Gfx_wrapper_Gpu.cpp and the library parts in Gfx_Gpu.cpp, the
// same for every such API; what differs is the device, a table of functions (GpuDevice below)
// that each API fills (MetalDevice.mm). The devices do not see the engine's headers (Objective-C
// has its own BOOL), so this interface uses plain C++ types only.
//
// A device keeps the render state the engine sets and puts it into its command stream when
// something is drawn. A frame starts with the first thing drawn after Present(); everything
// drawn runs on the GPU when the frame is presented.
//
// Coordinates are in pixels of the render target, with the origin at the top left (like
// Direct3D). Projections give depth in 0..1 (like Direct3D).

#include <stddef.h>
#include <stdint.h>
#include <string>

struct SDL_Window;

// blend factors (the order of the engine's GfxBlend)
enum GpuBlend {
  GPUB_ONE = 0, GPUB_ZERO, GPUB_SRC_COLOR, GPUB_INV_SRC_COLOR, GPUB_DST_COLOR, GPUB_INV_DST_COLOR,
  GPUB_SRC_ALPHA, GPUB_INV_SRC_ALPHA, GPUB_DST_ALPHA, GPUB_INV_DST_ALPHA,
};
// compare functions (the order of the engine's GfxComp)
enum GpuCompare {
  GPUC_NEVER = 0, GPUC_LESS, GPUC_LESS_EQUAL, GPUC_EQUAL, GPUC_NOT_EQUAL, GPUC_GREATER_EQUAL,
  GPUC_GREATER, GPUC_ALWAYS,
};
enum GpuCull { GPUCULL_NONE = 0, GPUCULL_FRONT, GPUCULL_BACK };

// texture formats the engine asks for (TS.ts_tf* under these APIs); the uncompressed are kept as
// 8-bit RGBA, the ones without color or alpha filled in as OpenGL reads them; and the DXT
// compressed formats
enum GpuTextureFormat {
  GPUTF_NONE = 0, GPUTF_RGBA8, GPUTF_RGB8, GPUTF_RGB5, GPUTF_RGBA4, GPUTF_RGB5A1,
  GPUTF_LA8, GPUTF_L8, GPUTF_A8,
  GPUTF_DXT1, GPUTF_DXT3, GPUTF_DXT5,
};

// sampling of a texture unit
struct GpuSampler {
  uint8_t ubMagLinear;     // 0=nearest, 1=linear
  uint8_t ubMinLinear;
  uint8_t ubMip;           // 0=no mipmaps, 1=nearest, 2=linear
  uint8_t ubClampU;        // 0=repeat, 1=clamp to edge
  uint8_t ubClampV;
  uint8_t ubAnisotropy;    // 1..16
};

// vertex streams; the shaders read them by vertex index
enum GpuStream {
  GPUS_POSITION = 0,   // float x,y,z
  GPUS_COLOR,          // UBYTE r,g,b,a
  GPUS_NORMAL,         // float x,y,z
  GPUS_TEXCOORD0,      // float s,t (or s,t,r,q)
  GPUS_TEXCOORD1,
  GPUS_TEXCOORD2,
  GPUS_TEXCOORD3,
  GPUS_WEIGHT,         // UBYTE indices[4], weights[4]
  GPUS_TANGENT,        // float x,y,z,w
  GPUS_COUNT,
};
#define GPU_MAXTEXUNITS 4

enum GpuPrimitive { GPUP_TRIANGLES = 0, GPUP_LINES, GPUP_POINTS };

// a texture object (the engine keeps its address as TEXOBJ), vertex data that stays on the GPU,
// and a compiled shader function; each device derives its own from them
struct GpuTexture {};
struct GpuBuffer {};
struct GpuProgram {};


// THE PROGRAMS' CONSTANTS
// The layouts the device's shaders read (the same in C++ and in the shading language): those of
// the vertex and pixel programs (Direct3D's assembly through ARB, ShaderAsmToARB.h), and those of
// the fixed function emulation, which has the same interface between the stages, so either goes
// with the other.

#define GPU_VERTEX_CONSTANTS 96
#define GPU_PIXEL_CONSTANTS  16   // c0..c7 and the texture stage values (ShaderAsmToARB.h)

struct GpuVPConst {
  float afConst[GPU_VERTEX_CONSTANTS][4];
  float afColor[4];         // the constant color, for vertex.color without a color array
  uint32_t ulStreams;       // bit per GpuStream that has data
  uint32_t ulTexCoords;     // 4 bits per texture unit: 2=s,t,r,q (else s,t)
  uint32_t aulPad[2];
};

struct GpuPPConst {
  float afConst[GPU_PIXEL_CONSTANTS][4];
  float fAlphaRef;
  uint32_t bAlphaTest;
  float fLODBias;
  uint32_t ulPad;
};

// the fixed function emulation: what OpenGL's fixed function does here (vertex arrays, projection
// and view matrices, texture matrices and units with MODULATE or 2X combine, constant color or
// color array, alpha test, user clip plane, point size)
struct GpuFFVertexConst {
  float afProjection[16];
  float afView[16];
  float aafTexture[4][16];
  float afClipPlane[4];      // in view space
  float afColor[4];          // without color array
  uint32_t ulFlags;          // 1=color array, 2=clip plane
  uint32_t ulTexCoords;      // 4 bits per texture unit: 1=array, 2=s,t,r,q, 4=texture matrix,
                             // 8=the position in view space (Direct3D's D3DTSS_TCI_CAMERASPACEPOSITION)
  float fPointSize;
  uint32_t ulPad;
};

struct GpuFFFragmentConst {
  uint32_t aulTexMode[4];    // per texture unit: 0=off, 1=modulate, 2=modulate 2x, 3=replace, 4=interpolate
  float fAlphaRef;
  uint32_t bAlphaTest;
  float fLODBias;
  float fFactor;             // for 4
};


// THE DEVICE

struct GpuDevice {
  const char *strAPI;   // "Metal"

  // start on the window (pixels drawn per window point: 1, or the display's density on Retina)
  bool (*Init)(SDL_Window *pwnd, int iScale);
  void (*End)(void);
  const char *(*DeviceName)(void);
  // whether it reads DXT (BC1..3) textures
  bool (*HasDXT)(void);
  // pixels drawn per window point (window resized or moved, Retina density changed)
  void (*SetDrawableScale)(int iScale);
  void (*SetVSync)(bool bWait);
  // end the frame: present it and let the GPU run it
  void (*Present)(void);
  // wait until the GPU has run everything given to it so far
  void (*Finish)(void);

  // render state
  void (*SetViewport)(int iX, int iY, int iW, int iH, float fMinZ, float fMaxZ);
  void (*SetScissor)(int iX, int iY, int iW, int iH);
  void (*SetBlend)(bool bBlend, GpuBlend eSrc, GpuBlend eDst);
  void (*SetColorMask)(bool bR, bool bG, bool bB, bool bA);
  void (*SetDepth)(bool bTest, bool bWrite, GpuCompare eFunc);
  void (*SetCull)(GpuCull eCull, bool bFrontCCW);
  void (*SetWireframe)(bool bWireframe);
  void (*SetDepthBias)(bool bBias);
  // shader functions to draw with (the fixed function emulation is a program too)
  void (*SetProgram)(GpuProgram *pprVertex, GpuProgram *pprFragment);
  void (*SetTexture)(int iUnit, GpuTexture *ptex, const GpuSampler &smp);
  void (*SetTextureNone)(int iUnit);
  // wrapping of the texture set on a unit (the sampling is the unit's, as in Direct3D)
  void (*SetTextureWrap)(int iUnit, bool bClampU, bool bClampV);

  // vertex data: an array in memory (copied when it is drawn, until set again) or a GPU buffer
  void (*SetStreamData)(GpuStream eStream, const void *pvData, size_t slSize);
  void (*SetStreamBuffer)(GpuStream eStream, GpuBuffer *pbuf, size_t slOffset);
  void (*ClearStream)(GpuStream eStream);
  // streams that have data (bit per GpuStream)
  uint32_t (*GetStreamMask)(void);
  // constants for the shaders of the next draws (copied now)
  void (*SetVertexConstants)(const void *pvData, size_t slSize);
  void (*SetFragmentConstants)(const void *pvData, size_t slSize);
  // draw triangles by 16-bit indices, quads of 4 vertices, or vertices in order
  void (*DrawIndexed)(int ctIndices, const uint16_t *puwIndices);
  void (*DrawQuads)(int ctVertices);
  void (*DrawPrimitives)(GpuPrimitive ePrimitive, int ctVertices);
  // fill a rectangle of the render target with a color and/or a depth
  void (*Clear)(int iX, int iY, int iW, int iH, bool bColor, const float afColor[4], bool bDepth, float fDepth);

  // textures (one mipmap at a time, from 32-bit RGBA)
  GpuTexture *(*CreateTexture)(void);
  void (*DeleteTexture)(GpuTexture *ptex);
  // start (or restart) a texture's storage: size of mipmap 0, number of mipmaps, format
  void (*TextureStorage)(GpuTexture *ptex, int iW, int iH, int ctMips, GpuTextureFormat eFormat);
  void (*TextureUpload)(GpuTexture *ptex, int iMip, int iW, int iH, const uint32_t *pulRGBA);
  // a mipmap of a DXT texture (the blocks as they are in the file)
  void (*TextureUploadCompressed)(GpuTexture *ptex, int iMip, int iW, int iH, const void *pvData, size_t slSize);
  int  (*TextureSize)(GpuTexture *ptex);   // bytes

  // render targets: a texture to draw into instead of the window (BGRA, with a depth buffer of its
  // own; drawn from the top left, as in Direct3D)
  void (*TextureRenderTarget)(GpuTexture *ptex, int iW, int iH);
  // where the next draws go (NULL: the window), and where they go now
  void (*SetRenderTarget)(GpuTexture *ptex);
  GpuTexture *(*GetRenderTarget)(void);
  // copy a rectangle of the window's frame into a texture (made BGRA of that size; a GPU copy)
  void (*CopyFrameToTexture)(GpuTexture *ptex, int iX, int iY, int iW, int iH);
  // read the window's frame as it is now (they wait for the GPU): a rectangle as 8-bit R,G,B rows
  // from the top, and depths at points (x,y pairs); false if there is no frame
  bool (*ReadFrameRGB)(int iX, int iY, int iW, int iH, uint8_t *pubRGB);
  bool (*ReadFrameDepths)(const int *piXY, int ctPoints, float *pfDepths);
  // keep the state of the draws (programs, render state, streams, textures, constants) and put it back
  void (*PushState)(void);
  void (*PopState)(void);

  // GPU buffers for vertex data
  GpuBuffer *(*CreateBuffer)(size_t slSize);
  void (*DeleteBuffer)(GpuBuffer *pbuf);
  void (*BufferUpdate)(GpuBuffer *pbuf, size_t slOffset, const void *pvData, size_t slSize);

  // programs: the fixed function emulation's stages, and the ARB programs that ShaderAsmToARB.cpp
  // makes, translated into the device's shading language and compiled (NULL, or false, and the
  // reason if it cannot be done)
  GpuProgram *(*CreateFixedFunction)(bool bVertex, char *strError, size_t slErrorSize);
  bool (*TranslateProgram)(const std::string &strARB, std::string &strSource, std::string &strError);
  GpuProgram *(*CreateProgram)(bool bVertex, const char *strSource, char *strError, size_t slErrorSize);
  void (*DeleteProgram)(GpuProgram *ppr);
};

// the device of the current API, in the engine (Gfx_Gpu.cpp sets it; NULL with the others)
extern const GpuDevice *_pgpu;

#endif  /* include-once check. */
