// Offline test: the same scenes drawn by the Metal and the Vulkan devices (GpuDevice), read back
// and compared pixel by pixel (build.sh). Usage: parity <programs dir> [-save <dir>]
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
#include <functional>
#include "MetalDevice.h"
#include "VulkanDevice.h"
#include "ShaderAsmToARB.h"

const GpuDevice *_pgpu = NULL;

static const int W = 256, H = 192;
typedef std::vector<uint8_t> Image;

struct Ctx {
  const GpuDevice *d;
  GpuProgram *pffv, *pfff;
  GpuTexture *ptexChecker, *ptexGrad, *ptexDXT, *ptexL8, *ptexMip;
  std::vector<GpuProgram *> aprVP, aprPP;
};

static GpuFFVertexConst FFV()
{
  GpuFFVertexConst c = {};
  for (int i = 0; i < 4; i++) { c.afProjection[i*5] = 1; c.afView[i*5] = 1; for (int t = 0; t < 4; t++) c.aafTexture[t][i*5] = 1; }
  c.afColor[0] = c.afColor[1] = c.afColor[2] = c.afColor[3] = 1;
  c.fPointSize = 1;
  return c;
}
static GpuFFFragmentConst FFF(uint32_t ulMode0 = 0)
{
  GpuFFFragmentConst c = {};
  c.aulTexMode[0] = ulMode0;
  c.fAlphaRef = 0.5f;
  return c;
}

static void Defaults(Ctx &x)
{
  const GpuDevice *d = x.d;
  d->SetRenderTarget(NULL);
  d->SetViewport(0, 0, W, H, 0, 1);
  d->SetScissor(0, 0, W, H);
  d->SetBlend(false, GPUB_ONE, GPUB_ZERO);
  d->SetColorMask(true, true, true, true);
  d->SetDepth(false, false, GPUC_LESS_EQUAL);
  d->SetCull(GPUCULL_NONE, true);
  d->SetWireframe(false);
  d->SetDepthBias(false);
  d->SetProgram(x.pffv, x.pfff);
  for (int i = 0; i < GPUS_COUNT; i++) d->ClearStream((GpuStream)i);
  for (int i = 0; i < 4; i++) d->SetTextureNone(i);
  GpuFFVertexConst vc = FFV();
  d->SetVertexConstants(&vc, sizeof(vc));
  GpuFFFragmentConst fc = FFF();
  d->SetFragmentConstants(&fc, sizeof(fc));
  const float afGrey[4] = {0.2f, 0.25f, 0.3f, 1};
  d->Clear(0, 0, W, H, true, afGrey, true, 1.0f);
}

static GpuSampler Smp(bool bLinear = true, bool bClamp = false, int iMip = 0)
{
  GpuSampler s = {};
  s.ubMagLinear = s.ubMinLinear = bLinear;
  s.ubMip = iMip;
  s.ubClampU = s.ubClampV = bClamp;
  s.ubAnisotropy = 1;
  return s;
}

// a quad from (x0,y0) to (x1,y1) in clip space, z, with texcoords 0..1 (v from the top)
static float _afQuadPos[64][3], _afQuadTex[64][2];
static uint8_t _aubQuadCol[64][4];
static void Quad(const GpuDevice *d, float x0, float y0, float x1, float y1, float z, int iQuad = 0)
{
  float (*p)[3] = &_afQuadPos[iQuad*4];
  float (*t)[2] = &_afQuadTex[iQuad*4];
  const float ap[4][3] = {{x0,y0,z},{x1,y0,z},{x1,y1,z},{x0,y1,z}};
  const float at[4][2] = {{0,1},{1,1},{1,0},{0,0}};
  memcpy(p, ap, sizeof(ap));
  memcpy(t, at, sizeof(at));
  d->SetStreamData(GPUS_POSITION, p, sizeof(ap));
  d->SetStreamData(GPUS_TEXCOORD0, t, sizeof(at));
}

static std::vector<std::pair<std::string, std::function<void(Ctx &)>>> _ascene;

static void AddScenes(void)
{
  _ascene.push_back({"clear", [](Ctx &x) {
    Defaults(x);
    const float afRed[4] = {1, 0, 0, 1};
    x.d->Clear(20, 30, 100, 50, true, afRed, false, 0);
  }});
  _ascene.push_back({"colorarray-tri", [](Ctx &x) {
    Defaults(x);
    static const float ap[3][3] = {{-0.8f,-0.8f,0},{0.8f,-0.6f,0},{0,0.9f,0}};
    static const uint8_t ac[3][4] = {{255,0,0,255},{0,255,0,255},{0,0,255,255}};
    x.d->SetStreamData(GPUS_POSITION, ap, sizeof(ap));
    x.d->SetStreamData(GPUS_COLOR, ac, sizeof(ac));
    GpuFFVertexConst vc = FFV(); vc.ulFlags = 1;
    x.d->SetVertexConstants(&vc, sizeof(vc));
    static const uint16_t ai[3] = {0, 1, 2};
    x.d->DrawIndexed(3, ai);
  }});
  // culling: a CCW triangle on the left, a CW one on the right (in y-up clip space)
  for (int iCull = 0; iCull < 3; iCull++) for (int iFront = 0; iFront < 2; iFront++) {
    char str[64];
    snprintf(str, sizeof(str), "cull%d-front%s", iCull, iFront ? "ccw" : "cw");
    _ascene.push_back({str, [iCull, iFront](Ctx &x) {
      Defaults(x);
      static const float ap[6][3] = {{-0.9f,-0.5f,0},{-0.1f,-0.5f,0},{-0.5f,0.5f,0}, {0.1f,-0.5f,0},{0.5f,0.5f,0},{0.9f,-0.5f,0}};
      x.d->SetStreamData(GPUS_POSITION, ap, sizeof(ap));
      x.d->SetCull((GpuCull)iCull, iFront != 0);
      static const uint16_t ai[6] = {0,1,2,3,4,5};
      x.d->DrawIndexed(6, ai);
    }});
  }
  _ascene.push_back({"texture-modulate", [](Ctx &x) {
    Defaults(x);
    Quad(x.d, -0.9f, -0.9f, 0.9f, 0.9f, 0);
    x.d->SetTexture(0, x.ptexChecker, Smp(false));
    GpuFFVertexConst vc = FFV(); vc.ulTexCoords = 1; vc.afColor[1] = 0.5f;
    x.d->SetVertexConstants(&vc, sizeof(vc));
    GpuFFFragmentConst fc = FFF(1);
    x.d->SetFragmentConstants(&fc, sizeof(fc));
    x.d->DrawQuads(4);
  }});
  _ascene.push_back({"texture-linear-2x-texmatrix", [](Ctx &x) {
    Defaults(x);
    Quad(x.d, -0.9f, -0.9f, 0.9f, 0.9f, 0);
    x.d->SetTexture(0, x.ptexGrad, Smp(true, false));
    GpuFFVertexConst vc = FFV(); vc.ulTexCoords = 1|4;
    vc.aafTexture[0][0] = 2.0f; vc.aafTexture[0][12] = 0.25f;   // s*2 + 0.25
    vc.afColor[0] = 0.6f; vc.afColor[1] = 0.4f; vc.afColor[2] = 0.5f;
    x.d->SetVertexConstants(&vc, sizeof(vc));
    GpuFFFragmentConst fc = FFF(2);
    x.d->SetFragmentConstants(&fc, sizeof(fc));
    x.d->DrawQuads(4);
  }});
  _ascene.push_back({"alphatest-blend", [](Ctx &x) {
    Defaults(x);
    Quad(x.d, -0.9f, -0.9f, 0.5f, 0.5f, 0, 0);
    x.d->SetTexture(0, x.ptexGrad, Smp(false));
    GpuFFVertexConst vc = FFV(); vc.ulTexCoords = 1;
    x.d->SetVertexConstants(&vc, sizeof(vc));
    GpuFFFragmentConst fc = FFF(1); fc.bAlphaTest = 1;
    x.d->SetFragmentConstants(&fc, sizeof(fc));
    x.d->DrawQuads(4);
    Quad(x.d, -0.5f, -0.5f, 0.9f, 0.9f, 0, 1);
    x.d->SetBlend(true, GPUB_SRC_ALPHA, GPUB_INV_SRC_ALPHA);
    fc.bAlphaTest = 0;
    x.d->SetFragmentConstants(&fc, sizeof(fc));
    x.d->DrawQuads(4);
  }});
  _ascene.push_back({"depth", [](Ctx &x) {
    Defaults(x);
    x.d->SetDepth(true, true, GPUC_LESS_EQUAL);
    GpuFFVertexConst vc = FFV(); vc.afColor[0] = 1; vc.afColor[1] = 0; vc.afColor[2] = 0;
    x.d->SetVertexConstants(&vc, sizeof(vc));
    Quad(x.d, -0.9f, -0.9f, 0.3f, 0.3f, 0.3f, 0);
    x.d->DrawQuads(4);
    vc.afColor[0] = 0; vc.afColor[1] = 1;
    x.d->SetVertexConstants(&vc, sizeof(vc));
    Quad(x.d, -0.3f, -0.3f, 0.9f, 0.9f, 0.6f, 1);
    x.d->DrawQuads(4);
    vc.afColor[1] = 0; vc.afColor[2] = 1;
    x.d->SetVertexConstants(&vc, sizeof(vc));
    x.d->SetDepth(true, false, GPUC_GREATER);
    Quad(x.d, -0.6f, -0.95f, 0.6f, 0.0f, 0.5f, 2);
    x.d->DrawQuads(4);
  }});
  _ascene.push_back({"viewport-scissor-colormask", [](Ctx &x) {
    Defaults(x);
    x.d->SetViewport(40, 20, 120, 100, 0, 1);
    x.d->SetScissor(60, 10, 200, 60);
    Quad(x.d, -1, -1, 1, 1, 0);
    x.d->SetColorMask(true, false, true, true);
    x.d->DrawQuads(4);
  }});
  _ascene.push_back({"lines-points", [](Ctx &x) {
    Defaults(x);
    static const float ap[4][3] = {{-0.9f,-0.9f,0},{0.9f,0.8f,0},{-0.5f,0.7f,0},{0.6f,-0.7f,0}};
    x.d->SetStreamData(GPUS_POSITION, ap, sizeof(ap));
    x.d->DrawPrimitives(GPUP_LINES, 4);
    GpuFFVertexConst vc = FFV(); vc.fPointSize = 9; vc.afColor[2] = 0;
    x.d->SetVertexConstants(&vc, sizeof(vc));
    x.d->DrawPrimitives(GPUP_POINTS, 4);
  }});
  _ascene.push_back({"clipplane-texgen", [](Ctx &x) {
    Defaults(x);
    Quad(x.d, -0.9f, -0.9f, 0.9f, 0.9f, 0);
    x.d->SetTexture(0, x.ptexChecker, Smp(false));
    GpuFFVertexConst vc = FFV(); vc.ulFlags = 2; vc.ulTexCoords = 8|4;
    vc.afClipPlane[0] = 1; vc.afClipPlane[1] = 0.5f; vc.afClipPlane[3] = 0.2f;
    vc.aafTexture[0][0] = 0.5f; vc.aafTexture[0][5] = 0.5f; vc.aafTexture[0][12] = 0.5f; vc.aafTexture[0][13] = 0.5f;
    x.d->SetVertexConstants(&vc, sizeof(vc));
    GpuFFFragmentConst fc = FFF(3);
    x.d->SetFragmentConstants(&fc, sizeof(fc));
    x.d->DrawQuads(4);
  }});
  _ascene.push_back({"dxt-l8-mip", [](Ctx &x) {
    Defaults(x);
    GpuFFVertexConst vc = FFV(); vc.ulTexCoords = 1;
    x.d->SetVertexConstants(&vc, sizeof(vc));
    GpuFFFragmentConst fc = FFF(3);
    x.d->SetFragmentConstants(&fc, sizeof(fc));
    Quad(x.d, -0.95f, -0.9f, -0.35f, 0.9f, 0, 0);
    x.d->SetTexture(0, x.ptexDXT, Smp(false));
    x.d->DrawQuads(4);
    Quad(x.d, -0.3f, -0.9f, 0.3f, 0.9f, 0, 1);
    x.d->SetTexture(0, x.ptexL8, Smp(true));
    x.d->DrawQuads(4);
    Quad(x.d, 0.35f, -0.2f, 0.45f, 0.2f, 0, 2);   // small: a smaller mipmap
    x.d->SetTexture(0, x.ptexMip, Smp(true, false, 1));
    x.d->DrawQuads(4);
    Quad(x.d, 0.5f, -0.9f, 0.95f, 0.9f, 0, 3);
    x.d->DrawQuads(4);
  }});
  _ascene.push_back({"wireframe-depthbias", [](Ctx &x) {
    Defaults(x);
    x.d->SetDepth(true, true, GPUC_LESS_EQUAL);
    Quad(x.d, -0.8f, -0.8f, 0.8f, 0.8f, 0.5f, 0);
    GpuFFVertexConst vc = FFV(); vc.afColor[0] = 0.2f;
    x.d->SetVertexConstants(&vc, sizeof(vc));
    x.d->DrawQuads(4);
    x.d->SetDepthBias(true);
    x.d->SetWireframe(true);
    vc.afColor[0] = 1; vc.afColor[1] = 1; vc.afColor[2] = 0;
    x.d->SetVertexConstants(&vc, sizeof(vc));
    x.d->DrawQuads(4);
  }});
  _ascene.push_back({"rendertarget-copy-pushpop", [](Ctx &x) {
    const GpuDevice *d = x.d;
    Defaults(x);
    GpuTexture *ptexRT = d->CreateTexture();
    d->TextureRenderTarget(ptexRT, 64, 64);
    d->SetRenderTarget(ptexRT);
    const float afRed[4] = {1,0,0,1}, afGreen[4] = {0,1,0,0.5f};
    d->Clear(0, 0, 64, 32, true, afRed, true, 1.0f);
    d->Clear(0, 32, 64, 32, true, afGreen, true, 1.0f);
    d->SetViewport(0, 0, 64, 64, 0, 1);
    d->SetScissor(0, 0, 64, 64);
    static const float ap[3][3] = {{-0.5f,-0.5f,0},{0.5f,-0.5f,0},{0,0.9f,0}};
    d->SetStreamData(GPUS_POSITION, ap, sizeof(ap));
    d->ClearStream(GPUS_TEXCOORD0);
    static const uint16_t ai[3] = {0,1,2};
    d->DrawIndexed(3, ai);
    d->SetRenderTarget(NULL);
    d->SetViewport(0, 0, W, H, 0, 1);
    d->SetScissor(0, 0, W, H);
    Quad(d, -1, -1, 0, 1, 0.5f, 0);
    d->SetTexture(0, ptexRT, Smp(false, true));
    GpuFFVertexConst vc = FFV(); vc.ulTexCoords = 1;
    d->SetVertexConstants(&vc, sizeof(vc));
    GpuFFFragmentConst fc = FFF(3);
    d->SetFragmentConstants(&fc, sizeof(fc));
    d->DrawQuads(4);
    // the frame's left half copied onto the right half, with the state kept around it
    GpuTexture *ptexCopy = d->CreateTexture();
    d->CopyFrameToTexture(ptexCopy, 0, 0, W/2, H);
    d->PushState();
    Quad(d, 0, -1, 1, 0.5f, 0.5f, 1);
    d->SetTexture(0, ptexCopy, Smp(false, true));
    d->DrawQuads(4);
    d->PopState();
    d->DeleteTexture(ptexRT);
    d->DeleteTexture(ptexCopy);
  }});
  // the game's programs: each vertex program with the fixed function's fragment stage, each
  // pixel program with its vertex stage (constants from a fixed seed)
  _ascene.push_back({"programs", [](Ctx &x) {}});   // (placeholder: replaced per program below)
}

static void ProgramScene(Ctx &x, bool bVertex, int iProgram)
{
  const GpuDevice *d = x.d;
  Defaults(x);
  srand(1234 + iProgram);
  // a grid of quads with every stream
  static float ap[64][3], an[64][3], at[4][64][2], atan_[64][4];
  static uint8_t ac[64][4], aw[64][8];
  static uint16_t ai[16*6];
  int ctQuads = 0;
  for (int j = 0; j < 4; j++) for (int i = 0; i < 4; i++) {
    const int v = ctQuads*4;
    const float x0 = -0.95f + i*0.48f, y0 = -0.95f + j*0.48f, x1 = x0 + 0.45f, y1 = y0 + 0.45f;
    const float aq[4][2] = {{x0,y0},{x1,y0},{x1,y1},{x0,y1}};
    for (int k = 0; k < 4; k++) {
      ap[v+k][0] = aq[k][0]; ap[v+k][1] = aq[k][1]; ap[v+k][2] = 0.5f;
      an[v+k][0] = 0.3f; an[v+k][1] = 0.5f*k/3.0f; an[v+k][2] = 0.8f;
      for (int u = 0; u < 4; u++) { at[u][v+k][0] = (k==1||k==2) ? 1.0f + u : 0.0f; at[u][v+k][1] = (k>=2) ? 1.0f : 0.0f; }
      for (int c = 0; c < 4; c++) { ac[v+k][c] = (uint8_t)(rand() & 255); atan_[v+k][c] = (rand() % 1000)/1000.0f; }
      aw[v+k][0] = 0; aw[v+k][1] = 3; aw[v+k][2] = 6; aw[v+k][3] = 9;      // bone indices (registers)
      aw[v+k][4] = 128; aw[v+k][5] = 64; aw[v+k][6] = 32; aw[v+k][7] = 31; // weights
    }
    const uint16_t aq6[6] = {0,1,2,0,2,3};
    for (int k = 0; k < 6; k++) ai[ctQuads*6+k] = (uint16_t)(v + aq6[k]);
    ctQuads++;
  }
  d->SetStreamData(GPUS_POSITION, ap, sizeof(ap));
  d->SetStreamData(GPUS_NORMAL, an, sizeof(an));
  d->SetStreamData(GPUS_COLOR, ac, sizeof(ac));
  for (int u = 0; u < 4; u++) d->SetStreamData((GpuStream)(GPUS_TEXCOORD0+u), at[u], sizeof(at[u]));
  d->SetStreamData(GPUS_WEIGHT, aw, sizeof(aw));
  d->SetStreamData(GPUS_TANGENT, atan_, sizeof(atan_));
  for (int u = 0; u < 4; u++) d->SetTexture(u, u & 1 ? x.ptexGrad : x.ptexChecker, Smp(true));
  if (bVertex) {
    GpuVPConst vc = {};
    for (int i = 0; i < GPU_VERTEX_CONSTANTS; i++) for (int c = 0; c < 4; c++) vc.afConst[i][c] = (rand() % 2000)/1000.0f - 0.5f;
    // c0..c3: about the identity (the matrix the programs take the position through)
    for (int i = 0; i < 4; i++) for (int c = 0; c < 4; c++) vc.afConst[i][c] = (i == c ? 1.0f : 0.0f) + ((rand() % 100) - 50)/1000.0f;
    vc.afColor[0] = vc.afColor[1] = vc.afColor[2] = vc.afColor[3] = 1;
    vc.ulStreams = d->GetStreamMask();
    d->SetProgram(x.aprVP[iProgram], x.pfff);
    d->SetVertexConstants(&vc, sizeof(vc));
    GpuFFFragmentConst fc = FFF(0);
    d->SetFragmentConstants(&fc, sizeof(fc));
  } else {
    GpuFFVertexConst vc = FFV(); vc.ulFlags = 1;
    vc.ulTexCoords = 1 | (1<<4) | (1<<8) | (1<<12);
    d->SetVertexConstants(&vc, sizeof(vc));
    GpuPPConst pc = {};
    for (int i = 0; i < GPU_PIXEL_CONSTANTS; i++) for (int c = 0; c < 4; c++) pc.afConst[i][c] = (rand() % 1000)/1000.0f;
    pc.fAlphaRef = 0.5f;
    d->SetProgram(x.pffv, x.aprPP[iProgram]);
    d->SetFragmentConstants(&pc, sizeof(pc));
  }
  d->DrawIndexed(ctQuads*6, ai);
}

static GpuTexture *MakeTexture(const GpuDevice *d, int iW, int iH, int ctMips, GpuTextureFormat e, std::function<uint32_t(int,int,int)> fn)
{
  GpuTexture *ptex = d->CreateTexture();
  d->TextureStorage(ptex, iW, iH, ctMips, e);
  for (int m = 0; m < ctMips; m++) {
    const int w = std::max(iW >> m, 1), h = std::max(iH >> m, 1);
    std::vector<uint32_t> aul(w*h);
    for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) aul[j*w+i] = fn(i, j, m);
    d->TextureUpload(ptex, m, w, h, aul.data());
  }
  return ptex;
}

static int _ctFail = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); printf("\n"); _ctFail++; } } while (0)

static std::vector<std::string> ProgramFiles(const char *strDir, const char *strExt)
{
  std::vector<std::string> astr;
  DIR *pd = opendir(strDir);
  while (dirent *e = readdir(pd)) {
    std::string s = e->d_name;
    if (s.size() > 3 && s.substr(s.size()-3) == strExt) astr.push_back(std::string(strDir) + "/" + s);
  }
  closedir(pd);
  std::sort(astr.begin(), astr.end());
  return astr;
}

static GpuProgram *LoadProgram(const GpuDevice *d, const std::string &strFile, bool bVertex)
{
  std::ifstream f(strFile);
  std::stringstream ss; ss << f.rdbuf();
  std::string strARB, strSrc, strErr;
  if (!(bVertex ? TranslateVertexProgramToARB(ss.str().c_str(), strARB, strErr) : TranslatePixelProgramToARB(ss.str().c_str(), strARB, strErr))
   || !d->TranslateProgram(strARB, strSrc, strErr)) {
    printf("%s %s: %s\n", d->strAPI, strFile.c_str(), strErr.c_str());
    return NULL;
  }
  char strCompile[4096];
  GpuProgram *ppr = d->CreateProgram(bVertex, strSrc.c_str(), strCompile, sizeof(strCompile));
  if (ppr == NULL) printf("%s %s: %s\n", d->strAPI, strFile.c_str(), strCompile);
  return ppr;
}

// draw every scene and read the frames back
static std::vector<Image> Run(const GpuDevice *d, SDL_Window *pwnd, const char *strDir, std::vector<std::string> &astrNames,
  std::vector<float> &afDepths)
{
  std::vector<Image> aimg;
  _pgpu = d;
  if (!d->Init(pwnd, 1)) { printf("%s: no device\n", d->strAPI); exit(1); }
  Ctx x;
  x.d = d;
  char strErr[4096];
  x.pffv = d->CreateFixedFunction(true, strErr, sizeof(strErr));
  if (!x.pffv) printf("%s FF vs: %s\n", d->strAPI, strErr);
  x.pfff = d->CreateFixedFunction(false, strErr, sizeof(strErr));
  if (!x.pfff) printf("%s FF fs: %s\n", d->strAPI, strErr);
  x.ptexChecker = MakeTexture(d, 8, 8, 1, GPUTF_RGBA8, [](int i, int j, int) { return ((i ^ j) & 1) ? 0xFF2080FFu : 0xFFFFFF20u; });
  x.ptexGrad = MakeTexture(d, 16, 16, 1, GPUTF_RGBA8, [](int i, int j, int) { return (uint32_t)(i*16) | ((uint32_t)(j*16) << 8) | 0x400000u | ((uint32_t)(i*16+15) << 24); });
  x.ptexL8 = MakeTexture(d, 4, 4, 1, GPUTF_L8, [](int i, int j, int) { return (uint32_t)((i+j*4)*16) | 0x55AA00u; });
  x.ptexMip = MakeTexture(d, 32, 32, 6, GPUTF_RGBA8, [](int i, int j, int m) { const uint32_t a[6] = {0xFF0000FF,0xFF00FF00,0xFFFF0000,0xFF00FFFF,0xFFFF00FF,0xFFFFFFFF}; return a[m]; });
  // DXT1: 8x8, four blocks of two colors (565) with a pattern
  x.ptexDXT = d->CreateTexture();
  d->TextureStorage(x.ptexDXT, 8, 8, 1, GPUTF_DXT1);
  uint8_t aubDXT[4*8];
  const uint16_t auwColors[4][2] = {{0xF800,0x001F},{0x07E0,0xFFFF},{0xFFE0,0x0000},{0x8410,0xF81F}};
  for (int b = 0; b < 4; b++) {
    memcpy(&aubDXT[b*8], auwColors[b], 4);
    const uint32_t ulBits = 0x1B1B4E4Eu*(b+1);
    memcpy(&aubDXT[b*8+4], &ulBits, 4);
  }
  d->TextureUploadCompressed(x.ptexDXT, 0, 8, 8, aubDXT, sizeof(aubDXT));

  std::vector<std::string> astrVP = ProgramFiles(strDir, ".vs"), astrPP = ProgramFiles(strDir, ".ps");
  int ctProgramsOK = 0;
  for (auto &s : astrVP) { x.aprVP.push_back(LoadProgram(d, s, true)); ctProgramsOK += x.aprVP.back() != NULL; }
  for (auto &s : astrPP) { x.aprPP.push_back(LoadProgram(d, s, false)); ctProgramsOK += x.aprPP.back() != NULL; }
  printf("%s: %d of %zu programs\n", d->strAPI, ctProgramsOK, astrVP.size() + astrPP.size());

  std::vector<std::pair<std::string, std::function<void(Ctx &)>>> ascene = _ascene;
  ascene.pop_back();
  for (size_t i = 0; i < x.aprVP.size(); i++) if (x.aprVP[i]) ascene.push_back({"vp " + astrVP[i].substr(astrVP[i].rfind('/')+1), [i](Ctx &x) { ProgramScene(x, true, (int)i); }});
  for (size_t i = 0; i < x.aprPP.size(); i++) if (x.aprPP[i]) ascene.push_back({"pp " + astrPP[i].substr(astrPP[i].rfind('/')+1), [i](Ctx &x) { ProgramScene(x, false, (int)i); }});

  astrNames.clear();
  for (auto &sc : ascene) {
    sc.second(x);
    Image img(W*H*3);
    if (!d->ReadFrameRGB(0, 0, W, H, img.data())) printf("%s %s: no frame read\n", d->strAPI, sc.first.c_str());
    if (sc.first == "depth") {
      const int aiXY[] = {20, 170, 200, 20, 128, 96, 128, 180, 1000, 5};
      float af[5];
      if (!d->ReadFrameDepths(aiXY, 5, af)) printf("%s: no depth read\n", d->strAPI);
      afDepths.assign(af, af + 5);
    }
    aimg.push_back(img);
    astrNames.push_back(sc.first);
    d->Present();
    SDL_PumpEvents();
  }
  for (auto *p : x.aprVP) if (p) d->DeleteProgram(p);
  for (auto *p : x.aprPP) if (p) d->DeleteProgram(p);
  d->DeleteProgram(x.pffv); d->DeleteProgram(x.pfff);
  d->DeleteTexture(x.ptexChecker); d->DeleteTexture(x.ptexGrad); d->DeleteTexture(x.ptexL8);
  d->DeleteTexture(x.ptexMip); d->DeleteTexture(x.ptexDXT);
  d->End();
  return aimg;
}

static void SavePPM(const std::string &strFile, const Image &img)
{
  FILE *pf = fopen(strFile.c_str(), "wb");
  fprintf(pf, "P6\n%d %d\n255\n", W, H);
  fwrite(img.data(), 1, img.size(), pf);
  fclose(pf);
}

int main(int argc, char **argv)
{
  const char *strDir = argc > 1 ? argv[1] : ".";
  const char *strSave = (argc > 3 && std::string(argv[2]) == "-save") ? argv[3] : NULL;
  SDL_Init(SDL_INIT_VIDEO);
  SDL_Window *pwnd = SDL_CreateWindow("parity", W, H, 0);
  SDL_PumpEvents();
  if (!vlkIsAvailable()) { printf("no Vulkan\n"); return 1; }
  AddScenes();
  std::vector<std::string> astrM, astrV;
  std::vector<float> afDepthM, afDepthV;
  std::vector<Image> aimgM = Run(mtlDevice(), pwnd, strDir, astrM, afDepthM);
  std::vector<Image> aimgV = Run(vlkDevice(), pwnd, strDir, astrV, afDepthV);
  CHECK(astrM == astrV, "different scenes (%zu Metal, %zu Vulkan)", astrM.size(), astrV.size());
  const size_t ct = std::min(aimgM.size(), aimgV.size());
  int ctSame = 0;
  for (size_t i = 0; i < ct; i++) {
    int ctDiff = 0, iMax = 0;
    for (size_t p = 0; p < aimgM[i].size(); p++) {
      const int iD = abs((int)aimgM[i][p] - (int)aimgV[i][p]);
      if (iD > 3) ctDiff++;
      if (iD > iMax) iMax = iD;
    }
    if (ctDiff > 0) printf("DIFF %-36s %6d components off, max %d\n", astrM[i].c_str(), ctDiff, iMax);
    else ctSame++;
    if (strSave) {
      std::string s = astrM[i];
      std::replace(s.begin(), s.end(), ' ', '_');
      SavePPM(std::string(strSave) + "/" + s + ".metal.ppm", aimgM[i]);
      SavePPM(std::string(strSave) + "/" + s + ".vulkan.ppm", aimgV[i]);
    }
  }
  printf("%d of %zu scenes the same\n", ctSame, ct);
  for (size_t i = 0; i < afDepthM.size() && i < afDepthV.size(); i++) {
    if (fabsf(afDepthM[i] - afDepthV[i]) > 1e-4f) printf("DIFF depth %zu: Metal %f, Vulkan %f\n", i, afDepthM[i], afDepthV[i]);
  }
  printf("depths (Metal): %f %f %f %f %f\n", afDepthM[0], afDepthM[1], afDepthM[2], afDepthM[3], afDepthM[4]);
  SDL_DestroyWindow(pwnd);
  SDL_Quit();
  return _ctFail;
}
