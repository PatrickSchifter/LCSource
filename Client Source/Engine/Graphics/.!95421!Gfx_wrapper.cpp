#include "StdH.h"

#include <Engine/Base/Console.h>
#include <Engine/Base/MemoryTracking.h>
#include <Engine/Math/Vector.h>
#include <Engine/Graphics/GfxLibrary.h>
#include <Engine/Graphics/ViewPort.h>
#include <Engine/Graphics/ShaderAsmToARB.h>

#include <set>

#include <Engine/Base/Statistics_Internal.h>


// track texture memory allocated in main heap
#define TRACKVB_HEAP() TRACKMEM(mem, "Vtx buffers (heap)")
// track texture memory allocated in hardware
#define TRACKVB_HW() TRACKMEM(mem, "Vtx buffs (hardware)")


extern INDEX gap_bOptimizeStateChanges;
extern INDEX gap_iOptimizeClipping;
extern INDEX gap_bUseOcclusionCulling;
extern INDEX gap_iDithering;
								
						
// cached states
extern BOOL GFX_bDepthTest  = FALSE;
extern BOOL GFX_bDepthWrite = FALSE;
extern BOOL GFX_bAlphaTest  = FALSE;
extern BOOL GFX_bDithering  = TRUE;
extern BOOL GFX_bBlending   = TRUE;
extern BOOL GFX_bLighting   = FALSE;
extern BOOL GFX_bClipping   = TRUE;
extern BOOL GFX_bClipPlane  = FALSE;
extern BOOL GFX_bColorArray = FALSE;
extern BOOL GFX_bTruform    = FALSE;
extern BOOL GFX_bFrontFace  = TRUE;
extern BOOL GFX_bViewMatrix = TRUE;
extern INDEX GFX_iActiveTexUnit = 0;
extern FLOAT GFX_fMinDepthRange = 0.0f;
extern FLOAT GFX_fMaxDepthRange = 0.0f;

extern BOOL GFX_bUseVertexProgram = FALSE;
extern BOOL GFX_bUsePixelProgram  = FALSE;

extern GfxBlend GFX_eBlendSrc  = GFX_ONE;
extern GfxBlend GFX_eBlendDst  = GFX_ZERO;
extern GfxComp  GFX_eDepthFunc = GFX_LESS_EQUAL;
extern GfxFace  GFX_eCullFace  = GFX_NONE;
extern BOOL  GFX_abLights[GFX_MAXLIGHTS] = { FALSE };
extern BOOL  GFX_abTexture[GFX_MAXTEXUNITS] = { FALSE };
extern INDEX GFX_iTexModulation[GFX_MAXTEXUNITS] = { 0 };

// last ortho/frustum values (frustum has negative sign, because of orgho-frustum switching!)
extern FLOAT GFX_fLastL = 0;
extern FLOAT GFX_fLastR = 0;
extern FLOAT GFX_fLastT = 0;
extern FLOAT GFX_fLastB = 0;
extern FLOAT GFX_fLastN = 0;
extern FLOAT GFX_fLastF = 0;

// number of vertices currently in buffer
extern INDEX GFX_ctVertices = 0;

// vertex buffers control
static CStaticStackArray<VertexBuffer> _avbVertexBuffers;
extern BOOL _bUsingDynamicBuffer = TRUE;
			 
// current texture object/bind number and color mask (for Get... function)
extern ULONG *GFX_apulCurrentTexture[GFX_MAXTEXUNITS] = {0};
extern ULONG GFX_ulCurrentColorMask = (CT_RMASK|CT_GMASK|CT_BMASK|CT_AMASK);

// for D3D: mark need for clipping (when wants to be disable but cannot be because of user clip plane)
static BOOL _bWantsClipping = TRUE;
// locking state for OGL
static BOOL _bCVAReallyLocked = FALSE;

// clip plane and last view matrix for D3D
extern FLOAT D3D_afClipPlane[4]    = {0,0,0,0};
extern FLOAT D3D_afViewMatrix[16]  = {0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0};
static FLOAT _afActiveClipPlane[4] = {0,0,0,0};

// Truform/N-Patches
extern INDEX truform_iLevel  = -1;
extern BOOL  truform_bLinear = FALSE;

// Texture transformation matrices
static ULONG _ulUsedTexMatrices = 0;

// functions' pointers
extern void (*gfxEnableDepthWrite)(void) = NULL;
extern void (*gfxEnableDepthBias)(void) = NULL;
extern void (*gfxEnableDepthTest)(void) = NULL;
extern void (*gfxEnableAlphaTest)(void) = NULL;
extern void (*gfxEnableBlend)(void) = NULL;
extern void (*gfxEnableDither)(void) = NULL;
extern void (*gfxEnableTexture)(void) = NULL;
extern void (*gfxEnableLight)( INDEX iLight) = NULL;
extern void (*gfxEnableLighting)(void) = NULL;
extern void (*gfxEnableClipping)(void) = NULL;
extern void (*gfxEnableClipPlane)(void) = NULL;
extern void (*gfxDisableDepthWrite)(void) = NULL;
extern void (*gfxDisableDepthBias)(void) = NULL;
extern void (*gfxDisableDepthTest)(void) = NULL;
extern void (*gfxDisableAlphaTest)(void) = NULL;
extern void (*gfxDisableBlend)(void) = NULL;
extern void (*gfxDisableDither)(void) = NULL;
extern void (*gfxDisableTexture)(void) = NULL;
extern void (*gfxDisableLight)( INDEX iLight) = NULL;
extern void (*gfxDisableLighting)(void) = NULL;
extern void (*gfxDisableClipping)(void) = NULL;
extern void (*gfxDisableClipPlane)(void) = NULL;
extern void (*gfxBlendFunc)( GfxBlend eSrc, GfxBlend eDst) = NULL;
extern void (*gfxDepthFunc)( GfxComp eFunc) = NULL;
extern void (*gfxDepthRange)( FLOAT fMin, FLOAT fMax) = NULL;
extern void (*gfxCullFace)(  GfxFace eFace) = NULL;
extern void (*gfxFrontFace)( GfxFace eFace) = NULL;
extern void (*gfxClipPlane)( const DOUBLE *pdPlane) = NULL;
extern void (*gfxSetOrtho)( const FLOAT fLeft, const FLOAT fRight, const FLOAT fTop,  const FLOAT fBottom, const FLOAT fNear, const FLOAT fFar, const BOOL bSubPixelAdjust) = NULL;
extern void (*gfxSetFrustum)( FLOAT fLeft, FLOAT fRight, FLOAT fTop, FLOAT fBottom, FLOAT fNear, FLOAT fFar) = NULL;
extern void (*gfxSetLightColor)( INDEX iLight, COLOR colLight, COLOR colAmbient, COLOR colSpecular/*=0*/) = NULL;
extern void (*gfxSetLightDirection)( INDEX iLight, const FLOAT3D &vLightDir) = NULL;
extern void (*gfxSetTextureMatrix)( const FLOAT *pfMatrix) = NULL;
extern void (*gfxSetViewMatrix)( const FLOAT *pfMatrix) = NULL;
extern void (*gfxPolygonMode)( GfxPolyMode ePolyMode) = NULL;
extern void (*gfxSetTextureWrapping)( enum GfxWrap eWrapU, enum GfxWrap eWrapV) = NULL;
extern void (*gfxSetTextureModulation)( INDEX iScale) = NULL;
extern void (*gfxGenerateTexture)( ULONG &ulTexObject) = NULL;
extern void (*gfxDeleteTexture)( ULONG &ulTexObject) = NULL;
extern void (*gfxSetVertexArray)( GFXVertex *pvtx, INDEX ctVtx) = NULL;
extern void (*gfxSetNormalArray)( GFXNormal *pnor) = NULL;
extern void (*gfxSetWeightArray)( GFXWeight *pwgh) = NULL;
extern void (*gfxSetTexCoordArray)( GFXTexCoord *ptex, BOOL bProjectiveMapping) = NULL;
extern void (*gfxSetColorArray)( GFXColor *pcol) = NULL;
extern void (*gfxDrawElements)( const INDEX ctIndices, const UWORD *puwIndices) = NULL;
extern void (*gfxSetConstantColor)(COLOR col) = NULL;
extern void (*gfxEnableColorArray)(void) = NULL;
extern void (*gfxDisableColorArray)(void) = NULL;
extern void (*gfxFinish)( BOOL bReadBack) = NULL;
extern void (*gfxLockArrays)(void) = NULL;
extern void (*gfxEnableTruform)( void) = NULL;
extern void (*gfxDisableTruform)(void) = NULL;
extern void (*gfxSetColorMask)( ULONG ulColorMask) = NULL; 
extern void* (*gfxLockSubBuffer)(   const INDEX iID, const INDEX i1stVertex, const INDEX ctVertices, const enum GfxLockType eLockType) = NULL;
extern void  (*gfxUnlockSubBuffer)( const INDEX iID, const INDEX ctVertices) = NULL;
extern void  (*gfxSetVertexSubBuffer)( const INDEX iID, const INDEX i1stVertex, const INDEX ctVertices) = NULL;
extern void  (*gfxSetSubBuffer)( const INDEX iID, const INDEX iUnit) = NULL;

extern void  (*gfxSetVertexProgram)( const ULONG ulHandle) = NULL;
extern void  (*gfxSetPixelProgram)(  const ULONG ulHandle) = NULL;
extern void  (*gfxDeleteVertexProgram)( ULONG ulHandle) = NULL;
extern void  (*gfxDeletePixelProgram)(  ULONG ulHandle) = NULL;
extern void  (*gfxSetVertexProgramConst)( INDEX iRegister, const void *pData, INDEX ctRegisters) = NULL;
extern void  (*gfxSetPixelProgramConst)(  INDEX iRegister, const void *pData, INDEX ctRegisters) = NULL;
extern ULONG (*gfxCreateVertexProgram)( const char *strVertexProgram, ULONG ulStreamFlags) = NULL;
extern ULONG (*gfxCreatePixelProgram)(  const char *strVertexProgram) = NULL;


// dummy functions (one size fits all:)
static void none_void(void)
{
	ASSERT( _pGfx->gl_eCurrentAPI==GAT_NONE);
}

static void none_int( INDEX iScale)
{
	ASSERT( _pGfx->gl_eCurrentAPI==GAT_NONE);
}

static void none_const_int( const INDEX iScale)
{
	ASSERT( _pGfx->gl_eCurrentAPI==GAT_NONE);
}

static void none_const_ulong( const ULONG ul)
{
	ASSERT( _pGfx->gl_eCurrentAPI==GAT_NONE);
}

static void none_ulong(ULONG ul)
{
	ASSERT( _pGfx->gl_eCurrentAPI==GAT_NONE);
}


// error checkers (this is for debug version only)


extern void OGL_CheckError(void)
{
#ifndef NDEBUG
	const GfxAPIType eAPI = _pGfx->gl_eCurrentAPI;
	if( eAPI==GAT_OGL) ASSERT( pglGetError()==GL_NO_ERROR);
	else ASSERT( eAPI==GAT_NONE);
#endif
}



#ifdef SE1_D3D
extern void D3D_CheckError(HRESULT hr)
{
#ifndef NDEBUG
	const GfxAPIType eAPI = _pGfx->gl_eCurrentAPI;
	if( eAPI==GAT_D3D) ASSERT( hr==D3D_OK);
	else ASSERT( eAPI==GAT_NONE);
#endif
}
#endif // SE1_D3D



// TEXTURE MANAGEMENT

