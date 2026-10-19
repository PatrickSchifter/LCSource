#include "StdH.h"

#include <Engine/Graphics/Texture.h>

#include <Engine/Base/Stream.h>
#include <Engine/Base/Timer.h>
#include <Engine/Base/Console.h>
#include <Engine/Math/Functions.h>
#include <Engine/Graphics/GfxLibrary.h>
#include <Engine/Graphics/ImageInfo.h>
#include <Engine/Graphics/TextureEffects.h>

#include <Engine/Templates/DynamicArray.h>
#include <Engine/Templates/DynamicArray.cpp>
#include <Engine/Templates/Stock_CTextureData.h>
#include <Engine/Templates/StaticArray.cpp>

#include <Engine/Base/Statistics_Internal.h>

//안태훈 수정 시작	//(Add CRenderTexture class for Render to Texture)(0.1)
#include <Engine/Base/MemoryTracking.h>
#ifdef SE1_D3D
#include <d3d8.h>
#endif // SE1_D3D

// track texture memory allocated in main heap
#define TRACKTEX_HEAP() TRACKMEM(mem, "Textures (heap)")
// track texture memory allocated in hardware
#define TRACKTEX_HW() TRACKMEM(mem, "Textures (hardware)")

#ifdef SE1_D3D
CRenderTexture::CRenderTexture()
{
	rt_pSurface = NULL;
	m_pDepthStencil = NULL;
}

CRenderTexture::~CRenderTexture()
{
	if(rt_pSurface) rt_pSurface->Release();
	if(m_pDepthStencil) m_pDepthStencil->Release();
}

// sehan D3DFORMAT fmt 추가 // sehan end
BOOL CRenderTexture::Init(INDEX width, INDEX height, ULONG flag, D3DFORMAT fmt)
{
	TRACKTEX_HEAP();
	
	// check maximum supported texture dimension
	if( width>MAX_MEX || height>MAX_MEX)
	{
		ASSERTALWAYS("너무 큰 텍스쳐 사이즈임. BY ANT");
		return FALSE;
	}

	// sehan D3DFMT_A8R8G8B8 를 fmt 로 바꿈. // sehan end
	rt_tdTexture.td_ulInternalFormat = fmt;
	
	// determine mip index from mex size and alpha channel presence
	rt_tdTexture.td_iFirstMipLevel = 0;
	rt_tdTexture.td_ulFlags = flag;
	rt_tdTexture.td_ulFlags |= TEX_ALPHACHANNEL;	//alhpa
	rt_tdTexture.td_ulFlags |= TEX_TRANSPARENT;		//alpha가 있으면 항상.
	rt_tdTexture.td_ulFlags |= TEX_32BIT;
	rt_tdTexture.td_ulFlags |= TEX_CONSTANT;
	rt_tdTexture.td_ulFlags &= ~TEX_STATIC;
	rt_tdTexture.td_ulFlags &= ~(TEX_COMPRESS|TEX_COMPRESSALPHA);	//not allowed compress in shadow

	// initialize general TextureData members
	rt_tdTexture.td_ctFrames  = 0;
	rt_tdTexture.td_mexWidth  = width << rt_tdTexture.td_iFirstMipLevel;
	rt_tdTexture.td_mexHeight = height << rt_tdTexture.td_iFirstMipLevel;
	
	// create all mip levels (either bilinear or downsampled)
	rt_tdTexture.td_ctFineMipLevels = 1;
	// get frame size (includes only one mip-map)
	rt_tdTexture.td_slFrameSize = 0;
	
	// allocate small ammount of memory just for Realloc sake
	rt_tdTexture.td_pulFrames = NULL;
	rt_tdTexture.td_ctFrames = 1;

	//IDirect3DTexture8의 생성.
	IDirect3DTexture8 *pTexture = NULL;
	// sehan D3DFMT_A8R8G8B8 를 fmt 로 바꿈. // sehan end
	HRESULT hr = _pGfx->gl_pd3dDevice->CreateTexture(width, height, 1, D3DUSAGE_RENDERTARGET, fmt, D3DPOOL_DEFAULT, &pTexture);
	if(hr == NOERROR)
	{
		rt_tdTexture.td_ulObject = (ULONG)pTexture;	//내가 작성했지만 싫다.  -_-;
		pTexture->GetSurfaceLevel(0, &rt_pSurface);
		//일단 Depth Stencil Surface는 쓰지 않지만 설정해야 된다.
		hr = _pGfx->gl_pd3dDevice->CreateDepthStencilSurface(width, height, D3DFMT_D16, D3DMULTISAMPLE_NONE, &m_pDepthStencil);
		if(hr == NOERROR)
		{
			return TRUE;
		}
	}
	else
	{
		rt_tdTexture.td_pulObjects = NULL;
		return FALSE;
	}
	return FALSE;
}

void CRenderTexture::Begin()	// SetRenderTarget current
{
	IDirect3DDevice8 *pDev = _pGfx->gl_pd3dDevice;
	pDev->GetRenderTarget(&m_pOldRenderTarget);
	pDev->GetDepthStencilSurface(&m_pOldDepthStencil);
	pDev->SetRenderTarget(rt_pSurface, m_pDepthStencil);
}

void CRenderTexture::Clear(COLOR colClear, FLOAT fZVal)
{
	_pGfx->gl_pd3dDevice->Clear(0, NULL, D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER, colClear, fZVal, 0);
}

void CRenderTexture::End()		// SetRenderTarget old
{
	IDirect3DDevice8 *pDev = _pGfx->gl_pd3dDevice;
	pDev->SetRenderTarget(m_pOldRenderTarget, m_pOldDepthStencil);
	if(m_pOldRenderTarget)
	{
		m_pOldRenderTarget->Release();
		m_pOldRenderTarget = NULL;
	}

	if(m_pOldDepthStencil)
	{
		m_pOldDepthStencil->Release();
		m_pOldDepthStencil = NULL;
	}
}
#else
// OpenGL: a framebuffer object with the texture as its color and a 16-bit depth renderbuffer,
// as the Direct3D version has; Init() fails without GL_EXT_framebuffer_object and callers skip the effect
CRenderTexture::CRenderTexture()
{
	m_ulFramebuffer = 0;
	m_ulDepthBuffer = 0;
	m_iOldFramebuffer = 0;
}

CRenderTexture::~CRenderTexture()
{
	// the texture itself goes with rt_tdTexture
	if( m_ulFramebuffer!=0) pglDeleteFramebuffersEXT( 1, (GLuint*)&m_ulFramebuffer);
	if( m_ulDepthBuffer!=0) pglDeleteRenderbuffersEXT( 1, (GLuint*)&m_ulDepthBuffer);
}

BOOL CRenderTexture::Init(INDEX width, INDEX height, ULONG flag)
{
	TRACKTEX_HEAP();

	if( _pGfx->gl_eCurrentAPI!=GAT_OGL || pglGenFramebuffersEXT==NULL) return FALSE;
	if( width>MAX_MEX || height>MAX_MEX) return FALSE;

	// same texture data as the Direct3D version
	rt_tdTexture.td_ulInternalFormat = GL_RGBA8;
	rt_tdTexture.td_iFirstMipLevel = 0;
	rt_tdTexture.td_ulFlags = flag;
	rt_tdTexture.td_ulFlags |= TEX_ALPHACHANNEL;
	rt_tdTexture.td_ulFlags |= TEX_TRANSPARENT;
	rt_tdTexture.td_ulFlags |= TEX_32BIT;
	rt_tdTexture.td_ulFlags |= TEX_CONSTANT;
	rt_tdTexture.td_ulFlags &= ~TEX_STATIC;
	rt_tdTexture.td_ulFlags &= ~(TEX_COMPRESS|TEX_COMPRESSALPHA);
	rt_tdTexture.td_mexWidth  = width;
	rt_tdTexture.td_mexHeight = height;
	rt_tdTexture.td_ctFineMipLevels = 1;
	rt_tdTexture.td_slFrameSize = 0;
	rt_tdTexture.td_pulFrames = NULL;
	rt_tdTexture.td_ctFrames = 1;
	rt_tdTexture.td_tpLocal.tp_bSingleMipmap = TRUE;

	// the texture, bound through gfxSetTexture() so the engine knows what is bound
	gfxGenerateTexture( rt_tdTexture.td_ulObject);
	gfxSetTexture( rt_tdTexture.td_ulObject, rt_tdTexture.td_tpLocal);
	pglTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	OGL_CHECKERROR;

	pglGenRenderbuffersEXT( 1, (GLuint*)&m_ulDepthBuffer);
	pglBindRenderbufferEXT( GL_RENDERBUFFER_EXT, m_ulDepthBuffer);
	pglRenderbufferStorageEXT( GL_RENDERBUFFER_EXT, GL_DEPTH_COMPONENT16, width, height);
	pglBindRenderbufferEXT( GL_RENDERBUFFER_EXT, 0);

	GLint iOldFramebuffer = 0;
	pglGetIntegerv( GL_FRAMEBUFFER_BINDING_EXT, &iOldFramebuffer);
	pglGenFramebuffersEXT( 1, (GLuint*)&m_ulFramebuffer);
	pglBindFramebufferEXT( GL_FRAMEBUFFER_EXT, m_ulFramebuffer);
	pglFramebufferTexture2DEXT( GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, rt_tdTexture.td_ulObject, 0);
	pglFramebufferRenderbufferEXT( GL_FRAMEBUFFER_EXT, GL_DEPTH_ATTACHMENT_EXT, GL_RENDERBUFFER_EXT, m_ulDepthBuffer);
	const GLenum eStatus = pglCheckFramebufferStatusEXT( GL_FRAMEBUFFER_EXT);
	pglBindFramebufferEXT( GL_FRAMEBUFFER_EXT, iOldFramebuffer);
	OGL_CHECKERROR;

	if( eStatus!=GL_FRAMEBUFFER_COMPLETE_EXT) {
		CPrintF( "Cannot create a %dx%d render target (framebuffer status 0x%X)\n", width, height, eStatus);
		pglDeleteFramebuffersEXT( 1, (GLuint*)&m_ulFramebuffer);
		m_ulFramebuffer = 0;
		return FALSE;
	}
	return TRUE;
}

void CRenderTexture::Begin()	// SetRenderTarget current
{
	// some callers keep a render target whose Init() failed: nothing to bind
	if( m_ulFramebuffer==0) return;
	GLint iOld = 0;
	pglGetIntegerv( GL_FRAMEBUFFER_BINDING_EXT, &iOld);
	m_iOldFramebuffer = iOld;
	pglGetIntegerv( GL_VIEWPORT,    (GLint*)m_aiOldViewport);
	pglGetIntegerv( GL_SCISSOR_BOX, (GLint*)m_aiOldScissor);
	pglBindFramebufferEXT( GL_FRAMEBUFFER_EXT, m_ulFramebuffer);
	// Direct3D's SetRenderTarget() sets the viewport to the whole target; the scissor test stays on (DrawPort.cpp)
	const PIX pixWidth  = rt_tdTexture.td_mexWidth;
	const PIX pixHeight = rt_tdTexture.td_mexHeight;
	pglViewport( 0, 0, pixWidth, pixHeight);
	pglScissor(  0, 0, pixWidth, pixHeight);
	OGL_CHECKERROR;
}

void CRenderTexture::Clear(COLOR colClear, FLOAT fZVal)
{
	if( m_ulFramebuffer==0) return;
	// colClear is a D3DCOLOR (ARGB), as for Direct3D's Clear(); that clears regardless of the write masks
	GLfloat afOldColor[4], fOldDepth;
	GLboolean abOldColorMask[4], bOldDepthMask;
	pglGetFloatv( GL_COLOR_CLEAR_VALUE, afOldColor);
	pglGetFloatv( GL_DEPTH_CLEAR_VALUE, &fOldDepth);
	pglGetBooleanv( GL_COLOR_WRITEMASK, abOldColorMask);
	pglGetBooleanv( GL_DEPTH_WRITEMASK, &bOldDepthMask);
	pglClearColor( ((colClear>>16)&0xFF)/255.0f, ((colClear>>8)&0xFF)/255.0f, (colClear&0xFF)/255.0f, ((colClear>>24)&0xFF)/255.0f);
	pglClearDepth( fZVal);
	pglColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	pglDepthMask( GL_TRUE);
	pglClear( GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
	pglColorMask( abOldColorMask[0], abOldColorMask[1], abOldColorMask[2], abOldColorMask[3]);
	pglDepthMask( bOldDepthMask);
	pglClearColor( afOldColor[0], afOldColor[1], afOldColor[2], afOldColor[3]);
	pglClearDepth( fOldDepth);
	OGL_CHECKERROR;
}

void CRenderTexture::End()		// SetRenderTarget old
{
	if( m_ulFramebuffer==0) return;
	pglBindFramebufferEXT( GL_FRAMEBUFFER_EXT, m_iOldFramebuffer);
	pglViewport( m_aiOldViewport[0], m_aiOldViewport[1], m_aiOldViewport[2], m_aiOldViewport[3]);
	pglScissor(  m_aiOldScissor[0],  m_aiOldScissor[1],  m_aiOldScissor[2],  m_aiOldScissor[3]);
	OGL_CHECKERROR;
}
#endif // SE1_D3D
//안태훈 수정 끝	//(Add CRenderTexture class for Render to Texture)(0.1)
