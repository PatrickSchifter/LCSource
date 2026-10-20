// The gfx* functions for the APIs without fixed function (Metal, Vulkan), on top of a GpuDevice
// (GpuDevice.h): the same for all of them, the device is the API's. Included by Gfx_wrapper.cpp
// like the OpenGL and Direct3D ones.
//
// What OpenGL does with its fixed function here (vertex arrays, projection and view matrices,
// texture matrices and units with MODULATE or 2X combine, constant color or color array, alpha
// test, user clip plane, point size) is a program of the device's (CreateFixedFunction), whose
// constants are made from the engine's state at every draw. The other state (blending, depth,
// culling, viewport...) also goes to the device at every draw; the enable/disable functions only
// keep it in the GFX_* variables.
//
// Matrices are those of Direct3D (depth 0..1), without its half pixel offset (the devices sample
// at pixel centers like OpenGL). Viewports and scissors are given from the top left in the
// engine's pixels (window points), times GFX_DrawableScale() on the device.

#include <Engine/Graphics/GpuDevice.h>
#include <Engine/Graphics/ShaderAsmToARB.h>
#include <set>
#include <string>
#include <vector>

extern PIX GFX_DrawableScale(void);   // GfxLibrary.cpp
extern ULONG _aulTempRow[4096];       // Graphics.cpp

const GpuDevice *_pgpu = NULL;

static GpuProgram *_pprFFVertex   = NULL;
static GpuProgram *_pprFFFragment = NULL;

// state of the emulated fixed function
static FLOAT _gpu_afProjection[16];
static FLOAT _gpu_afView[16];
static FLOAT _gpu_aafTexture[GFX_USETEXUNITS][16];
static ULONG _gpu_ulTexMatrices = 0;                  // units with a texture matrix (bits)
static ULONG _gpu_ulTexGenView = 0;                   // units whose coordinates are the view space position (bits)
static UBYTE _gpu_aubTexCoords[GFX_USETEXUNITS];      // per unit: 1=array, 2=s,t,r,q
static FLOAT _gpu_afClipPlane[4];
static COLOR _gpu_colConstant = C_WHITE|CT_OPAQUE;
static FLOAT _gpu_fPointSize = 1.0f;
static BOOL  _gpu_bWireframe = FALSE;
static BOOL  _gpu_bDepthBias = FALSE;
static BOOL  _gpu_abSingleMipmap[GFX_MAXTEXUNITS];    // of the texture set on each unit
static BOOL  _gpu_bShadowMapBlend = FALSE;            // the terrain's (GPU_SetShadowMapBlend)
static FLOAT _gpu_fShadowMapBlend = 0;
// in the engine's pixels, from the top left of the render target
static PIX _gpu_aiViewport[4] = {0, 0, 1, 1};
static PIX _gpu_aiScissor[4]  = {0, 0, 1, 1};

static const FLOAT _afIdentity[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };

// vertex and pixel programs (Direct3D's assembly translated thru ARB into the device's): a handle is an
// index+1 into _gpu_apprPrograms; the constants of both kinds are kept here and go with each draw
static std::vector<GpuProgram*> _gpu_apprPrograms;
static FLOAT _gpu_afVertexConst[GPU_VERTEX_CONSTANTS][4];
static FLOAT _gpu_afPixelConst[GPU_PIXEL_CONSTANTS][4];

static GpuProgram *gpu_Program( ULONG ulHandle)
{
	if( ulHandle==NONE || ulHandle>_gpu_apprPrograms.size()) return NULL;
	return _gpu_apprPrograms[ulHandle-1];
}

// called on a new context
extern BOOL InitFixedFunction_GPU(void)
{
	char strError[2048];
	_pprFFVertex   = _pgpu->CreateFixedFunction( true, strError, sizeof(strError));
	if( _pprFFVertex==NULL) CPrintF( "%s: fixed function vertex program: %s\n", _pgpu->strAPI, strError);
	_pprFFFragment = _pgpu->CreateFixedFunction( false, strError, sizeof(strError));
	if( _pprFFFragment==NULL) CPrintF( "%s: fixed function fragment program: %s\n", _pgpu->strAPI, strError);
	memcpy( _gpu_afProjection, _afIdentity, sizeof(_afIdentity));
	memcpy( _gpu_afView, _afIdentity, sizeof(_afIdentity));
	for( INDEX iUnit=0; iUnit<GFX_USETEXUNITS; iUnit++) {
		memcpy( _gpu_aafTexture[iUnit], _afIdentity, sizeof(_afIdentity));
		_gpu_aubTexCoords[iUnit] = 0;
	}
	for( INDEX iUnit=0; iUnit<GFX_MAXTEXUNITS; iUnit++) _gpu_abSingleMipmap[iUnit] = FALSE;
	_gpu_ulTexMatrices = 0;
	_gpu_ulTexGenView = 0;
	_gpu_afClipPlane[0] = _gpu_afClipPlane[1] = _gpu_afClipPlane[2] = _gpu_afClipPlane[3] = 0;
	_gpu_colConstant = C_WHITE|CT_OPAQUE;
	_gpu_fPointSize = 1.0f;
	_gpu_bWireframe = FALSE;
	_gpu_bDepthBias = FALSE;
	_gpu_bShadowMapBlend = FALSE;
	memset( _gpu_afVertexConst, 0, sizeof(_gpu_afVertexConst));
	memset( _gpu_afPixelConst, 0, sizeof(_gpu_afPixelConst));
	return _pprFFVertex!=NULL && _pprFFFragment!=NULL;
}

extern void EndFixedFunction_GPU(void)
{
	if( _pprFFVertex!=NULL)   _pgpu->DeleteProgram(_pprFFVertex);
	if( _pprFFFragment!=NULL) _pgpu->DeleteProgram(_pprFFFragment);
	_pprFFVertex = _pprFFFragment = NULL;
	// (the engine has deleted its programs by now: ClearVertexAndPixelPrograms())
	for( size_t i=0; i<_gpu_apprPrograms.size(); i++) {
		if( _gpu_apprPrograms[i]!=NULL) _pgpu->DeleteProgram(_gpu_apprPrograms[i]);
	}
	_gpu_apprPrograms.clear();
}


// VIEWPORT, SCISSOR AND CLEARING (for GfxLibrary.cpp and DrawPort.cpp)

extern void GPU_SetViewport( PIX pixI, PIX pixJ, PIX pixW, PIX pixH)
{
	_gpu_aiViewport[0] = pixI;  _gpu_aiViewport[1] = pixJ;
	_gpu_aiViewport[2] = pixW;  _gpu_aiViewport[3] = pixH;
}

extern void GPU_SetScissor( PIX pixI, PIX pixJ, PIX pixW, PIX pixH)
{
	_gpu_aiScissor[0] = pixI;  _gpu_aiScissor[1] = pixJ;
	_gpu_aiScissor[2] = pixW;  _gpu_aiScissor[3] = pixH;
}

// both, as x,y,w,h (for CRenderTexture, which puts them back)
extern void GPU_GetViewportAndScissor( PIX *ppixViewport, PIX *ppixScissor)
{
	memcpy( ppixViewport, _gpu_aiViewport, sizeof(_gpu_aiViewport));
	memcpy( ppixScissor,  _gpu_aiScissor,  sizeof(_gpu_aiScissor));
}

// fill a rectangle with a color and/or a depth (as glClear in a scissor, without the masks)
extern void GPU_Clear( PIX pixI, PIX pixJ, PIX pixW, PIX pixH, BOOL bColor, COLOR col, BOOL bDepth, FLOAT fDepth)
{
	const PIX pixScale = GFX_DrawableScale();
	UBYTE ubR, ubG, ubB, ubA;
	ColorToRGBA( col, ubR, ubG, ubB, ubA);
	const float afColor[4] = { ubR/255.0f, ubG/255.0f, ubB/255.0f, 1.0f };
	_pgpu->Clear( pixI*pixScale, pixJ*pixScale, pixW*pixScale, pixH*pixScale, bColor!=FALSE, afColor, bDepth!=FALSE, fDepth);
}


// the state of the engine into the device, for a draw
static void gpu_SyncState(void)
{
	const PIX pixScale = GFX_DrawableScale();
	_pgpu->SetViewport( _gpu_aiViewport[0]*pixScale, _gpu_aiViewport[1]*pixScale,
	                _gpu_aiViewport[2]*pixScale, _gpu_aiViewport[3]*pixScale, GFX_fMinDepthRange, GFX_fMaxDepthRange);
	_pgpu->SetScissor( _gpu_aiScissor[0]*pixScale, _gpu_aiScissor[1]*pixScale, _gpu_aiScissor[2]*pixScale, _gpu_aiScissor[3]*pixScale);
	_pgpu->SetBlend( GFX_bBlending!=FALSE, (GpuBlend)(GFX_eBlendSrc-GFX_ONE), (GpuBlend)(GFX_eBlendDst-GFX_ONE));
	const ULONG ulMask = GFX_ulCurrentColorMask;
	_pgpu->SetColorMask( (ulMask&CT_RMASK)==CT_RMASK, (ulMask&CT_GMASK)==CT_GMASK, (ulMask&CT_BMASK)==CT_BMASK, (ulMask&CT_AMASK)==CT_AMASK);
	_pgpu->SetDepth( GFX_bDepthTest!=FALSE, GFX_bDepthWrite!=FALSE, (GpuCompare)(GFX_eDepthFunc-GFX_NEVER));
	const GpuCull eCull = GFX_eCullFace==GFX_FRONT ? GPUCULL_FRONT : GFX_eCullFace==GFX_BACK ? GPUCULL_BACK : GPUCULL_NONE;
	_pgpu->SetCull( eCull, GFX_bFrontFace!=FALSE);
	_pgpu->SetWireframe( _gpu_bWireframe!=FALSE);
	_pgpu->SetDepthBias( _gpu_bDepthBias!=FALSE);

	// programs, where the engine has them on (else the fixed function emulation)
	GpuProgram *pprVertex = NULL, *pprPixel = NULL;
	if( _pGfx->gl_ulFlags&GLF_VERTEXPROGRAM) {
		if( GFX_bUseVertexProgram) pprVertex = gpu_Program(_pGfx->gl_dwVertexShader);
		if( GFX_bUsePixelProgram)  pprPixel  = gpu_Program(_pGfx->gl_dwPixelShader);
	}
	_pgpu->SetProgram( pprVertex!=NULL ? pprVertex : _pprFFVertex, pprPixel!=NULL ? pprPixel : _pprFFFragment);

	if( pprVertex!=NULL) {
		GpuVPConst vc;
		memcpy( vc.afConst, _gpu_afVertexConst, sizeof(vc.afConst));
		UBYTE ubR, ubG, ubB, ubA;
		ColorToRGBA( _gpu_colConstant, ubR, ubG, ubB, ubA);
		vc.afColor[0] = ubR/255.0f;  vc.afColor[1] = ubG/255.0f;
		vc.afColor[2] = ubB/255.0f;  vc.afColor[3] = ubA/255.0f;
		// (a color array that is off, as with OpenGL, is the constant color)
		vc.ulStreams = _pgpu->GetStreamMask();
		if( !GFX_bColorArray) vc.ulStreams &= ~(1UL<<GPUS_COLOR);
		vc.ulTexCoords = 0;
		for( INDEX iUnit=0; iUnit<GFX_USETEXUNITS; iUnit++) vc.ulTexCoords |= (ULONG)_gpu_aubTexCoords[iUnit]<<(iUnit*4);
		vc.aulPad[0] = vc.aulPad[1] = 0;
		_pgpu->SetVertexConstants( &vc, sizeof(vc));
	} else {
		GpuFFVertexConst vc;
		memcpy( vc.afProjection, _gpu_afProjection, sizeof(vc.afProjection));
		memcpy( vc.afView, _gpu_afView, sizeof(vc.afView));
		ULONG ulTexCoords = 0;
		for( INDEX iUnit=0; iUnit<GFX_USETEXUNITS; iUnit++) {
			memcpy( vc.aafTexture[iUnit], _gpu_aafTexture[iUnit], sizeof(vc.aafTexture[iUnit]));
			ULONG ulMode = _gpu_aubTexCoords[iUnit];
			if( _gpu_ulTexMatrices&(1UL<<iUnit)) ulMode |= 4;
			if( _gpu_ulTexGenView &(1UL<<iUnit)) ulMode |= 8;
			ulTexCoords |= ulMode<<(iUnit*4);
		}
		memcpy( vc.afClipPlane, _gpu_afClipPlane, sizeof(vc.afClipPlane));
		UBYTE ubR, ubG, ubB, ubA;
		ColorToRGBA( _gpu_colConstant, ubR, ubG, ubB, ubA);
		vc.afColor[0] = ubR/255.0f;  vc.afColor[1] = ubG/255.0f;
		vc.afColor[2] = ubB/255.0f;  vc.afColor[3] = ubA/255.0f;
		vc.ulFlags = (GFX_bColorArray ? 1 : 0) | (GFX_bClipPlane ? 2 : 0);
		vc.ulTexCoords = ulTexCoords;
		vc.fPointSize  = _gpu_fPointSize*pixScale;
		vc.ulPad = 0;
		_pgpu->SetVertexConstants( &vc, sizeof(vc));
	}

	if( pprPixel!=NULL) {
		GpuPPConst pc;
		memcpy( pc.afConst, _gpu_afPixelConst, sizeof(pc.afConst));
		pc.fAlphaRef  = 0.5f;
		pc.bAlphaTest = GFX_bAlphaTest ? 1 : 0;
		pc.fLODBias   = _fCurrentLODBias;
		pc.ulPad = 0;
		_pgpu->SetFragmentConstants( &pc, sizeof(pc));
		return;
	}
	GpuFFFragmentConst fc;
	for( INDEX iUnit=0; iUnit<4; iUnit++) {
		fc.aulTexMode[iUnit] = 0;
		if( iUnit<_pGfx->gl_ctTextureUnits && GFX_abTexture[iUnit]) {
			fc.aulTexMode[iUnit] = (GFX_iTexModulation[iUnit]==2) ? 2 : 1;
		}
	}
	fc.fFactor = 0;
	if( _gpu_bShadowMapBlend) {
		fc.aulTexMode[0] = 3;
		fc.aulTexMode[1] = 4;
		fc.fFactor = _gpu_fShadowMapBlend;
	}
	fc.fAlphaRef  = 0.5f;  // glAlphaFunc( GL_GEQUAL, 0.5f)
	fc.bAlphaTest = GFX_bAlphaTest ? 1 : 0;
	fc.fLODBias   = _fCurrentLODBias;
	_pgpu->SetFragmentConstants( &fc, sizeof(fc));
}


// ENABLE/DISABLE FUNCTIONS (the state goes to the device at the next draw)

static void gpu_EnableTexture(void)     { GFX_abTexture[GFX_iActiveTexUnit] = TRUE; }
static void gpu_DisableTexture(void)
{
	GFX_abTexture[GFX_iActiveTexUnit] = FALSE;
	// as OpenGL, which disables the unit's texture coordinate array too
	if( GFX_iActiveTexUnit<GFX_USETEXUNITS) {
		_pgpu->ClearStream( (GpuStream)(GPUS_TEXCOORD0+GFX_iActiveTexUnit));
		_gpu_aubTexCoords[GFX_iActiveTexUnit] = 0;
	}
}
static void gpu_EnableDepthTest(void)   { GFX_bDepthTest  = TRUE;  }
static void gpu_DisableDepthTest(void)  { GFX_bDepthTest  = FALSE; }
static void gpu_EnableDepthBias(void)   { _gpu_bDepthBias = TRUE;  }
static void gpu_DisableDepthBias(void)  { _gpu_bDepthBias = FALSE; }
static void gpu_EnableDepthWrite(void)  { GFX_bDepthWrite = TRUE;  }
static void gpu_DisableDepthWrite(void) { GFX_bDepthWrite = FALSE; }
static void gpu_EnableDither(void)      { GFX_bDithering  = TRUE;  }
static void gpu_DisableDither(void)     { GFX_bDithering  = FALSE; }
static void gpu_EnableAlphaTest(void)   { GFX_bAlphaTest  = TRUE;  }
static void gpu_DisableAlphaTest(void)  { GFX_bAlphaTest  = FALSE; }
static void gpu_EnableBlend(void)       { GFX_bBlending   = TRUE;  }
static void gpu_DisableBlend(void)      { GFX_bBlending   = FALSE; }
static void gpu_EnableLighting(void)    { GFX_bLighting   = TRUE;  }  // (the engine draws nothing with it)
static void gpu_DisableLighting(void)   { GFX_bLighting   = FALSE; }
static void gpu_EnableClipping(void)    { GFX_bClipping   = TRUE;  }
static void gpu_DisableClipping(void)   { GFX_bClipping   = FALSE; }
static void gpu_EnableClipPlane(void)   { GFX_bClipPlane  = TRUE;  }
static void gpu_DisableClipPlane(void)  { GFX_bClipPlane  = FALSE; }
static void gpu_EnableColorArray(void)  { GFX_bColorArray = TRUE;  }
static void gpu_DisableColorArray(void) { GFX_bColorArray = FALSE; }
static void gpu_EnableTruform(void)     { NOTHING; }
static void gpu_DisableTruform(void)    { NOTHING; }
static void gpu_EnableLight( INDEX iLight)  { GFX_abLights[iLight] = TRUE;  }
static void gpu_DisableLight( INDEX iLight) { GFX_abLights[iLight] = FALSE; }
static void gpu_SetLightColor( INDEX iLight, COLOR colLight, COLOR colAmbient, COLOR colSpecular) { NOTHING; }
static void gpu_SetLightDirection( INDEX iLight, const FLOAT3D &vLightDir) { NOTHING; }

static void gpu_BlendFunc( GfxBlend eSrc, GfxBlend eDst)
{
	GFX_eBlendSrc = eSrc;
	GFX_eBlendDst = eDst;
}

static void gpu_SetColorMask( ULONG ulColorMask) { GFX_ulCurrentColorMask = ulColorMask; }
static void gpu_DepthFunc( GfxComp eFunc)       { GFX_eDepthFunc = eFunc; }

static void gpu_DepthRange( FLOAT fMin, FLOAT fMax)
{
	GFX_fMinDepthRange = fMin;
	GFX_fMaxDepthRange = fMax;
}

static void gpu_CullFace( GfxFace eFace)
{
	ASSERT( eFace==GFX_FRONT || eFace==GFX_BACK || eFace==GFX_NONE);
	GFX_eCullFace = eFace;
}

static void gpu_FrontFace( GfxFace eFace)
{
	ASSERT( eFace==GFX_CW || eFace==GFX_CCW);
	GFX_bFrontFace = (eFace==GFX_CCW);
}

// the plane is in view space (as Direct3D takes it)
static void gpu_ClipPlane( const DOUBLE *pdViewPlane)
{
	for( INDEX i=0; i<4; i++) _gpu_afClipPlane[i] = (FLOAT)pdViewPlane[i];
}

static void gpu_SetTextureMatrix( const FLOAT *pfMatrix/*=NULL*/)
{
	const INDEX iUnit = GFX_iActiveTexUnit;
	if( iUnit>=GFX_USETEXUNITS) return;
	if( pfMatrix!=NULL) {
		memcpy( _gpu_aafTexture[iUnit], pfMatrix, 16*sizeof(FLOAT));
		_gpu_ulTexMatrices |= 1UL<<iUnit;
	} else {
		_gpu_ulTexMatrices &= ~(1UL<<iUnit);
	}
}

static void gpu_SetViewMatrix( const FLOAT *pfMatrix/*=NULL*/)
{
	GFX_bViewMatrix = (pfMatrix!=NULL);
	memcpy( _gpu_afView, pfMatrix!=NULL ? pfMatrix : _afIdentity, 16*sizeof(FLOAT));
}

// as d3d_SetOrtho(), without the half pixel offset
static void gpu_SetOrtho( FLOAT fLeft, FLOAT fRight, FLOAT fTop, FLOAT fBottom, FLOAT fNear, FLOAT fFar,
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
	memcpy( _gpu_afProjection, afMatrix, sizeof(afMatrix));
}

// as d3d_SetFrustum()
static void gpu_SetFrustum( FLOAT fLeft, FLOAT fRight, FLOAT fTop, FLOAT fBottom, FLOAT fNear, FLOAT fFar)
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
	memcpy( _gpu_afProjection, afMatrix, sizeof(afMatrix));
}

// current projection and view matrices (Direct3D's depth range, Shader.cpp and Render.cpp), and
// a projection given as for Direct3D
extern void GetProjectionMatrix_GPU( FLOAT *pfMatrix) { memcpy( pfMatrix, _gpu_afProjection, 16*sizeof(FLOAT)); }
extern void GetViewMatrix_GPU( FLOAT *pfMatrix)       { memcpy( pfMatrix, _gpu_afView, 16*sizeof(FLOAT)); }
extern void SetProjectionMatrix_GPU( const FLOAT *pfMatrix)
{
	memcpy( _gpu_afProjection, pfMatrix, 16*sizeof(FLOAT));
	GFX_fLastL = GFX_fLastR = GFX_fLastT = GFX_fLastB = GFX_fLastN = GFX_fLastF = 0;  // (no cached ortho)
}

// texture coordinates of the current unit from the position in view space, through its texture
// matrix and divided by q: Direct3D's D3DTSS_TCI_CAMERASPACEPOSITION with D3DTTFF_COUNT4|D3DTTFF_PROJECTED
// (the projected shadows, Render.cpp; the render targets start at the top as Direct3D's)
extern void GPU_SetTexGenViewPosition( BOOL bOn)
{
	const INDEX iUnit = GFX_iActiveTexUnit;
	if( iUnit>=GFX_USETEXUNITS) return;
	if( bOn) _gpu_ulTexGenView |=  (1UL<<iUnit);
	else     _gpu_ulTexGenView &= ~(1UL<<iUnit);
}

// the terrain's shadow map blend without programs: unit 0 takes the current shadow map, unit 1
// goes to the next one by the factor (as SetShadowMapBlend_OGL in TRRender.cpp)
extern void GPU_SetShadowMapBlend( FLOAT fBlendFactor)
{
	_gpu_bShadowMapBlend = TRUE;
	_gpu_fShadowMapBlend = fBlendFactor;
}
extern void GPU_ResetShadowMapBlend(void) { _gpu_bShadowMapBlend = FALSE; }

static void gpu_PolygonMode( GfxPolyMode ePolyMode)
{
	// (no point mode for triangles: lines instead)
	_gpu_bWireframe = (ePolyMode!=GFX_FILL);
}


// TEXTURES

static void gpu_SetTextureWrapping( enum GfxWrap eWrapU, enum GfxWrap eWrapV)
{
	_tpGlobal[GFX_iActiveTexUnit].tp_eWrapU = eWrapU;
	_tpGlobal[GFX_iActiveTexUnit].tp_eWrapV = eWrapV;
	// on the texture set now too, as OpenGL's glTexParameteri() and Direct3D's stage state
	if( GFX_iActiveTexUnit<GFX_USETEXUNITS) _pgpu->SetTextureWrap( GFX_iActiveTexUnit, eWrapU==GFX_CLAMP, eWrapV==GFX_CLAMP);
}

static void gpu_SetTextureModulation( INDEX iScale)
{
	ASSERT( iScale==1 || iScale==2);
	GFX_iTexModulation[GFX_iActiveTexUnit] = iScale;
}

static void gpu_GenerateTexture( TEXOBJ &ulTexObject)
{
	ulTexObject = (TEXOBJ)_pgpu->CreateTexture();
}

static void gpu_DeleteTexture( TEXOBJ &ulTexObject)
{
	if( ulTexObject==NONE) return;
	MEMTRACK_FREE( (void*)(ulTexObject^0x80000000));
	_pgpu->DeleteTexture( (GpuTexture*)ulTexObject);
	ulTexObject = NONE;
}

// set a texture on the current unit, with the sampling the engine asks for (gfxSetTexture)
static void SetTexture_GPU( TEXOBJ &ulTexObject, CTexParams &tpLocal)
{
	const INDEX iUnit = GFX_iActiveTexUnit;
	GFX_apulCurrentTexture[iUnit] = &ulTexObject;  // (where the upload goes, as with Direct3D)
	_gpu_abSingleMipmap[iUnit] = tpLocal.tp_bSingleMipmap;
	if( iUnit>=GFX_USETEXUNITS) return;
	if( ulTexObject==NONE) {
		_pgpu->SetTextureNone(iUnit);
		return;
	}
	// filtering is global (unit 0), wrapping per unit, as MimicTexParams_OGL() takes them
	const INDEX iFilter = _tpGlobal[0].tp_iFilter;
	const INDEX iMag = iFilter/100;
	const INDEX iMin = iFilter/10 %10;
	INDEX iMip = iFilter%10;
	if( tpLocal.tp_bSingleMipmap) iMip = 0;
	GpuSampler smp;
	smp.ubMinLinear = (iMin==2);
	smp.ubMagLinear = (iMag==0) ? smp.ubMinLinear : (iMag==2);
	smp.ubMip = (UBYTE)iMip;
	smp.ubClampU = (_tpGlobal[iUnit].tp_eWrapU==GFX_CLAMP);
	smp.ubClampV = (_tpGlobal[iUnit].tp_eWrapV==GFX_CLAMP);
	INDEX iAnisotropy = Clamp( _tpGlobal[0].tp_iAnisotropy, 1L, 16L);
	extern INDEX gap_bAggressiveAnisotropy;
	if( tpLocal.tp_bSingleMipmap && !gap_bAggressiveAnisotropy) iAnisotropy = 1;
	smp.ubAnisotropy = (UBYTE)iAnisotropy;
	_pgpu->SetTexture( iUnit, (GpuTexture*)ulTexObject, smp);
	// what the texture has now
	tpLocal.tp_iFilter = _tpGlobal[0].tp_iFilter;
	tpLocal.tp_iAnisotropy = _tpGlobal[0].tp_iAnisotropy;
	tpLocal.tp_eWrapU = _tpGlobal[iUnit].tp_eWrapU;
	tpLocal.tp_eWrapV = _tpGlobal[iUnit].tp_eWrapV;
}

// upload the current unit's texture: 32-bit RGBA mipmaps, one after the other (as UploadTexture_OGL)
static void UploadTexture_GPU( ULONG *pulTexture, PIX pixSizeU, PIX pixSizeV, ULONG ulFormat)
{
	ASSERT( pulTexture!=NULL && pixSizeU>0 && pixSizeV>0);
	const INDEX iUnit = GFX_iActiveTexUnit;
	TEXOBJ *pulTexObject = GFX_apulCurrentTexture[iUnit];
	if( pulTexObject==NULL || *pulTexObject==NONE) return;
	GpuTexture *ptex = (GpuTexture*)*pulTexObject;
	_sfStats.StartTimer( CStatForm::STI_BINDTEXTURE);
	const BOOL bNoMipmaps = _gpu_abSingleMipmap[iUnit];

	// all mipmaps down to 1x1 (those under 1*N or N*1 are made here, as OpenGL needs them)
	INDEX ctMips = 1;
	if( !bNoMipmaps) {
		PIX pixSize = Max( pixSizeU, pixSizeV);
		while( pixSize>1) { pixSize >>= 1;  ctMips++; }
	}
	_pgpu->TextureStorage( ptex, pixSizeU, pixSizeV, ctMips, (GpuTextureFormat)ulFormat);

	INDEX iMip=0;
	PIX pixOffset=0;
	while( pixSizeU>0 && pixSizeV>0)
	{
		_pgpu->TextureUpload( ptex, iMip, pixSizeU, pixSizeV, (const uint32_t*)(pulTexture+pixOffset));
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
			_pgpu->TextureUpload( ptex, iMip, pixSizeU, pixSizeV, (const uint32_t*)pulDst);
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
static BOOL UploadCompressedTexture_GPU( UBYTE *pubTexture, PIX pixSizeU, PIX pixSizeV, SLONG slSize, ULONG ulFormat)
{
	ASSERT( pubTexture!=NULL && pixSizeU>0 && pixSizeV>0 && slSize>0);
	TEXOBJ *pulTexObject = GFX_apulCurrentTexture[GFX_iActiveTexUnit];
	if( pulTexObject==NULL || *pulTexObject==NONE) return FALSE;
	GpuTexture *ptex = (GpuTexture*)*pulTexObject;
	GpuTextureFormat eFormat = GPUTF_DXT1;
	if( ulFormat==TS.ts_tfRGBcA)  eFormat = GPUTF_DXT3;
	if( ulFormat==TS.ts_tfRGBcAc) eFormat = GPUTF_DXT5;
	_sfStats.StartTimer( CStatForm::STI_BINDTEXTURE);

	// how many mipmaps there are
	INDEX ctMips = 0;
	for( SLONG slRead=0; slRead<slSize; ctMips++) slRead += *(SLONG*)(pubTexture+slRead) + 4;
	_pgpu->TextureStorage( ptex, pixSizeU, pixSizeV, ctMips, eFormat);
	for( INDEX iMip=0; iMip<ctMips; iMip++) {
		const SLONG slMipSize = *(SLONG*)pubTexture;
		pubTexture += 4;
		_pgpu->TextureUploadCompressed( ptex, iMip, pixSizeU, pixSizeV, pubTexture, slMipSize);
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

static void gpu_SetVertexArray( GFXVertex *pvtx, INDEX ctVtx)
{
	ASSERT( ctVtx>0 && GFX_iActiveTexUnit==0);
	GFX_ctVertices = ctVtx;
	_pgpu->SetStreamData( GPUS_POSITION, pvtx, ctVtx*sizeof(GFXVertex));
	// as OpenGL: the other arrays of unit 0 are off until set again
	_pgpu->ClearStream( GPUS_COLOR);
	_pgpu->ClearStream( GPUS_NORMAL);
	_pgpu->ClearStream( GPUS_TEXCOORD0);
	_pgpu->ClearStream( GPUS_WEIGHT);
	_pgpu->ClearStream( GPUS_TANGENT);
	_gpu_aubTexCoords[0] = 0;
	GFX_bColorArray = FALSE;
}

static void gpu_SetNormalArray( GFXNormal *pnor)
{
	_pgpu->SetStreamData( GPUS_NORMAL, pnor, GFX_ctVertices*sizeof(GFXNormal));
}

static void gpu_SetWeightArray( GFXWeight *pwgh)
{
	_pgpu->SetStreamData( GPUS_WEIGHT, pwgh, GFX_ctVertices*sizeof(GFXWeight));
}

static void gpu_SetColorArray( GFXColor *pcol)
{
	GFX_bColorArray = TRUE;
	_pgpu->SetStreamData( GPUS_COLOR, pcol, GFX_ctVertices*sizeof(GFXColor));
}

static void gpu_SetTexCoordArray( GFXTexCoord *ptex, BOOL bProjectiveMapping/*=FALSE*/)
{
	const INDEX iUnit = GFX_iActiveTexUnit;
	if( iUnit>=GFX_USETEXUNITS) return;
	const SLONG slSize = bProjectiveMapping ? sizeof(GFXTexCoord4) : sizeof(GFXTexCoord);
	_pgpu->SetStreamData( (GpuStream)(GPUS_TEXCOORD0+iUnit), ptex, GFX_ctVertices*slSize);
	_gpu_aubTexCoords[iUnit] = bProjectiveMapping ? 3 : 1;
}

static void gpu_SetConstantColor( COLOR col)
{
	GFX_bColorArray = FALSE;
	_gpu_colConstant = col;
}

static void gpu_DrawElements( const INDEX ctIndices, const UWORD *puwIndices)
{
	_sfStats.IncrementCounter( CStatForm::SCI_GFXVERTICES, ctIndices);
	_sfStats.StartTimer(CStatForm::STI_GFXAPI);
	_pGfx->gl_ctTotalElements += ctIndices;  // for profiling

	gpu_SyncState();
	// arrays (quads) or elements
	if( puwIndices==NULL) _pgpu->DrawQuads( ctIndices);
	else _pgpu->DrawIndexed( ctIndices, puwIndices);

	_sfStats.StopTimer(CStatForm::STI_GFXAPI);
}

// draw vertices in order as lines or points, with the current state (DrawPort.cpp: what OpenGL
// does there between glBegin and glEnd); colors and texture coordinates may be NULL
extern void GPU_DrawPrimitives( GpuPrimitive ePrimitive, const GFXVertex *pvtx, const GFXColor *pcol,
                                const GFXTexCoord *ptex, INDEX ctVertices, FLOAT fPointSize)
{
	if( ctVertices<=0) return;
	_pGfx->gl_ctTotalElements += ctVertices;
	ASSERT( GFX_iActiveTexUnit==0);
	gpu_SetVertexArray( (GFXVertex*)pvtx, ctVertices);
	if( pcol!=NULL) gpu_SetColorArray( (GFXColor*)pcol);
	if( ptex!=NULL) gpu_SetTexCoordArray( (GFXTexCoord*)ptex, FALSE);
	_gpu_fPointSize = fPointSize;
	gpu_SyncState();
	_pgpu->DrawPrimitives( ePrimitive, ctVertices);
	_gpu_fPointSize = 1.0f;
}


// vertex buffers (as the OpenGL ones: arrays in memory, those of static buffers also on the GPU)

static void *gpu_LockSubBuffer( const INDEX iID, const INDEX i1stVertex, const INDEX ctVertices,
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

static void gpu_UnlockSubBuffer( const INDEX iID, const INDEX ctVertices/*=0*/)
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
		case GFX_VBA_POS: gpu_SetVertexArray(  &_avtxCommon[0], GFX_ctVertices);      break;
		case GFX_VBA_NOR: gpu_SetNormalArray(  &_anorCommon[0]);                      break;
		case GFX_VBA_COL: gpu_SetColorArray(   &_acolCommon[0]);                      break;
		case GFX_VBA_TEX: gpu_SetTexCoordArray(&_atexCommon[GFX_iActiveTexUnit][0], FALSE);  break;
		default: ASSERT(FALSE);
		}
	}
	// static buffer: what was written goes to the GPU
	else {
		ASSERT( ctVertices==0);
		const INDEX ctLocked = vb.vb_ctLockedVertices[iType];
		if( vb.vb_apbufGpu[iType]!=NULL && ctLocked>0) {
			const SLONG slTypeSize = GetVBTypeSize(iType);
			const SLONG slOffset = slTypeSize*vb.vb_i1stLockedVertex[iType];
			_pgpu->BufferUpdate( vb.vb_apbufGpu[iType], slOffset, (UBYTE*)vb.vb_paWriteArray[iType]+slOffset, slTypeSize*ctLocked);
		}
		vb.vb_ctLockedVertices[iType] = 0;
	}
	vb.vb_ulLockMask &= ~ulTypeMask;
}

static INDEX _gpu_iCurrent1stVertex = 0;

// a static buffer's array as a stream: from its GPU buffer, or from memory
static void gpu_SetBufferStream( GpuStream eStream, const VertexBuffer &vb, INDEX iType, INDEX i1stVertex, INDEX ctVertices)
{
	const SLONG slTypeSize = GetVBTypeSize(iType);
	if( vb.vb_apbufGpu[iType]!=NULL) {
		_pgpu->SetStreamBuffer( eStream, vb.vb_apbufGpu[iType], (size_t)slTypeSize*i1stVertex);
	} else {
		_pgpu->SetStreamData( eStream, (UBYTE*)vb.vb_paWriteArray[iType] + slTypeSize*i1stVertex, (size_t)slTypeSize*ctVertices);
	}
}

static void gpu_SetVertexSubBuffer( const INDEX iID, const INDEX i1stVertex/*=0*/, const INDEX ctVertices/*=0*/)
{
	const INDEX iBind = iID >> 4;
	const INDEX iType = iID & 15;
	ASSERT( iType==GFX_VBA_POS && iBind<_avbVertexBuffers.Count());
	VertexBuffer &vb = _avbVertexBuffers[iBind];
	GFX_ctVertices = (ctVertices>0) ? ctVertices : vb.vb_ctVertices;
	// as gpu_SetVertexArray(), from the buffer
	gpu_SetVertexArray( (GFXVertex*)vb.vb_paWriteArray[GFX_VBA_POS], GFX_ctVertices);
	gpu_SetBufferStream( GPUS_POSITION, vb, GFX_VBA_POS, i1stVertex, GFX_ctVertices);
	_gpu_iCurrent1stVertex = i1stVertex;
}

static void gpu_SetSubBuffer( const INDEX iID, const INDEX iUnit/*=-1*/)
{
	const INDEX iBind = iID >> 4;
	const INDEX iType = iID & 15;
	ASSERT( iType>GFX_VBA_POS && iType<GFX_MAX_VBA && iBind<_avbVertexBuffers.Count());
	VertexBuffer &vb = _avbVertexBuffers[iBind];
	const INDEX i1st = _gpu_iCurrent1stVertex;
	switch( iType) {
	case GFX_VBA_NOR:  gpu_SetBufferStream( GPUS_NORMAL,  vb, iType, i1st, GFX_ctVertices);  break;
	case GFX_VBA_WGH:  gpu_SetBufferStream( GPUS_WEIGHT,  vb, iType, i1st, GFX_ctVertices);  break;
	case GFX_VBA_TAN:  gpu_SetBufferStream( GPUS_TANGENT, vb, iType, i1st, GFX_ctVertices);  break;
	case GFX_VBA_COL:
		GFX_bColorArray = TRUE;
		gpu_SetBufferStream( GPUS_COLOR, vb, iType, i1st, GFX_ctVertices);
		break;
	default: {
		// tex coord array (to the given unit, or the current one)
		ASSERT( iType>=GFX_VBA_TEX);
		const INDEX iTexUnit = (iUnit>=0) ? iUnit : GFX_iActiveTexUnit;
		if( iTexUnit>=GFX_USETEXUNITS) break;
		gpu_SetBufferStream( (GpuStream)(GPUS_TEXCOORD0+iTexUnit), vb, iType, i1st, GFX_ctVertices);
		_gpu_aubTexCoords[iTexUnit] = 1;
		} break;
	}
}

static void gpu_Finish( BOOL bReadBack)
{
	_pgpu->Finish();
}

static void gpu_LockArrays(void) { NOTHING; }


// VERTEX AND PIXEL PROGRAMS
// Direct3D's assembly into ARB (ShaderAsmToARB.cpp, as for OpenGL), and that into the device's
// shading language (TranslateProgram: ShaderARBToMSL.cpp for Metal). They are on while set and
// gfxEnableVertexProgram()/gfxEnablePixelProgram() are in effect (gpu_SyncState).
// LC_DUMP_PROGRAMS=<directory> writes each program's assembly there (<hash>.vs or .ps), for the
// offline test of the translation.

static void gpu_SetVertexProgram( const ULONG ulHandle) { _pGfx->gl_dwVertexShader = ulHandle; }
static void gpu_SetPixelProgram(  const ULONG ulHandle) { _pGfx->gl_dwPixelShader  = ulHandle; }

static void gpu_DeleteProgram( ULONG ulHandle)
{
	GpuProgram *ppr = gpu_Program(ulHandle);
	if( ppr==NULL) return;
	if( _pGfx->gl_dwVertexShader==ulHandle) _pGfx->gl_dwVertexShader = NONE;
	if( _pGfx->gl_dwPixelShader ==ulHandle) _pGfx->gl_dwPixelShader  = NONE;
	_pgpu->DeleteProgram(ppr);
	_gpu_apprPrograms[ulHandle-1] = NULL;
}

// print a program that failed, line by line (the console has a line limit)
static void gpu_PrintProgram( const char *strProgram)
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
static ULONG gpu_CreateProgram( BOOL bVertex, const char *strD3D)
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

	std::string strARB, strSource, strError;
	const bool bTranslated = (bVertex ? TranslateVertexProgramToARB( strD3D, strARB, strError)
	                                  : TranslatePixelProgramToARB(  strD3D, strARB, strError))
	                      && _pgpu->TranslateProgram( strARB, strSource, strError);
	if( !bTranslated) {
		CPrintF( "Cannot translate %s program: %s\n", bVertex ? "vertex" : "pixel", strError.c_str());
		gpu_PrintProgram( strD3D);
		_setFailed.insert(strD3D);
		return NONE;
	}
	char strCompileError[4096];
	GpuProgram *ppr = _pgpu->CreateProgram( bVertex!=FALSE, strSource.c_str(), strCompileError, sizeof(strCompileError));
	if( ppr==NULL) {
		CPrintF( "Cannot compile %s program: %s\n", bVertex ? "vertex" : "pixel", strCompileError);
		gpu_PrintProgram( strSource.c_str());
		_setFailed.insert(strD3D);
		return NONE;
	}
	_gpu_apprPrograms.push_back(ppr);
	return (ULONG)_gpu_apprPrograms.size();
}

static ULONG gpu_CreateVertexProgram( const char *strVertexProgram, ULONG ulStreamMasks)
{
	// the inputs are fixed (the GpuStream streams), so they need no declaration
	(void)ulStreamMasks;
	return gpu_CreateProgram( TRUE, strVertexProgram);
}

static ULONG gpu_CreatePixelProgram( const char *strPixelProgram)
{
	return gpu_CreateProgram( FALSE, strPixelProgram);
}

static void gpu_SetVertexProgramConst( INDEX iRegister, const void *pData, INDEX ctRegisters)
{
	ASSERT( iRegister>=0 && ctRegisters>0 && iRegister+ctRegisters<=GPU_VERTEX_CONSTANTS && pData!=NULL);
	memcpy( _gpu_afVertexConst[iRegister], pData, ctRegisters*4*sizeof(FLOAT));
}

// (c0..c7, and the texture stage values above them: ShaderAsmToARB.h)
static void gpu_SetPixelProgramConst( INDEX iRegister, const void *pData, INDEX ctRegisters)
{
	ASSERT( iRegister>=0 && ctRegisters>0 && iRegister+ctRegisters<=GPU_PIXEL_CONSTANTS && pData!=NULL);
	memcpy( _gpu_afPixelConst[iRegister], pData, ctRegisters*4*sizeof(FLOAT));
}

// value of constant registers (for shaGet*ProgramConst)
extern void GetProgramConst_GPU( BOOL bVertex, INDEX iRegister, void *pData, INDEX ctRegisters)
{
	memcpy( pData, bVertex ? _gpu_afVertexConst[iRegister] : _gpu_afPixelConst[iRegister], ctRegisters*4*sizeof(FLOAT));
}


// RENDER TARGETS AND THE FRAME (TextureRender.cpp, DrawPort.cpp, DepthCheck.cpp)
// The devices' render targets start at the top, as Direct3D's: what is drawn into them is the right
// way up for the texture coordinates Direct3D uses.

extern void GPU_MakeRenderTarget( TEXOBJ ulTexture, PIX pixW, PIX pixH)
{
	_pgpu->TextureRenderTarget( (GpuTexture*)ulTexture, pixW, pixH);
}

// draw into a render target (NONE: the window); returns where the draws went before
extern TEXOBJ GPU_SetRenderTarget( TEXOBJ ulTexture)
{
	const TEXOBJ ulOld = (TEXOBJ)_pgpu->GetRenderTarget();
	_pgpu->SetRenderTarget( (GpuTexture*)ulTexture);
	return ulOld;
}

// fill the whole render target (as Direct3D's Clear(): a D3DCOLOR, alpha too, without the masks)
extern void GPU_ClearRenderTarget( PIX pixW, PIX pixH, ULONG ulARGB, FLOAT fDepth)
{
	const float afColor[4] = { ((ulARGB>>16)&0xFF)/255.0f, ((ulARGB>>8)&0xFF)/255.0f, (ulARGB&0xFF)/255.0f, ((ulARGB>>24)&0xFF)/255.0f };
	_pgpu->Clear( 0, 0, pixW, pixH, true, afColor, true, fDepth);
}

// copy a rectangle of the frame into a texture (in the pixels drawn, from the top left)
extern void GPU_CopyFrameToTexture( TEXOBJ ulTexture, PIX pixI, PIX pixJ, PIX pixW, PIX pixH)
{
	if( ulTexture==NONE) return;
	_pgpu->CopyFrameToTexture( (GpuTexture*)ulTexture, pixI, pixJ, pixW, pixH);
}

// the frame's 24-bit RGB rows from the top, and depths at points (in the pixels drawn)
extern BOOL GPU_ReadFrameRGB( PIX pixI, PIX pixJ, PIX pixW, PIX pixH, UBYTE *pubRGB)
{
	return _pgpu->ReadFrameRGB( pixI, pixJ, pixW, pixH, pubRGB);
}
extern BOOL GPU_ReadFrameDepths( const int *piXY, INDEX ctPoints, FLOAT *pfDepths)
{
	return _pgpu->ReadFrameDepths( piXY, ctPoints, pfDepths);
}


// THE BLOOM'S PASSES (RenderBloom.cpp)
// Quads straight to the device, with their own state: the engine's state stays as it was, and its
// streams and textures on the device are put back by GPU_EndFilter().

extern void GPU_BeginFilter(void) { _pgpu->PushState(); }
extern void GPU_EndFilter(void)   { _pgpu->PopState();  }

// the quad from -1,-1 to 1,1 (that the constants c0..c3 put over the viewport) with the texture on
// units 0..3, through the programs and their constants as they are set
extern void GPU_DrawFilterQuad( TEXOBJ ulTexture, BOOL bLinear, ULONG ulVertexProgram, ULONG ulPixelProgram,
                                BOOL bBlend, GfxBlend eSrc, GfxBlend eDst)
{
	GpuProgram *pprVertex = gpu_Program(ulVertexProgram);
	GpuProgram *pprPixel  = gpu_Program(ulPixelProgram);
	if( pprVertex==NULL || pprPixel==NULL || ulTexture==NONE) return;
	// texture coordinates from the top, as Direct3D's
	static const FLOAT afPositions[4][3] = { {-1,-1,0}, {1,-1,0}, {1,1,0}, {-1,1,0} };
	static const FLOAT afTexCoords[4][2] = { {0,1}, {1,1}, {1,0}, {0,0} };
	for( INDEX iStream=0; iStream<GPUS_COUNT; iStream++) _pgpu->ClearStream( (GpuStream)iStream);
	_pgpu->SetStreamData( GPUS_POSITION,  afPositions, sizeof(afPositions));
	_pgpu->SetStreamData( GPUS_TEXCOORD0, afTexCoords, sizeof(afTexCoords));
	GpuSampler smp;
	smp.ubMagLinear = smp.ubMinLinear = bLinear ? 1 : 0;
	smp.ubMip = 0;
	smp.ubClampU = smp.ubClampV = 1;
	smp.ubAnisotropy = 1;
	for( INDEX iUnit=0; iUnit<GPU_MAXTEXUNITS; iUnit++) _pgpu->SetTexture( iUnit, (GpuTexture*)ulTexture, smp);

	const PIX pixScale = GFX_DrawableScale();
	_pgpu->SetViewport( _gpu_aiViewport[0]*pixScale, _gpu_aiViewport[1]*pixScale,
	                _gpu_aiViewport[2]*pixScale, _gpu_aiViewport[3]*pixScale, 0.0f, 1.0f);
	_pgpu->SetScissor( _gpu_aiScissor[0]*pixScale, _gpu_aiScissor[1]*pixScale, _gpu_aiScissor[2]*pixScale, _gpu_aiScissor[3]*pixScale);
	_pgpu->SetBlend( bBlend!=FALSE, (GpuBlend)(eSrc-GFX_ONE), (GpuBlend)(eDst-GFX_ONE));
	_pgpu->SetColorMask( true, true, true, true);
	_pgpu->SetDepth( false, false, GPUC_ALWAYS);
	_pgpu->SetCull( GPUCULL_NONE, true);
	_pgpu->SetWireframe( false);
	_pgpu->SetDepthBias( false);
	_pgpu->SetProgram( pprVertex, pprPixel);

	GpuVPConst vc;
	memcpy( vc.afConst, _gpu_afVertexConst, sizeof(vc.afConst));
	vc.afColor[0] = vc.afColor[1] = vc.afColor[2] = vc.afColor[3] = 1.0f;
	vc.ulStreams = (1UL<<GPUS_POSITION) | (1UL<<GPUS_TEXCOORD0);
	vc.ulTexCoords = 0;
	vc.aulPad[0] = vc.aulPad[1] = 0;
	_pgpu->SetVertexConstants( &vc, sizeof(vc));
	GpuPPConst pc;
	memcpy( pc.afConst, _gpu_afPixelConst, sizeof(pc.afConst));
	pc.fAlphaRef  = 0.5f;
	pc.bAlphaTest = 0;
	pc.fLODBias   = 0.0f;
	pc.ulPad = 0;
	_pgpu->SetFragmentConstants( &pc, sizeof(pc));
	_pgpu->DrawQuads( 4);
}
