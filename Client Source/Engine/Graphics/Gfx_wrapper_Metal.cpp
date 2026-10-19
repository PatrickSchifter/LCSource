// The gfx* functions for Metal (GAT_MTL), on top of MetalDevice.h. Included by Gfx_wrapper.cpp
// like the OpenGL and Direct3D ones.
//
// Metal has no fixed function pipeline: what OpenGL does with it here (vertex arrays,
// projection and view matrices, texture matrices and units with MODULATE or 2X combine, constant
// color or color array, alpha test, user clip plane, point size) is a program of its own
// (_strFixedFunction), whose constants are made from the engine's state at every draw. The other
// state (blending, depth, culling, viewport...) also goes to the device at every draw; the
// enable/disable functions only keep it in the GFX_* variables.
//
// Matrices are those of Direct3D (depth 0..1), without its half pixel offset (Metal samples at
// pixel centers like OpenGL). Viewports and scissors are given from the top left in the engine's
// pixels (window points), times GFX_DrawableScale() on the device.

#include <Engine/Graphics/MetalDevice.h>
#include <Engine/Graphics/ShaderAsmToARB.h>
#include <Engine/Graphics/ShaderARBToMSL.h>
#include <set>
#include <string>
#include <vector>

extern PIX GFX_DrawableScale(void);   // GfxLibrary.cpp
extern ULONG _aulTempRow[4096];       // Graphics.cpp

// the fixed function emulation (constants: MtlFFVertexConst and MtlFFFragmentConst below), with
// the stage interface of the programs (ShaderARBToMSL.h), so either can go with the other
static const char *_strFixedFunction = MSL_PROGRAM_INTERFACE R"MSL(
struct FFVertexConst {
  float4x4 mProjection;
  float4x4 mView;
  float4x4 amTexture[4];
  float4 vClipPlane;     // in view space
  float4 colConstant;    // without color array
  uint ulFlags;          // 1=color array, 2=clip plane
  uint ulTexCoords;      // 4 bits per texture unit: 1=array, 2=s,t,r,q, 4=texture matrix,
                         // 8=the position in view space (Direct3D's D3DTSS_TCI_CAMERASPACEPOSITION)
  float fPointSize;
  uint ulPad;
};

static float4 TexCoord(const device float *pfTex, uint vid, uint ulMode, float4x4 mTexture, float4 vView)
{
  float4 v = float4(0, 0, 0, 1);
  if (ulMode & 8) {
    v = vView;
  } else if (ulMode & 1) {
    if (ulMode & 2) v = float4(pfTex[vid*4], pfTex[vid*4+1], pfTex[vid*4+2], pfTex[vid*4+3]);
    else v = float4(pfTex[vid*2], pfTex[vid*2+1], 0, 1);
  }
  if (ulMode & 4) v = mTexture * v;
  return v;
}

vertex MtlVSOut ff_vs(uint vid [[vertex_id]], constant FFVertexConst &c [[buffer(0)]],
  const device packed_float3 *pvtx [[buffer(1)]], const device uchar4 *pcol [[buffer(2)]],
  const device float *ptex0 [[buffer(4)]], const device float *ptex1 [[buffer(5)]],
  const device float *ptex2 [[buffer(6)]], const device float *ptex3 [[buffer(7)]])
{
  MtlVSOut o;
  const float4 vView = c.mView * float4(float3(pvtx[vid]), 1);
  o.pos = c.mProjection * vView;
  o.col0 = (c.ulFlags & 1) ? float4(pcol[vid]) / 255.0 : c.colConstant;
  o.col1 = float4(0);
  o.tex0 = TexCoord(ptex0, vid, (c.ulTexCoords >>  0) & 15, c.amTexture[0], vView);
  o.tex1 = TexCoord(ptex1, vid, (c.ulTexCoords >>  4) & 15, c.amTexture[1], vView);
  o.tex2 = TexCoord(ptex2, vid, (c.ulTexCoords >>  8) & 15, c.amTexture[2], vView);
  o.tex3 = TexCoord(ptex3, vid, (c.ulTexCoords >> 12) & 15, c.amTexture[3], vView);
  o.fPointSize = c.fPointSize;
  o.afClip[0] = (c.ulFlags & 2) ? dot(vView, c.vClipPlane) : 1.0;
  return o;
}

struct FFFragmentConst {
  uint4 aulTexMode;      // per texture unit: 0=off, 1=modulate, 2=modulate 2x, 3=replace, 4=interpolate
  float fAlphaRef;
  uint bAlphaTest;
  float fLODBias;
  float fFactor;         // for 4
};

// as GL_MODULATE, GL_COMBINE with GL_MODULATE and GL_RGB_SCALE 2 (results clamped), GL_REPLACE,
// or GL_COMBINE with GL_INTERPOLATE of the previous color and the texture by the constant (the
// terrain's shadow map blend, TRRender.cpp)
static float4 Stage(float4 col, float4 tex, uint ulMode, float fFactor)
{
  if (ulMode == 2) return float4(saturate(col.rgb*tex.rgb*2.0), col.a*tex.a);
  if (ulMode == 3) return tex;
  if (ulMode == 4) return float4(mix(tex.rgb, col.rgb, fFactor), col.a);
  return col*tex;
}

fragment float4 ff_fs(MtlPSIn in [[stage_in]], constant FFFragmentConst &c [[buffer(0)]],
  texture2d<float> t0 [[texture(0)]], sampler s0 [[sampler(0)]],
  texture2d<float> t1 [[texture(1)]], sampler s1 [[sampler(1)]],
  texture2d<float> t2 [[texture(2)]], sampler s2 [[sampler(2)]],
  texture2d<float> t3 [[texture(3)]], sampler s3 [[sampler(3)]])
{
  float4 col = in.col0;
  if (c.aulTexMode.x) col = Stage(col, t0.sample(s0, in.tex0.xy/in.tex0.w, bias(c.fLODBias)), c.aulTexMode.x, c.fFactor);
  if (c.aulTexMode.y) col = Stage(col, t1.sample(s1, in.tex1.xy/in.tex1.w, bias(c.fLODBias)), c.aulTexMode.y, c.fFactor);
  if (c.aulTexMode.z) col = Stage(col, t2.sample(s2, in.tex2.xy/in.tex2.w, bias(c.fLODBias)), c.aulTexMode.z, c.fFactor);
  if (c.aulTexMode.w) col = Stage(col, t3.sample(s3, in.tex3.xy/in.tex3.w, bias(c.fLODBias)), c.aulTexMode.w, c.fFactor);
  if (c.bAlphaTest && col.a < c.fAlphaRef) discard_fragment();
  return col;
}
)MSL";

struct MtlFFVertexConst {
  FLOAT afProjection[16];
  FLOAT afView[16];
  FLOAT aafTexture[4][16];
  FLOAT afClipPlane[4];
  FLOAT afColor[4];
  ULONG ulFlags;
  ULONG ulTexCoords;
  FLOAT fPointSize;
  ULONG ulPad;
};

struct MtlFFFragmentConst {
  ULONG aulTexMode[4];
  FLOAT fAlphaRef;
  ULONG bAlphaTest;
  FLOAT fLODBias;
  FLOAT fFactor;
};

static MtlProgram *_pprFFVertex   = NULL;
static MtlProgram *_pprFFFragment = NULL;

// state of the emulated fixed function
static FLOAT _mtl_afProjection[16];
static FLOAT _mtl_afView[16];
static FLOAT _mtl_aafTexture[GFX_USETEXUNITS][16];
static ULONG _mtl_ulTexMatrices = 0;                  // units with a texture matrix (bits)
static ULONG _mtl_ulTexGenView = 0;                   // units whose coordinates are the view space position (bits)
static UBYTE _mtl_aubTexCoords[GFX_USETEXUNITS];      // per unit: 1=array, 2=s,t,r,q
static FLOAT _mtl_afClipPlane[4];
static COLOR _mtl_colConstant = C_WHITE|CT_OPAQUE;
static FLOAT _mtl_fPointSize = 1.0f;
static BOOL  _mtl_bWireframe = FALSE;
static BOOL  _mtl_bDepthBias = FALSE;
static BOOL  _mtl_abSingleMipmap[GFX_MAXTEXUNITS];    // of the texture set on each unit
static BOOL  _mtl_bShadowMapBlend = FALSE;            // the terrain's (MTL_SetShadowMapBlend)
static FLOAT _mtl_fShadowMapBlend = 0;
// in the engine's pixels, from the top left of the render target
static PIX _mtl_aiViewport[4] = {0, 0, 1, 1};
static PIX _mtl_aiScissor[4]  = {0, 0, 1, 1};

static const FLOAT _afIdentity[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };

// vertex and pixel programs (Direct3D's assembly translated thru ARB into MSL): a handle is an
// index+1 into _mtl_apprPrograms; the constants of both kinds are kept here and go with each draw
static std::vector<MtlProgram*> _mtl_apprPrograms;
static FLOAT _mtl_afVertexConst[MSL_VERTEX_CONSTANTS][4];
static FLOAT _mtl_afPixelConst[MSL_PIXEL_CONSTANTS][4];

static MtlProgram *mtl_Program( ULONG ulHandle)
{
	if( ulHandle==NONE || ulHandle>_mtl_apprPrograms.size()) return NULL;
	return _mtl_apprPrograms[ulHandle-1];
}

// called on a new context
extern BOOL InitFixedFunction_MTL(void)
{
	char strError[2048];
	_pprFFVertex   = mtlCreateProgram( _strFixedFunction, "ff_vs", strError, sizeof(strError));
	if( _pprFFVertex==NULL) CPrintF( "Metal: fixed function vertex program: %s\n", strError);
	_pprFFFragment = mtlCreateProgram( _strFixedFunction, "ff_fs", strError, sizeof(strError));
	if( _pprFFFragment==NULL) CPrintF( "Metal: fixed function fragment program: %s\n", strError);
	memcpy( _mtl_afProjection, _afIdentity, sizeof(_afIdentity));
	memcpy( _mtl_afView, _afIdentity, sizeof(_afIdentity));
	for( INDEX iUnit=0; iUnit<GFX_USETEXUNITS; iUnit++) {
		memcpy( _mtl_aafTexture[iUnit], _afIdentity, sizeof(_afIdentity));
		_mtl_aubTexCoords[iUnit] = 0;
	}
	for( INDEX iUnit=0; iUnit<GFX_MAXTEXUNITS; iUnit++) _mtl_abSingleMipmap[iUnit] = FALSE;
	_mtl_ulTexMatrices = 0;
	_mtl_ulTexGenView = 0;
	_mtl_afClipPlane[0] = _mtl_afClipPlane[1] = _mtl_afClipPlane[2] = _mtl_afClipPlane[3] = 0;
	_mtl_colConstant = C_WHITE|CT_OPAQUE;
	_mtl_fPointSize = 1.0f;
	_mtl_bWireframe = FALSE;
	_mtl_bDepthBias = FALSE;
	_mtl_bShadowMapBlend = FALSE;
	memset( _mtl_afVertexConst, 0, sizeof(_mtl_afVertexConst));
	memset( _mtl_afPixelConst, 0, sizeof(_mtl_afPixelConst));
	return _pprFFVertex!=NULL && _pprFFFragment!=NULL;
}

extern void EndFixedFunction_MTL(void)
{
	if( _pprFFVertex!=NULL)   mtlDeleteProgram(_pprFFVertex);
	if( _pprFFFragment!=NULL) mtlDeleteProgram(_pprFFFragment);
	_pprFFVertex = _pprFFFragment = NULL;
	// (the engine has deleted its programs by now: ClearVertexAndPixelPrograms())
	for( size_t i=0; i<_mtl_apprPrograms.size(); i++) {
		if( _mtl_apprPrograms[i]!=NULL) mtlDeleteProgram(_mtl_apprPrograms[i]);
	}
	_mtl_apprPrograms.clear();
}


// VIEWPORT, SCISSOR AND CLEARING (for GfxLibrary.cpp and DrawPort.cpp)

extern void MTL_SetViewport( PIX pixI, PIX pixJ, PIX pixW, PIX pixH)
{
	_mtl_aiViewport[0] = pixI;  _mtl_aiViewport[1] = pixJ;
	_mtl_aiViewport[2] = pixW;  _mtl_aiViewport[3] = pixH;
}

extern void MTL_SetScissor( PIX pixI, PIX pixJ, PIX pixW, PIX pixH)
{
	_mtl_aiScissor[0] = pixI;  _mtl_aiScissor[1] = pixJ;
	_mtl_aiScissor[2] = pixW;  _mtl_aiScissor[3] = pixH;
}

// both, as x,y,w,h (for CRenderTexture, which puts them back)
extern void MTL_GetViewportAndScissor( PIX *ppixViewport, PIX *ppixScissor)
{
	memcpy( ppixViewport, _mtl_aiViewport, sizeof(_mtl_aiViewport));
	memcpy( ppixScissor,  _mtl_aiScissor,  sizeof(_mtl_aiScissor));
}

// fill a rectangle with a color and/or a depth (as glClear in a scissor, without the masks)
extern void MTL_Clear( PIX pixI, PIX pixJ, PIX pixW, PIX pixH, BOOL bColor, COLOR col, BOOL bDepth, FLOAT fDepth)
{
	const PIX pixScale = GFX_DrawableScale();
	UBYTE ubR, ubG, ubB, ubA;
	ColorToRGBA( col, ubR, ubG, ubB, ubA);
	const float afColor[4] = { ubR/255.0f, ubG/255.0f, ubB/255.0f, 1.0f };
	mtlClear( pixI*pixScale, pixJ*pixScale, pixW*pixScale, pixH*pixScale, bColor!=FALSE, afColor, bDepth!=FALSE, fDepth);
}


// the state of the engine into the device, for a draw
static void mtl_SyncState(void)
{
	const PIX pixScale = GFX_DrawableScale();
	mtlSetViewport( _mtl_aiViewport[0]*pixScale, _mtl_aiViewport[1]*pixScale,
	                _mtl_aiViewport[2]*pixScale, _mtl_aiViewport[3]*pixScale, GFX_fMinDepthRange, GFX_fMaxDepthRange);
	mtlSetScissor( _mtl_aiScissor[0]*pixScale, _mtl_aiScissor[1]*pixScale, _mtl_aiScissor[2]*pixScale, _mtl_aiScissor[3]*pixScale);
	mtlSetBlend( GFX_bBlending!=FALSE, (MtlBlend)(GFX_eBlendSrc-GFX_ONE), (MtlBlend)(GFX_eBlendDst-GFX_ONE));
	const ULONG ulMask = GFX_ulCurrentColorMask;
	mtlSetColorMask( (ulMask&CT_RMASK)==CT_RMASK, (ulMask&CT_GMASK)==CT_GMASK, (ulMask&CT_BMASK)==CT_BMASK, (ulMask&CT_AMASK)==CT_AMASK);
	mtlSetDepth( GFX_bDepthTest!=FALSE, GFX_bDepthWrite!=FALSE, (MtlCompare)(GFX_eDepthFunc-GFX_NEVER));
	const MtlCull eCull = GFX_eCullFace==GFX_FRONT ? MTLCULL_FRONT : GFX_eCullFace==GFX_BACK ? MTLCULL_BACK : MTLCULL_NONE;
	mtlSetCull( eCull, GFX_bFrontFace!=FALSE);
	mtlSetWireframe( _mtl_bWireframe!=FALSE);
	mtlSetDepthBias( _mtl_bDepthBias!=FALSE);

	// programs, where the engine has them on (else the fixed function emulation)
	MtlProgram *pprVertex = NULL, *pprPixel = NULL;
	if( _pGfx->gl_ulFlags&GLF_VERTEXPROGRAM) {
		if( GFX_bUseVertexProgram) pprVertex = mtl_Program(_pGfx->gl_dwVertexShader);
		if( GFX_bUsePixelProgram)  pprPixel  = mtl_Program(_pGfx->gl_dwPixelShader);
	}
	mtlSetProgram( pprVertex!=NULL ? pprVertex : _pprFFVertex, pprPixel!=NULL ? pprPixel : _pprFFFragment);

	if( pprVertex!=NULL) {
		MtlVPConst vc;
		memcpy( vc.afConst, _mtl_afVertexConst, sizeof(vc.afConst));
		UBYTE ubR, ubG, ubB, ubA;
		ColorToRGBA( _mtl_colConstant, ubR, ubG, ubB, ubA);
		vc.afColor[0] = ubR/255.0f;  vc.afColor[1] = ubG/255.0f;
		vc.afColor[2] = ubB/255.0f;  vc.afColor[3] = ubA/255.0f;
		// (a color array that is off, as with OpenGL, is the constant color)
		vc.ulStreams = mtlGetStreamMask();
		if( !GFX_bColorArray) vc.ulStreams &= ~(1UL<<MTLS_COLOR);
		vc.ulTexCoords = 0;
		for( INDEX iUnit=0; iUnit<GFX_USETEXUNITS; iUnit++) vc.ulTexCoords |= (ULONG)_mtl_aubTexCoords[iUnit]<<(iUnit*4);
		vc.aulPad[0] = vc.aulPad[1] = 0;
		mtlSetVertexConstants( &vc, sizeof(vc));
	} else {
		MtlFFVertexConst vc;
		memcpy( vc.afProjection, _mtl_afProjection, sizeof(vc.afProjection));
		memcpy( vc.afView, _mtl_afView, sizeof(vc.afView));
		ULONG ulTexCoords = 0;
		for( INDEX iUnit=0; iUnit<GFX_USETEXUNITS; iUnit++) {
			memcpy( vc.aafTexture[iUnit], _mtl_aafTexture[iUnit], sizeof(vc.aafTexture[iUnit]));
			ULONG ulMode = _mtl_aubTexCoords[iUnit];
			if( _mtl_ulTexMatrices&(1UL<<iUnit)) ulMode |= 4;
			if( _mtl_ulTexGenView &(1UL<<iUnit)) ulMode |= 8;
			ulTexCoords |= ulMode<<(iUnit*4);
		}
		memcpy( vc.afClipPlane, _mtl_afClipPlane, sizeof(vc.afClipPlane));
		UBYTE ubR, ubG, ubB, ubA;
		ColorToRGBA( _mtl_colConstant, ubR, ubG, ubB, ubA);
		vc.afColor[0] = ubR/255.0f;  vc.afColor[1] = ubG/255.0f;
		vc.afColor[2] = ubB/255.0f;  vc.afColor[3] = ubA/255.0f;
		vc.ulFlags = (GFX_bColorArray ? 1 : 0) | (GFX_bClipPlane ? 2 : 0);
		vc.ulTexCoords = ulTexCoords;
		vc.fPointSize  = _mtl_fPointSize*pixScale;
		vc.ulPad = 0;
		mtlSetVertexConstants( &vc, sizeof(vc));
	}

	if( pprPixel!=NULL) {
		MtlPPConst pc;
		memcpy( pc.afConst, _mtl_afPixelConst, sizeof(pc.afConst));
		pc.fAlphaRef  = 0.5f;
		pc.bAlphaTest = GFX_bAlphaTest ? 1 : 0;
		pc.fLODBias   = _fCurrentLODBias;
		pc.ulPad = 0;
		mtlSetFragmentConstants( &pc, sizeof(pc));
		return;
	}
	MtlFFFragmentConst fc;
	for( INDEX iUnit=0; iUnit<4; iUnit++) {
		fc.aulTexMode[iUnit] = 0;
		if( iUnit<_pGfx->gl_ctTextureUnits && GFX_abTexture[iUnit]) {
			fc.aulTexMode[iUnit] = (GFX_iTexModulation[iUnit]==2) ? 2 : 1;
		}
	}
	fc.fFactor = 0;
	if( _mtl_bShadowMapBlend) {
		fc.aulTexMode[0] = 3;
		fc.aulTexMode[1] = 4;
		fc.fFactor = _mtl_fShadowMapBlend;
	}
	fc.fAlphaRef  = 0.5f;  // glAlphaFunc( GL_GEQUAL, 0.5f)
	fc.bAlphaTest = GFX_bAlphaTest ? 1 : 0;
	fc.fLODBias   = _fCurrentLODBias;
	mtlSetFragmentConstants( &fc, sizeof(fc));
}


// ENABLE/DISABLE FUNCTIONS (the state goes to the device at the next draw)

static void mtl_EnableTexture(void)     { GFX_abTexture[GFX_iActiveTexUnit] = TRUE; }
static void mtl_DisableTexture(void)
{
	GFX_abTexture[GFX_iActiveTexUnit] = FALSE;
	// as OpenGL, which disables the unit's texture coordinate array too
	if( GFX_iActiveTexUnit<GFX_USETEXUNITS) {
		mtlClearStream( (MtlStream)(MTLS_TEXCOORD0+GFX_iActiveTexUnit));
		_mtl_aubTexCoords[GFX_iActiveTexUnit] = 0;
	}
}
static void mtl_EnableDepthTest(void)   { GFX_bDepthTest  = TRUE;  }
static void mtl_DisableDepthTest(void)  { GFX_bDepthTest  = FALSE; }
static void mtl_EnableDepthBias(void)   { _mtl_bDepthBias = TRUE;  }
static void mtl_DisableDepthBias(void)  { _mtl_bDepthBias = FALSE; }
static void mtl_EnableDepthWrite(void)  { GFX_bDepthWrite = TRUE;  }
static void mtl_DisableDepthWrite(void) { GFX_bDepthWrite = FALSE; }
static void mtl_EnableDither(void)      { GFX_bDithering  = TRUE;  }
static void mtl_DisableDither(void)     { GFX_bDithering  = FALSE; }
static void mtl_EnableAlphaTest(void)   { GFX_bAlphaTest  = TRUE;  }
static void mtl_DisableAlphaTest(void)  { GFX_bAlphaTest  = FALSE; }
static void mtl_EnableBlend(void)       { GFX_bBlending   = TRUE;  }
static void mtl_DisableBlend(void)      { GFX_bBlending   = FALSE; }
static void mtl_EnableLighting(void)    { GFX_bLighting   = TRUE;  }  // (the engine draws nothing with it)
static void mtl_DisableLighting(void)   { GFX_bLighting   = FALSE; }
static void mtl_EnableClipping(void)    { GFX_bClipping   = TRUE;  }
static void mtl_DisableClipping(void)   { GFX_bClipping   = FALSE; }
static void mtl_EnableClipPlane(void)   { GFX_bClipPlane  = TRUE;  }
static void mtl_DisableClipPlane(void)  { GFX_bClipPlane  = FALSE; }
static void mtl_EnableColorArray(void)  { GFX_bColorArray = TRUE;  }
static void mtl_DisableColorArray(void) { GFX_bColorArray = FALSE; }
static void mtl_EnableTruform(void)     { NOTHING; }
static void mtl_DisableTruform(void)    { NOTHING; }
static void mtl_EnableLight( INDEX iLight)  { GFX_abLights[iLight] = TRUE;  }
static void mtl_DisableLight( INDEX iLight) { GFX_abLights[iLight] = FALSE; }
static void mtl_SetLightColor( INDEX iLight, COLOR colLight, COLOR colAmbient, COLOR colSpecular) { NOTHING; }
static void mtl_SetLightDirection( INDEX iLight, const FLOAT3D &vLightDir) { NOTHING; }

static void mtl_BlendFunc( GfxBlend eSrc, GfxBlend eDst)
{
	GFX_eBlendSrc = eSrc;
	GFX_eBlendDst = eDst;
}

static void mtl_SetColorMask( ULONG ulColorMask) { GFX_ulCurrentColorMask = ulColorMask; }
static void mtl_DepthFunc( GfxComp eFunc)       { GFX_eDepthFunc = eFunc; }

static void mtl_DepthRange( FLOAT fMin, FLOAT fMax)
{
	GFX_fMinDepthRange = fMin;
	GFX_fMaxDepthRange = fMax;
}

static void mtl_CullFace( GfxFace eFace)
{
	ASSERT( eFace==GFX_FRONT || eFace==GFX_BACK || eFace==GFX_NONE);
	GFX_eCullFace = eFace;
}

static void mtl_FrontFace( GfxFace eFace)
{
	ASSERT( eFace==GFX_CW || eFace==GFX_CCW);
	GFX_bFrontFace = (eFace==GFX_CCW);
}

// the plane is in view space (as Direct3D takes it)
static void mtl_ClipPlane( const DOUBLE *pdViewPlane)
{
	for( INDEX i=0; i<4; i++) _mtl_afClipPlane[i] = (FLOAT)pdViewPlane[i];
}

static void mtl_SetTextureMatrix( const FLOAT *pfMatrix/*=NULL*/)
{
	const INDEX iUnit = GFX_iActiveTexUnit;
	if( iUnit>=GFX_USETEXUNITS) return;
	if( pfMatrix!=NULL) {
		memcpy( _mtl_aafTexture[iUnit], pfMatrix, 16*sizeof(FLOAT));
		_mtl_ulTexMatrices |= 1UL<<iUnit;
	} else {
		_mtl_ulTexMatrices &= ~(1UL<<iUnit);
	}
}

static void mtl_SetViewMatrix( const FLOAT *pfMatrix/*=NULL*/)
{
	GFX_bViewMatrix = (pfMatrix!=NULL);
	memcpy( _mtl_afView, pfMatrix!=NULL ? pfMatrix : _afIdentity, 16*sizeof(FLOAT));
}

// as d3d_SetOrtho(), without the half pixel offset
static void mtl_SetOrtho( FLOAT fLeft, FLOAT fRight, FLOAT fTop, FLOAT fBottom, FLOAT fNear, FLOAT fFar,
                          const BOOL bSubPixelAdjust/*=FALSE*/)
{
	GFX_fLastL = fLeft;   GFX_fLastT = fTop;     GFX_fLastN = fNear;
	GFX_fLastR = fRight;  GFX_fLastB = fBottom;  GFX_fLastF = fFar;
	if( fRight==fLeft || fTop==fBottom) {  // (as OpenGL does it)
		fLeft = 0;  fRight = 1;
		fBottom = 0;  fTop = 1;
	}
	const FLOAT fRpL=fRight+fLeft;  const FLOAT fRmL=fRight-fLeft;
	const FLOAT fTpB=fTop+fBottom;  const FLOAT fTmB=fTop-fBottom;  const FLOAT fFmN=fFar-fNear;
	const FLOAT afMatrix[16] = {
		    2/fRmL,          0,           0, 0,
		         0,     2/fTmB,           0, 0,
		         0,          0,     -1/fFmN, 0,
		-fRpL/fRmL, -fTpB/fTmB, -fNear/fFmN, 1 };
	memcpy( _mtl_afProjection, afMatrix, sizeof(afMatrix));
}

// as d3d_SetFrustum()
static void mtl_SetFrustum( FLOAT fLeft, FLOAT fRight, FLOAT fTop, FLOAT fBottom, FLOAT fNear, FLOAT fFar)
{
	// fix for "out of screen" values
	if( fRight ==fLeft) fRight  += 0.001f;
	if( fBottom==fTop)  fBottom += 0.001f;
	if( fFar   ==fNear) fFar    += 0.001f;
	GFX_fLastL = -fLeft;   GFX_fLastT = -fTop;     GFX_fLastN = -fNear;
	GFX_fLastR = -fRight;  GFX_fLastB = -fBottom;  GFX_fLastF = -fFar;
	const FLOAT fRpL=fRight+fLeft;  const FLOAT fTpB=fTop+fBottom;
	const FLOAT fRmL=fRight-fLeft;  const FLOAT fTmB=fTop-fBottom;  const FLOAT fFmN=fFar-fNear;
	const FLOAT afMatrix[16] = {
		2*fNear/fRmL,            0,                0,  0,
		           0, 2*fNear/fTmB,                0,  0,
		   fRpL/fRmL,    fTpB/fTmB,       -fFar/fFmN, -1,
		           0,            0, -fFar*fNear/fFmN,  0 };
	memcpy( _mtl_afProjection, afMatrix, sizeof(afMatrix));
}

// current projection and view matrices (Direct3D's depth range, Shader.cpp and Render.cpp), and
// a projection given as for Direct3D
extern void GetProjectionMatrix_MTL( FLOAT *pfMatrix) { memcpy( pfMatrix, _mtl_afProjection, 16*sizeof(FLOAT)); }
extern void GetViewMatrix_MTL( FLOAT *pfMatrix)       { memcpy( pfMatrix, _mtl_afView, 16*sizeof(FLOAT)); }
extern void SetProjectionMatrix_MTL( const FLOAT *pfMatrix)
{
	memcpy( _mtl_afProjection, pfMatrix, 16*sizeof(FLOAT));
	GFX_fLastL = GFX_fLastR = GFX_fLastT = GFX_fLastB = GFX_fLastN = GFX_fLastF = 0;  // (no cached ortho)
}

// texture coordinates of the current unit from the position in view space, through its texture
// matrix and divided by q: Direct3D's D3DTSS_TCI_CAMERASPACEPOSITION with D3DTTFF_COUNT4|D3DTTFF_PROJECTED
// (the projected shadows, Render.cpp; Metal's render targets start at the top as Direct3D's)
extern void MTL_SetTexGenViewPosition( BOOL bOn)
{
	const INDEX iUnit = GFX_iActiveTexUnit;
	if( iUnit>=GFX_USETEXUNITS) return;
	if( bOn) _mtl_ulTexGenView |=  (1UL<<iUnit);
	else     _mtl_ulTexGenView &= ~(1UL<<iUnit);
}

// the terrain's shadow map blend without programs: unit 0 takes the current shadow map, unit 1
// goes to the next one by the factor (as SetShadowMapBlend_OGL in TRRender.cpp)
extern void MTL_SetShadowMapBlend( FLOAT fBlendFactor)
{
	_mtl_bShadowMapBlend = TRUE;
	_mtl_fShadowMapBlend = fBlendFactor;
}
extern void MTL_ResetShadowMapBlend(void) { _mtl_bShadowMapBlend = FALSE; }

static void mtl_PolygonMode( GfxPolyMode ePolyMode)
{
	// (Metal has no point mode for triangles: lines instead)
	_mtl_bWireframe = (ePolyMode!=GFX_FILL);
}


// TEXTURES

static void mtl_SetTextureWrapping( enum GfxWrap eWrapU, enum GfxWrap eWrapV)
{
	_tpGlobal[GFX_iActiveTexUnit].tp_eWrapU = eWrapU;
	_tpGlobal[GFX_iActiveTexUnit].tp_eWrapV = eWrapV;
	// on the texture set now too, as OpenGL's glTexParameteri() and Direct3D's stage state
	if( GFX_iActiveTexUnit<GFX_USETEXUNITS) mtlSetTextureWrap( GFX_iActiveTexUnit, eWrapU==GFX_CLAMP, eWrapV==GFX_CLAMP);
}

static void mtl_SetTextureModulation( INDEX iScale)
{
	ASSERT( iScale==1 || iScale==2);
	GFX_iTexModulation[GFX_iActiveTexUnit] = iScale;
}

static void mtl_GenerateTexture( TEXOBJ &ulTexObject)
{
	ulTexObject = (TEXOBJ)mtlCreateTexture();
}

static void mtl_DeleteTexture( TEXOBJ &ulTexObject)
{
	if( ulTexObject==NONE) return;
	MEMTRACK_FREE( (void*)(ulTexObject^0x80000000));
	mtlDeleteTexture( (MtlTexture*)ulTexObject);
	ulTexObject = NONE;
}

// set a texture on the current unit, with the sampling the engine asks for (gfxSetTexture)
static void SetTexture_MTL( TEXOBJ &ulTexObject, CTexParams &tpLocal)
{
	const INDEX iUnit = GFX_iActiveTexUnit;
	GFX_apulCurrentTexture[iUnit] = &ulTexObject;  // (where the upload goes, as with Direct3D)
	_mtl_abSingleMipmap[iUnit] = tpLocal.tp_bSingleMipmap;
	if( iUnit>=GFX_USETEXUNITS) return;
	if( ulTexObject==NONE) {
		mtlSetTextureNone(iUnit);
		return;
	}
	// filtering is global (unit 0), wrapping per unit, as MimicTexParams_OGL() takes them
	const INDEX iFilter = _tpGlobal[0].tp_iFilter;
	const INDEX iMag = iFilter/100;
	const INDEX iMin = iFilter/10 %10;
	INDEX iMip = iFilter%10;
	if( tpLocal.tp_bSingleMipmap) iMip = 0;
	MtlSampler smp;
	smp.ubMinLinear = (iMin==2);
	smp.ubMagLinear = (iMag==0) ? smp.ubMinLinear : (iMag==2);
	smp.ubMip = (UBYTE)iMip;
	smp.ubClampU = (_tpGlobal[iUnit].tp_eWrapU==GFX_CLAMP);
	smp.ubClampV = (_tpGlobal[iUnit].tp_eWrapV==GFX_CLAMP);
	INDEX iAnisotropy = Clamp( _tpGlobal[0].tp_iAnisotropy, 1L, 16L);
	extern INDEX gap_bAggressiveAnisotropy;
	if( tpLocal.tp_bSingleMipmap && !gap_bAggressiveAnisotropy) iAnisotropy = 1;
	smp.ubAnisotropy = (UBYTE)iAnisotropy;
	mtlSetTexture( iUnit, (MtlTexture*)ulTexObject, smp);
	// what the texture has now
	tpLocal.tp_iFilter = _tpGlobal[0].tp_iFilter;
	tpLocal.tp_iAnisotropy = _tpGlobal[0].tp_iAnisotropy;
	tpLocal.tp_eWrapU = _tpGlobal[iUnit].tp_eWrapU;
	tpLocal.tp_eWrapV = _tpGlobal[iUnit].tp_eWrapV;
}

// upload the current unit's texture: 32-bit RGBA mipmaps, one after the other (as UploadTexture_OGL)
static void UploadTexture_MTL( ULONG *pulTexture, PIX pixSizeU, PIX pixSizeV, ULONG ulFormat)
{
	ASSERT( pulTexture!=NULL && pixSizeU>0 && pixSizeV>0);
	const INDEX iUnit = GFX_iActiveTexUnit;
	TEXOBJ *pulTexObject = GFX_apulCurrentTexture[iUnit];
	if( pulTexObject==NULL || *pulTexObject==NONE) return;
	MtlTexture *ptex = (MtlTexture*)*pulTexObject;
	_sfStats.StartTimer( CStatForm::STI_BINDTEXTURE);
	const BOOL bNoMipmaps = _mtl_abSingleMipmap[iUnit];

	// all mipmaps down to 1x1 (those under 1*N or N*1 are made here, as OpenGL needs them)
	INDEX ctMips = 1;
	if( !bNoMipmaps) {
		PIX pixSize = Max( pixSizeU, pixSizeV);
		while( pixSize>1) { pixSize >>= 1;  ctMips++; }
	}
	mtlTextureStorage( ptex, pixSizeU, pixSizeV, ctMips, (MtlTextureFormat)ulFormat);

	INDEX iMip=0;
	PIX pixOffset=0;
	while( pixSizeU>0 && pixSizeV>0)
	{
		mtlTextureUpload( ptex, iMip, pixSizeU, pixSizeV, (const uint32_t*)(pulTexture+pixOffset));
		pixOffset += pixSizeU*pixSizeV;
		pixSizeU >>=1;
		pixSizeV >>=1;
		iMip++;
		if( bNoMipmaps) break;
	}
	if( !bNoMipmaps && pixSizeU!=pixSizeV)
	{
		PIX pixSize = Max(pixSizeU,pixSizeV);
		ASSERT( pixSize<=2048);
		ULONG *pulSrc = pulTexture+pixOffset-pixSize*2;
		ULONG *pulDst = _aulTempRow;
		while( pixSizeU>0 || pixSizeV>0)
		{
			if( pixSizeU==0) pixSizeU=1;
			if( pixSizeV==0) pixSizeV=1;
			pixSize = pixSizeU*pixSizeV;
			MakeSubMipmap( pulSrc, pulDst, pixSize);
			mtlTextureUpload( ptex, iMip, pixSizeU, pixSizeV, (const uint32_t*)pulDst);
			pulSrc     = pulDst;
			pulDst    += pixSize;
			pixOffset += pixSize;
			pixSizeU >>=1;
			pixSizeV >>=1;
			iMip++;
		}
	}

	_sfStats.IncrementCounter( CStatForm::SCI_TEXTUREUPLOADS, 1);
	_sfStats.IncrementCounter( CStatForm::SCI_TEXTUREUPLOADBYTES, pixOffset*4);
	_sfStats.StopTimer( CStatForm::STI_BINDTEXTURE);
}


// upload the current unit's DXT texture: each mipmap is its size (SLONG) and its blocks, as
// UploadCompressedTexture_OGL takes them
static BOOL UploadCompressedTexture_MTL( UBYTE *pubTexture, PIX pixSizeU, PIX pixSizeV, SLONG slSize, ULONG ulFormat)
{
	ASSERT( pubTexture!=NULL && pixSizeU>0 && pixSizeV>0 && slSize>0);
	TEXOBJ *pulTexObject = GFX_apulCurrentTexture[GFX_iActiveTexUnit];
	if( pulTexObject==NULL || *pulTexObject==NONE) return FALSE;
	MtlTexture *ptex = (MtlTexture*)*pulTexObject;
	MtlTextureFormat eFormat = MTLTF_DXT1;
	if( ulFormat==TS.ts_tfRGBcA)  eFormat = MTLTF_DXT3;
	if( ulFormat==TS.ts_tfRGBcAc) eFormat = MTLTF_DXT5;
	_sfStats.StartTimer( CStatForm::STI_BINDTEXTURE);

	// how many mipmaps there are
	INDEX ctMips = 0;
	for( SLONG slRead=0; slRead<slSize; ctMips++) slRead += *(SLONG*)(pubTexture+slRead) + 4;
	mtlTextureStorage( ptex, pixSizeU, pixSizeV, ctMips, eFormat);
	for( INDEX iMip=0; iMip<ctMips; iMip++) {
		const SLONG slMipSize = *(SLONG*)pubTexture;
		pubTexture += 4;
		mtlTextureUploadCompressed( ptex, iMip, pixSizeU, pixSizeV, pubTexture, slMipSize);
		pubTexture += slMipSize;
		pixSizeU = ClampDn( pixSizeU>>1, 1L);
		pixSizeV = ClampDn( pixSizeV>>1, 1L);
	}

	_sfStats.IncrementCounter( CStatForm::SCI_TEXTUREUPLOADS, 1);
	_sfStats.IncrementCounter( CStatForm::SCI_TEXTUREUPLOADBYTES, slSize);
	_sfStats.StopTimer( CStatForm::STI_BINDTEXTURE);
	return TRUE;
}


// VERTEX ARRAYS

static void mtl_SetVertexArray( GFXVertex *pvtx, INDEX ctVtx)
{
	ASSERT( ctVtx>0 && GFX_iActiveTexUnit==0);
	GFX_ctVertices = ctVtx;
	mtlSetStreamData( MTLS_POSITION, pvtx, ctVtx*sizeof(GFXVertex));
	// as OpenGL: the other arrays of unit 0 are off until set again
	mtlClearStream( MTLS_COLOR);
	mtlClearStream( MTLS_NORMAL);
	mtlClearStream( MTLS_TEXCOORD0);
	mtlClearStream( MTLS_WEIGHT);
	mtlClearStream( MTLS_TANGENT);
	_mtl_aubTexCoords[0] = 0;
	GFX_bColorArray = FALSE;
}

static void mtl_SetNormalArray( GFXNormal *pnor)
{
	mtlSetStreamData( MTLS_NORMAL, pnor, GFX_ctVertices*sizeof(GFXNormal));
}

static void mtl_SetWeightArray( GFXWeight *pwgh)
{
	mtlSetStreamData( MTLS_WEIGHT, pwgh, GFX_ctVertices*sizeof(GFXWeight));
}

static void mtl_SetColorArray( GFXColor *pcol)
{
	GFX_bColorArray = TRUE;
	mtlSetStreamData( MTLS_COLOR, pcol, GFX_ctVertices*sizeof(GFXColor));
}

static void mtl_SetTexCoordArray( GFXTexCoord *ptex, BOOL bProjectiveMapping/*=FALSE*/)
{
	const INDEX iUnit = GFX_iActiveTexUnit;
	if( iUnit>=GFX_USETEXUNITS) return;
	const SLONG slSize = bProjectiveMapping ? sizeof(GFXTexCoord4) : sizeof(GFXTexCoord);
	mtlSetStreamData( (MtlStream)(MTLS_TEXCOORD0+iUnit), ptex, GFX_ctVertices*slSize);
	_mtl_aubTexCoords[iUnit] = bProjectiveMapping ? 3 : 1;
}

static void mtl_SetConstantColor( COLOR col)
{
	GFX_bColorArray = FALSE;
	_mtl_colConstant = col;
}

static void mtl_DrawElements( const INDEX ctIndices, const UWORD *puwIndices)
{
	_sfStats.IncrementCounter( CStatForm::SCI_GFXVERTICES, ctIndices);
	_sfStats.StartTimer(CStatForm::STI_GFXAPI);
	_pGfx->gl_ctTotalElements += ctIndices;  // for profiling

	mtl_SyncState();
	// arrays (quads) or elements
	if( puwIndices==NULL) mtlDrawQuads( ctIndices);
	else mtlDrawIndexed( ctIndices, puwIndices);

	_sfStats.StopTimer(CStatForm::STI_GFXAPI);
}

// draw vertices in order as lines or points, with the current state (DrawPort.cpp: what OpenGL
// does there between glBegin and glEnd); colors and texture coordinates may be NULL
extern void MTL_DrawPrimitives( MtlPrimitive ePrimitive, const GFXVertex *pvtx, const GFXColor *pcol,
                                const GFXTexCoord *ptex, INDEX ctVertices, FLOAT fPointSize)
{
	if( ctVertices<=0) return;
	_pGfx->gl_ctTotalElements += ctVertices;
	ASSERT( GFX_iActiveTexUnit==0);
	mtl_SetVertexArray( (GFXVertex*)pvtx, ctVertices);
	if( pcol!=NULL) mtl_SetColorArray( (GFXColor*)pcol);
	if( ptex!=NULL) mtl_SetTexCoordArray( (GFXTexCoord*)ptex, FALSE);
	_mtl_fPointSize = fPointSize;
	mtl_SyncState();
	mtlDrawPrimitives( ePrimitive, ctVertices);
	_mtl_fPointSize = 1.0f;
}


// vertex buffers (as the OpenGL ones: arrays in memory, those of static buffers also on the GPU)

static void *mtl_LockSubBuffer( const INDEX iID, const INDEX i1stVertex, const INDEX ctVertices,
                                const enum GfxLockType eLockType/*=GFX_WRITE*/)
{
	const INDEX iBind = iID >> 4;
	const INDEX iType = iID & 15;
	const ULONG ulTypeMask = 1L << iType;
	ASSERT( iBind<_avbVertexBuffers.Count() && iType<GFX_MAX_VBA);
	VertexBuffer &vb = _avbVertexBuffers[iBind];
	ASSERT( (vb.vb_ulLockMask&ulTypeMask)==0 && (vb.vb_ulArrayMask&ulTypeMask));
	void *pRet;

	// dynamic buffer?
	if( iBind==0) {
		ASSERT( eLockType==GFX_WRITE);
		switch( iType) {
		case GFX_VBA_POS:  _avtxCommon.PopAll();  pRet=(void*)_avtxCommon.Push(ctVertices);  GFX_ctVertices=ctVertices;  break;
		case GFX_VBA_NOR:  _anorCommon.PopAll();  pRet=(void*)_anorCommon.Push(GFX_ctVertices);  break;
		case GFX_VBA_COL:  _acolCommon.PopAll();  pRet=(void*)_acolCommon.Push(GFX_ctVertices);  break;
		case GFX_VBA_TEX:  _atexCommon[GFX_iActiveTexUnit].PopAll();  pRet=(void*)_atexCommon[GFX_iActiveTexUnit].Push(GFX_ctVertices);  break;
		default: ASSERT(FALSE);  pRet = NULL;
		}
	}
	// static buffer?
	else {
		pRet = (UBYTE*)vb.vb_paWriteArray[iType] + GetVBTypeSize(iType)*i1stVertex;
		if( eLockType!=GFX_READ) {
			vb.vb_i1stLockedVertex[iType] = i1stVertex;
			vb.vb_ctLockedVertices[iType] = ctVertices;
		}
	}
	vb.vb_ulLockMask |= ulTypeMask;
	return pRet;
}

static void mtl_UnlockSubBuffer( const INDEX iID, const INDEX ctVertices/*=0*/)
{
	const INDEX iBind = iID >> 4;
	const INDEX iType = iID & 15;
	const ULONG ulTypeMask = 1L << iType;
	ASSERT( iBind<_avbVertexBuffers.Count() && iType<GFX_MAX_VBA);
	VertexBuffer &vb = _avbVertexBuffers[iBind];
	ASSERT( (vb.vb_ulLockMask&ulTypeMask)!=0 && (vb.vb_ulArrayMask&ulTypeMask));

	// dynamic buffer?
	if( iBind==0) {
		if( ctVertices>0) GFX_ctVertices = ctVertices;
		switch( iType) {
		case GFX_VBA_POS: mtl_SetVertexArray(  &_avtxCommon[0], GFX_ctVertices);      break;
		case GFX_VBA_NOR: mtl_SetNormalArray(  &_anorCommon[0]);                      break;
		case GFX_VBA_COL: mtl_SetColorArray(   &_acolCommon[0]);                      break;
		case GFX_VBA_TEX: mtl_SetTexCoordArray(&_atexCommon[GFX_iActiveTexUnit][0], FALSE);  break;
		default: ASSERT(FALSE);
		}
	}
	// static buffer: what was written goes to the GPU
	else {
		ASSERT( ctVertices==0);
		const INDEX ctLocked = vb.vb_ctLockedVertices[iType];
		if( vb.vb_apbufMetal[iType]!=NULL && ctLocked>0) {
			const SLONG slTypeSize = GetVBTypeSize(iType);
			const SLONG slOffset = slTypeSize*vb.vb_i1stLockedVertex[iType];
			mtlBufferUpdate( vb.vb_apbufMetal[iType], slOffset, (UBYTE*)vb.vb_paWriteArray[iType]+slOffset, slTypeSize*ctLocked);
		}
		vb.vb_ctLockedVertices[iType] = 0;
	}
	vb.vb_ulLockMask &= ~ulTypeMask;
}

static INDEX _mtl_iCurrent1stVertex = 0;

// a static buffer's array as a stream: from its GPU buffer, or from memory
static void mtl_SetBufferStream( MtlStream eStream, const VertexBuffer &vb, INDEX iType, INDEX i1stVertex, INDEX ctVertices)
{
	const SLONG slTypeSize = GetVBTypeSize(iType);
	if( vb.vb_apbufMetal[iType]!=NULL) {
		mtlSetStreamBuffer( eStream, vb.vb_apbufMetal[iType], (size_t)slTypeSize*i1stVertex);
	} else {
		mtlSetStreamData( eStream, (UBYTE*)vb.vb_paWriteArray[iType] + slTypeSize*i1stVertex, (size_t)slTypeSize*ctVertices);
	}
}

static void mtl_SetVertexSubBuffer( const INDEX iID, const INDEX i1stVertex/*=0*/, const INDEX ctVertices/*=0*/)
{
	const INDEX iBind = iID >> 4;
	const INDEX iType = iID & 15;
	ASSERT( iType==GFX_VBA_POS && iBind<_avbVertexBuffers.Count());
	VertexBuffer &vb = _avbVertexBuffers[iBind];
	GFX_ctVertices = (ctVertices>0) ? ctVertices : vb.vb_ctVertices;
	// as mtl_SetVertexArray(), from the buffer
	mtl_SetVertexArray( (GFXVertex*)vb.vb_paWriteArray[GFX_VBA_POS], GFX_ctVertices);
	mtl_SetBufferStream( MTLS_POSITION, vb, GFX_VBA_POS, i1stVertex, GFX_ctVertices);
	_mtl_iCurrent1stVertex = i1stVertex;
}

static void mtl_SetSubBuffer( const INDEX iID, const INDEX iUnit/*=-1*/)
{
	const INDEX iBind = iID >> 4;
	const INDEX iType = iID & 15;
	ASSERT( iType>GFX_VBA_POS && iType<GFX_MAX_VBA && iBind<_avbVertexBuffers.Count());
	VertexBuffer &vb = _avbVertexBuffers[iBind];
	const INDEX i1st = _mtl_iCurrent1stVertex;
	switch( iType) {
	case GFX_VBA_NOR:  mtl_SetBufferStream( MTLS_NORMAL,  vb, iType, i1st, GFX_ctVertices);  break;
	case GFX_VBA_WGH:  mtl_SetBufferStream( MTLS_WEIGHT,  vb, iType, i1st, GFX_ctVertices);  break;
	case GFX_VBA_TAN:  mtl_SetBufferStream( MTLS_TANGENT, vb, iType, i1st, GFX_ctVertices);  break;
	case GFX_VBA_COL:
		GFX_bColorArray = TRUE;
		mtl_SetBufferStream( MTLS_COLOR, vb, iType, i1st, GFX_ctVertices);
		break;
	default: {
		// tex coord array (to the given unit, or the current one)
		ASSERT( iType>=GFX_VBA_TEX);
		const INDEX iTexUnit = (iUnit>=0) ? iUnit : GFX_iActiveTexUnit;
		if( iTexUnit>=GFX_USETEXUNITS) break;
		mtl_SetBufferStream( (MtlStream)(MTLS_TEXCOORD0+iTexUnit), vb, iType, i1st, GFX_ctVertices);
		_mtl_aubTexCoords[iTexUnit] = 1;
		} break;
	}
}

static void mtl_Finish( BOOL bReadBack)
{
	mtlFinish();
}

static void mtl_LockArrays(void) { NOTHING; }


// VERTEX AND PIXEL PROGRAMS
// Direct3D's assembly into ARB (ShaderAsmToARB.cpp, as for OpenGL), and that into MSL
// (ShaderARBToMSL.cpp). They are on while set and gfxEnableVertexProgram()/gfxEnablePixelProgram()
// are in effect (mtl_SyncState). LC_DUMP_PROGRAMS=<directory> writes each program's assembly there
// (<hash>.vs or .ps), for the offline test of the translation.

static void mtl_SetVertexProgram( const ULONG ulHandle) { _pGfx->gl_dwVertexShader = ulHandle; }
static void mtl_SetPixelProgram(  const ULONG ulHandle) { _pGfx->gl_dwPixelShader  = ulHandle; }

static void mtl_DeleteProgram( ULONG ulHandle)
{
	MtlProgram *ppr = mtl_Program(ulHandle);
	if( ppr==NULL) return;
	if( _pGfx->gl_dwVertexShader==ulHandle) _pGfx->gl_dwVertexShader = NONE;
	if( _pGfx->gl_dwPixelShader ==ulHandle) _pGfx->gl_dwPixelShader  = NONE;
	mtlDeleteProgram(ppr);
	_mtl_apprPrograms[ulHandle-1] = NULL;
}

// print a program that failed, line by line (the console has a line limit)
static void mtl_PrintProgram( const char *strProgram)
{
	const char *pch = strProgram;
	while( *pch!=0) {
		const char *pchEnd = strchr( pch, '\n');
		const INDEX ctChars = (pchEnd!=NULL) ? (INDEX)(pchEnd-pch) : (INDEX)strlen(pch);
		CPrintF( "  %.*s\n", (int)ctChars, pch);
		pch += ctChars;
		if( *pch=='\n') pch++;
	}
}

// translate and compile a program (NONE if it cannot be done)
static ULONG mtl_CreateProgram( BOOL bVertex, const char *strD3D)
{
	ASSERT( strD3D!=NULL && strlen(strD3D)>0);
	if( !(_pGfx->gl_ulFlags & (bVertex ? GLF_VERTEXPROGRAM : GLF_PIXELPROGRAM))) return NONE;
	// the engine asks again for a program it did not get: report it once
	static std::set<std::string> _setFailed;
	if( _setFailed.count(strD3D)) return NONE;

	const char *strDump = getenv("LC_DUMP_PROGRAMS");
	if( strDump!=NULL) {
		char strFile[1024];
		snprintf( strFile, sizeof(strFile), "%s/%016llx.%s", strDump,
			(unsigned long long)std::hash<std::string>()(strD3D), bVertex ? "vs" : "ps");
		FILE *pf = fopen( strFile, "w");
		if( pf!=NULL) { fputs( strD3D, pf);  fclose(pf); }
	}

	std::string strARB, strMSL, strError;
	const bool bTranslated = (bVertex ? TranslateVertexProgramToARB( strD3D, strARB, strError)
	                                  : TranslatePixelProgramToARB(  strD3D, strARB, strError))
	                      && TranslateARBToMSL( strARB, strMSL, strError);
	if( !bTranslated) {
		CPrintF( "Cannot translate %s program: %s\n", bVertex ? "vertex" : "pixel", strError.c_str());
		mtl_PrintProgram( strD3D);
		_setFailed.insert(strD3D);
		return NONE;
	}
	char strCompileError[4096];
	MtlProgram *ppr = mtlCreateProgram( strMSL.c_str(), bVertex ? "vp" : "pp", strCompileError, sizeof(strCompileError));
	if( ppr==NULL) {
		CPrintF( "Cannot compile %s program: %s\n", bVertex ? "vertex" : "pixel", strCompileError);
		mtl_PrintProgram( strMSL.c_str());
		_setFailed.insert(strD3D);
		return NONE;
	}
	_mtl_apprPrograms.push_back(ppr);
	return (ULONG)_mtl_apprPrograms.size();
}

static ULONG mtl_CreateVertexProgram( const char *strVertexProgram, ULONG ulStreamMasks)
{
	// the inputs are fixed (ShaderARBToMSL.h), so the streams need no declaration
	(void)ulStreamMasks;
	return mtl_CreateProgram( TRUE, strVertexProgram);
}

static ULONG mtl_CreatePixelProgram( const char *strPixelProgram)
{
	return mtl_CreateProgram( FALSE, strPixelProgram);
}

static void mtl_SetVertexProgramConst( INDEX iRegister, const void *pData, INDEX ctRegisters)
{
	ASSERT( iRegister>=0 && ctRegisters>0 && iRegister+ctRegisters<=MSL_VERTEX_CONSTANTS && pData!=NULL);
	memcpy( _mtl_afVertexConst[iRegister], pData, ctRegisters*4*sizeof(FLOAT));
}

// (c0..c7, and the texture stage values above them: ShaderAsmToARB.h)
static void mtl_SetPixelProgramConst( INDEX iRegister, const void *pData, INDEX ctRegisters)
{
	ASSERT( iRegister>=0 && ctRegisters>0 && iRegister+ctRegisters<=MSL_PIXEL_CONSTANTS && pData!=NULL);
	memcpy( _mtl_afPixelConst[iRegister], pData, ctRegisters*4*sizeof(FLOAT));
}

// value of constant registers (for shaGet*ProgramConst)
extern void GetProgramConst_MTL( BOOL bVertex, INDEX iRegister, void *pData, INDEX ctRegisters)
{
	memcpy( pData, bVertex ? _mtl_afVertexConst[iRegister] : _mtl_afPixelConst[iRegister], ctRegisters*4*sizeof(FLOAT));
}


// RENDER TARGETS AND THE FRAME (TextureRender.cpp, DrawPort.cpp, DepthCheck.cpp)
// Metal's render targets start at the top, as Direct3D's: what is drawn into them is the right
// way up for the texture coordinates Direct3D uses.

extern void MTL_MakeRenderTarget( TEXOBJ ulTexture, PIX pixW, PIX pixH)
{
	mtlTextureRenderTarget( (MtlTexture*)ulTexture, pixW, pixH);
}

// draw into a render target (NONE: the window); returns where the draws went before
extern TEXOBJ MTL_SetRenderTarget( TEXOBJ ulTexture)
{
	const TEXOBJ ulOld = (TEXOBJ)mtlGetRenderTarget();
	mtlSetRenderTarget( (MtlTexture*)ulTexture);
	return ulOld;
}

// fill the whole render target (as Direct3D's Clear(): a D3DCOLOR, alpha too, without the masks)
extern void MTL_ClearRenderTarget( PIX pixW, PIX pixH, ULONG ulARGB, FLOAT fDepth)
{
	const float afColor[4] = { ((ulARGB>>16)&0xFF)/255.0f, ((ulARGB>>8)&0xFF)/255.0f, (ulARGB&0xFF)/255.0f, ((ulARGB>>24)&0xFF)/255.0f };
	mtlClear( 0, 0, pixW, pixH, true, afColor, true, fDepth);
}

// copy a rectangle of the frame into a texture (in the pixels drawn, from the top left)
extern void MTL_CopyFrameToTexture( TEXOBJ ulTexture, PIX pixI, PIX pixJ, PIX pixW, PIX pixH)
{
	if( ulTexture==NONE) return;
	mtlCopyFrameToTexture( (MtlTexture*)ulTexture, pixI, pixJ, pixW, pixH);
}

// the frame's 24-bit RGB rows from the top, and depths at points (in the pixels drawn)
extern BOOL MTL_ReadFrameRGB( PIX pixI, PIX pixJ, PIX pixW, PIX pixH, UBYTE *pubRGB)
{
	return mtlReadFrameRGB( pixI, pixJ, pixW, pixH, pubRGB);
}
extern BOOL MTL_ReadFrameDepths( const int *piXY, INDEX ctPoints, FLOAT *pfDepths)
{
	return mtlReadFrameDepths( piXY, ctPoints, pfDepths);
}


// THE BLOOM'S PASSES (RenderBloom.cpp)
// Quads straight to the device, with their own state: the engine's state stays as it was, and its
// streams and textures on the device are put back by MTL_EndFilter().

extern void MTL_BeginFilter(void) { mtlPushState(); }
extern void MTL_EndFilter(void)   { mtlPopState();  }

// the quad from -1,-1 to 1,1 (that the constants c0..c3 put over the viewport) with the texture on
// units 0..3, through the programs and their constants as they are set
extern void MTL_DrawFilterQuad( TEXOBJ ulTexture, BOOL bLinear, ULONG ulVertexProgram, ULONG ulPixelProgram,
                                BOOL bBlend, GfxBlend eSrc, GfxBlend eDst)
{
	MtlProgram *pprVertex = mtl_Program(ulVertexProgram);
	MtlProgram *pprPixel  = mtl_Program(ulPixelProgram);
	if( pprVertex==NULL || pprPixel==NULL || ulTexture==NONE) return;
	// texture coordinates from the top, as Direct3D's
	static const FLOAT afPositions[4][3] = { {-1,-1,0}, {1,-1,0}, {1,1,0}, {-1,1,0} };
	static const FLOAT afTexCoords[4][2] = { {0,1}, {1,1}, {1,0}, {0,0} };
	for( INDEX iStream=0; iStream<MTLS_COUNT; iStream++) mtlClearStream( (MtlStream)iStream);
	mtlSetStreamData( MTLS_POSITION,  afPositions, sizeof(afPositions));
	mtlSetStreamData( MTLS_TEXCOORD0, afTexCoords, sizeof(afTexCoords));
	MtlSampler smp;
	smp.ubMagLinear = smp.ubMinLinear = bLinear ? 1 : 0;
	smp.ubMip = 0;
	smp.ubClampU = smp.ubClampV = 1;
	smp.ubAnisotropy = 1;
	for( INDEX iUnit=0; iUnit<MTL_MAXTEXUNITS; iUnit++) mtlSetTexture( iUnit, (MtlTexture*)ulTexture, smp);

	const PIX pixScale = GFX_DrawableScale();
	mtlSetViewport( _mtl_aiViewport[0]*pixScale, _mtl_aiViewport[1]*pixScale,
	                _mtl_aiViewport[2]*pixScale, _mtl_aiViewport[3]*pixScale, 0.0f, 1.0f);
	mtlSetScissor( _mtl_aiScissor[0]*pixScale, _mtl_aiScissor[1]*pixScale, _mtl_aiScissor[2]*pixScale, _mtl_aiScissor[3]*pixScale);
	mtlSetBlend( bBlend!=FALSE, (MtlBlend)(eSrc-GFX_ONE), (MtlBlend)(eDst-GFX_ONE));
	mtlSetColorMask( true, true, true, true);
	mtlSetDepth( false, false, MTLC_ALWAYS);
	mtlSetCull( MTLCULL_NONE, true);
	mtlSetWireframe( false);
	mtlSetDepthBias( false);
	mtlSetProgram( pprVertex, pprPixel);

	MtlVPConst vc;
	memcpy( vc.afConst, _mtl_afVertexConst, sizeof(vc.afConst));
	vc.afColor[0] = vc.afColor[1] = vc.afColor[2] = vc.afColor[3] = 1.0f;
	vc.ulStreams = (1UL<<MTLS_POSITION) | (1UL<<MTLS_TEXCOORD0);
	vc.ulTexCoords = 0;
	vc.aulPad[0] = vc.aulPad[1] = 0;
	mtlSetVertexConstants( &vc, sizeof(vc));
	MtlPPConst pc;
	memcpy( pc.afConst, _mtl_afPixelConst, sizeof(pc.afConst));
	pc.fAlphaRef  = 0.5f;
	pc.bAlphaTest = 0;
	pc.fLODBias   = 0.0f;
	pc.ulPad = 0;
	mtlSetFragmentConstants( &pc, sizeof(pc));
	mtlDrawQuads( 4);
}
