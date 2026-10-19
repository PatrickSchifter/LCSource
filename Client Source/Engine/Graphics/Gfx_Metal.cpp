#include "StdH.h"

// The library side of the Metal backend (GAT_MTL, macOS): starting and ending it, the context
// on the game's window and the end of a frame. The device is MetalDevice.mm; the gfx* functions
// are Gfx_wrapper_Metal.cpp.

#ifdef SE1_METAL

#include <Engine/Graphics/GfxLibrary.h>
#include <Engine/Graphics/ViewPort.h>
#include <Engine/Graphics/MetalDevice.h>
#include <Engine/Base/Translation.h>
#include <Engine/Base/ErrorReporting.h>
#include <Engine/Base/Console.h>
#include <Engine/Base/Memory.h>
#include <Engine/Platform/Platform.h>

#include <SDL3/SDL.h>

extern TEXOBJ _fog_ulTexture;
extern TEXOBJ _haze_ulTexture;
extern INDEX GFX_ctVertices;
extern UINT *_puiOcclusionQueryIDs;

extern BOOL GFX_bDepthTest;
extern BOOL GFX_bDepthWrite;
extern BOOL GFX_bAlphaTest;
extern BOOL GFX_bBlending;
extern BOOL GFX_bDithering;
extern BOOL GFX_bLighting;
extern BOOL GFX_bClipping;
extern BOOL GFX_bClipPlane;
extern BOOL GFX_bColorArray;
extern BOOL GFX_bFrontFace;
extern BOOL GFX_bTruform;
extern INDEX GFX_iActiveTexUnit;
extern FLOAT GFX_fMinDepthRange;
extern FLOAT GFX_fMaxDepthRange;
extern GfxBlend GFX_eBlendSrc;
extern GfxBlend GFX_eBlendDst;
extern GfxComp  GFX_eDepthFunc;
extern GfxFace  GFX_eCullFace;
extern INDEX GFX_iTexModulation[GFX_MAXTEXUNITS];
extern BOOL  GFX_abLights[GFX_MAXLIGHTS];
extern BOOL  GFX_abTexture[GFX_MAXTEXUNITS];
extern ULONG GFX_ulCurrentColorMask;

extern INDEX gap_iSwapInterval;

extern BOOL InitFixedFunction_MTL(void);
extern void EndFixedFunction_MTL(void);


// whether Metal can be used (the device is made on the window, SetCurrentViewport_MTL)
BOOL CGfxLibrary::InitDriver_MTL(void)
{
	if( !mtlIsAvailable()) {
		CPrintF( TRANS("Error starting Metal: no Metal device\n"));
		return FALSE;
	}
	return TRUE;
}


void CGfxLibrary::EndDriver_MTL(void)
{
	// programs belong to the device
	extern void ClearVertexAndPixelPrograms(void);
	ClearVertexAndPixelPrograms();
	// occlusion query IDs (not used with Metal, but kept as with OpenGL)
	if( _puiOcclusionQueryIDs!=NULL) {
		FreeMemory(_puiOcclusionQueryIDs);
		_puiOcclusionQueryIDs = NULL;
	}
	// restore the display's gamma (platform layer)
	if( gl_ulFlags & GLF_ADJUSTABLEGAMMA) PLT_SetGammaRamp(NULL);
	EndFixedFunction_MTL();
	mtlEnd();
}


// state and capabilities of a new device (as InitContext_OGL)
void CGfxLibrary::InitContext_MTL(void)
{
	ASSERT( gl_pvpActive!=NULL);

	// the engine's state, as the device starts
	for( INDEX iUnit=0; iUnit<GFX_MAXTEXUNITS; iUnit++) {
		GFX_abTexture[iUnit] = FALSE;
		GFX_iTexModulation[iUnit] = 1;
	}
	GFX_iActiveTexUnit = 0;
	gl_ctMaxStreams = 16;
	extern BOOL  GFX_bViewMatrix;
	extern FLOAT GFX_fLastL, GFX_fLastR, GFX_fLastT, GFX_fLastB, GFX_fLastN, GFX_fLastF;
	GFX_fLastL = GFX_fLastR = GFX_fLastT = GFX_fLastB = GFX_fLastN = GFX_fLastF = 0;
	GFX_bViewMatrix = TRUE;
	GFX_bTruform  = FALSE;
	GFX_bClipping = TRUE;
	GFX_ulCurrentColorMask = CT_RMASK|CT_GMASK|CT_BMASK|CT_AMASK;
	GFX_abTexture[0] = TRUE;
	GFX_bDithering   = TRUE;
	GFX_bBlending    = FALSE;
	GFX_bDepthTest   = FALSE;
	GFX_bAlphaTest   = FALSE;
	GFX_bLighting    = FALSE;
	GFX_bClipPlane   = FALSE;
	GFX_eCullFace    = GFX_NONE;
	GFX_bFrontFace   = TRUE;
	GFX_bDepthWrite  = FALSE;
	GFX_eDepthFunc   = GFX_LESS_EQUAL;
	GFX_eBlendSrc = GFX_eBlendDst = GFX_ONE;
	GFX_fMinDepthRange = 0.0f;
	GFX_fMaxDepthRange = 1.0f;
	for( INDEX iLight=0; iLight<GFX_MAXLIGHTS; iLight++) GFX_abLights[iLight] = FALSE;
	GFX_bColorArray = FALSE;
	if( !InitFixedFunction_MTL()) {
		FatalError( TRANS("Cannot compile the Metal fixed function emulation (see the console log)."));
	}

	CDisplayAdapter &da = gl_gaAPI[GAT_MTL].ga_adaAdapter[gl_iCurrentAdapter];
	da.da_strVendor   = "Apple";
	da.da_strRenderer = mtlDeviceName();
	da.da_strVersion  = "Metal";
	CPrintF( TRANS("\n* Metal device created: *----------------------------------\n"));
	CPrintF( "  (%s)\n", (const char*)da.da_strRenderer);
	int iW=0, iH=0;
	SDL_GetWindowSize( (SDL_Window*)gl_pvpActive->vp_hWnd, &iW, &iH);
	const INDEX iScale = PLT_GetDrawableScale();
	CPrintF( "  Window: %dx%d points, %dx%d pixels\n\n", iW, iH, iW*iScale, iH*iScale);

	// what the device has (none of the OpenGL extensions); vertex and pixel programs are Direct3D's
	// assembly translated into MSL (Gfx_wrapper_Metal.cpp)
	gl_ulFlags |= GLF_HASACCELERATION|GLF_32BITTEXTURES|GLF_VSYNC|GLF_VERTEXPROGRAM|GLF_PIXELPROGRAM;
	gl_ulFlags &= ~(GLF_STENCILBUFFER|GLF_TEXTURECOMPRESSION);
	gl_ctDepthBits = 32;
	if( mtlHasDXT()) gl_ulFlags |= GLF_TEXTURECOMPRESSION;  // the textures that come compressed
	gl_ulFlags &= ~GLF_ADJUSTABLEGAMMA;
	if( PLT_CanSetGammaRamp()) gl_ulFlags |= GLF_ADJUSTABLEGAMMA;
	else CPrintF( TRANS("\nWARNING: Gamma, brightness and contrast are not adjustable.\n\n"));
	gl_ctTextureUnits = GFX_USETEXUNITS;
	gl_ctRealTextureUnits = GFX_USETEXUNITS;
	gl_pixMaxTextureDimension = 16384;
	gl_ctMaxPrimitives = 16777215;
	gl_fMaxTextureLODBias = 4.0f;
	gl_iMaxTextureAnisotropy = 16;
	gl_iTessellationLevel = 0;
	gl_iMaxTessellationLevel = 0;
	go_strExtensions = "";
	go_strWinExtensions = "";
	go_strSupportedExtensions = "none";

	// fog, haze and line pattern textures
	extern PIX _fog_pixSizeH;
	extern PIX _fog_pixSizeL;
	extern PIX _haze_pixSize;
	gfxGenerateTexture( _fog_ulTexture);
	gfxGenerateTexture( _haze_ulTexture);
	_fog_pixSizeH = 0;
	_fog_pixSizeL = 0;
	_haze_pixSize = 0;
	extern CTexParams _tpPattern;
	extern TEXOBJ _ulPatternTexture;
	extern ULONG _ulLastUploadedPattern;
	gfxGenerateTexture( _ulPatternTexture);
	_ulLastUploadedPattern = 0;
	_tpPattern.Clear();

	extern void InitVertexBuffers(void);
	InitVertexBuffers();

	for( INDEX i=0; i<GFX_MAXTEXUNITS; i++) _tpGlobal[i].Clear();
	GFX_ctVertices = 0;
	gl_dwVertexShader = NONE;
	gl_dwPixelShader  = NONE;

	extern INDEX gap_iTextureFiltering;
	extern INDEX gap_iTextureAnisotropy;
	extern FLOAT gap_fTextureLODBias;
	gfxSetTextureFiltering( gap_iTextureFiltering, gap_iTextureAnisotropy);
	gfxSetTextureBiasing( gap_fTextureLODBias);

	_puiOcclusionQueryIDs = (UINT*)AllocMemory( GFX_MAXOCCQUERIES*sizeof(UINT));
	for( INDEX iID=0; iID<GFX_MAXOCCQUERIES; iID++) _puiOcclusionQueryIDs[iID] = iID;

	extern BOOL _bNeedPretouch;
	_bNeedPretouch = TRUE;
	gl_bAllowProbing = FALSE;

	extern void UpdateGfxSysCVars(void);
	UpdateGfxSysCVars();

	extern INDEX shd_bCacheAll;
	extern void ReloadTextures(void);
	extern void ReloadMeshes(void);
	extern void CacheShadows(void);
	ReloadTextures();
	ReloadMeshes();
	if( shd_bCacheAll) CacheShadows();
}


// the device is made on the first viewport (the game's window), as the OpenGL context
BOOL CGfxLibrary::SetCurrentViewport_MTL( CViewPort *pvp)
{
	if( gl_ulFlags & GLF_INITONNEXTWINDOW)
	{
		gl_ulFlags &= ~GLF_INITONNEXTWINDOW;
		pvp->CloseCanvas();
		pvp->OpenCanvas();
		if( !mtlInit( (SDL_Window*)pvp->vp_hWnd, PLT_GetDrawableScale())) {
			CPrintF( TRANS("Error starting Metal on the window\n"));
			return FALSE;
		}
		gl_pvpActive = pvp;
		InitContext_MTL();
		pvp->vp_ctDisplayChanges = gl_ctDriverChanges;
		return TRUE;
	}
	if( pvp->vp_ctDisplayChanges<gl_ctDriverChanges) {
		pvp->CloseCanvas();
		pvp->OpenCanvas();
		pvp->vp_ctDisplayChanges = gl_ctDriverChanges;
	}
	gl_pvpActive = pvp;
	return TRUE;
}


// end of a frame
void CGfxLibrary::SwapBuffers_MTL( CViewPort *pvp)
{
	extern INDEX ogl_iFinish;
	extern INDEX ogl_bHardFinish;
	if( ogl_iFinish==2) gfxFinish(ogl_bHardFinish);
	// vertical sync (the swap interval is a wait or not)
	if( gl_iSwapInterval!=gap_iSwapInterval) {
		gl_iSwapInterval = gap_iSwapInterval;
		mtlSetVSync( gl_iSwapInterval>0);
	}
	mtlPresent();
	// the next frame at the window's density (Retina, fullscreen or not)
	mtlSetDrawableScale( PLT_GetDrawableScale());
	if( ogl_iFinish==3) gfxFinish(ogl_bHardFinish);
}

#endif // SE1_METAL
