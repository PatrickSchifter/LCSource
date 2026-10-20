// Minimal replacement for the DirectX 8.1 SDK's d3dx8tex.h (see d3dx8math.h).
#pragma once

#include <d3d8.h>

#ifndef D3DX_FILTER_NONE
#define D3DX_FILTER_NONE (1 << 0)
#endif

// Only what the client needs: copy an A8R8G8B8 rectangle into a surface of a common
// 16/32-bit RGB format. No filtering or palettes.
HRESULT WINAPI D3DXLoadSurfaceFromMemory(IDirect3DSurface8* pDestSurface, const PALETTEENTRY* pDestPalette,
                                         const RECT* pDestRect, LPCVOID pSrcMemory, D3DFORMAT SrcFormat,
                                         UINT SrcPitch, const PALETTEENTRY* pSrcPalette, const RECT* pSrcRect,
                                         DWORD Filter, D3DCOLOR ColorKey);

HRESULT WINAPI D3DXCreateTexture(IDirect3DDevice8* pDevice, UINT Width, UINT Height, UINT MipLevels,
                                 DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DTexture8** ppTexture);
