// D3DX8 texture helpers used by the client, implemented directly on Direct3D 8.
#include <windows.h>
#include "d3dx8tex.h"

static void PutPixel(BYTE* dst, D3DFORMAT fmt, DWORD argb) {
  const DWORD a = argb >> 24, r = (argb >> 16) & 0xFF, g = (argb >> 8) & 0xFF, b = argb & 0xFF;
  switch (fmt) {
    case D3DFMT_A8R8G8B8: *(DWORD*)dst = argb; break;
    case D3DFMT_X8R8G8B8: *(DWORD*)dst = 0xFF000000 | (argb & 0x00FFFFFF); break;
    case D3DFMT_R8G8B8: dst[0] = (BYTE)b; dst[1] = (BYTE)g; dst[2] = (BYTE)r; break;
    case D3DFMT_R5G6B5: *(WORD*)dst = (WORD)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)); break;
    case D3DFMT_X1R5G5B5: *(WORD*)dst = (WORD)(0x8000 | ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3)); break;
    case D3DFMT_A1R5G5B5: *(WORD*)dst = (WORD)(((a >> 7) << 15) | ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3)); break;
    case D3DFMT_A4R4G4B4: *(WORD*)dst = (WORD)(((a >> 4) << 12) | ((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4)); break;
    case D3DFMT_X4R4G4B4: *(WORD*)dst = (WORD)(0xF000 | ((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4)); break;
    default: break;
  }
}

static UINT BytesPerPixel(D3DFORMAT fmt) {
  switch (fmt) {
    case D3DFMT_A8R8G8B8: case D3DFMT_X8R8G8B8: return 4;
    case D3DFMT_R8G8B8: return 3;
    case D3DFMT_R5G6B5: case D3DFMT_X1R5G5B5: case D3DFMT_A1R5G5B5:
    case D3DFMT_A4R4G4B4: case D3DFMT_X4R4G4B4: return 2;
    default: return 0;
  }
}

HRESULT WINAPI D3DXLoadSurfaceFromMemory(IDirect3DSurface8* pDestSurface, const PALETTEENTRY*, const RECT* pDestRect,
                                         LPCVOID pSrcMemory, D3DFORMAT SrcFormat, UINT SrcPitch,
                                         const PALETTEENTRY*, const RECT* pSrcRect, DWORD, D3DCOLOR) {
  if (!pDestSurface || !pSrcMemory || !pSrcRect || SrcFormat != D3DFMT_A8R8G8B8) return D3DERR_INVALIDCALL;
  D3DSURFACE_DESC desc;
  if (FAILED(pDestSurface->GetDesc(&desc))) return D3DERR_INVALIDCALL;
  const UINT bpp = BytesPerPixel(desc.Format);
  if (bpp == 0) return D3DERR_INVALIDCALL;

  RECT dst = pDestRect ? *pDestRect : RECT{0, 0, (LONG)desc.Width, (LONG)desc.Height};
  const LONG w = min(dst.right - dst.left, pSrcRect->right - pSrcRect->left);
  const LONG h = min(dst.bottom - dst.top, pSrcRect->bottom - pSrcRect->top);

  D3DLOCKED_RECT lr;
  if (FAILED(pDestSurface->LockRect(&lr, &dst, 0))) return D3DERR_INVALIDCALL;
  const BYTE* src = (const BYTE*)pSrcMemory + pSrcRect->top * SrcPitch + pSrcRect->left * 4;
  for (LONG y = 0; y < h; y++) {
    const DWORD* s = (const DWORD*)(src + y * SrcPitch);
    BYTE* d = (BYTE*)lr.pBits + y * lr.Pitch;
    for (LONG x = 0; x < w; x++) PutPixel(d + x * bpp, desc.Format, s[x]);
  }
  pDestSurface->UnlockRect();
  return D3D_OK;
}

HRESULT WINAPI D3DXCreateTexture(IDirect3DDevice8* pDevice, UINT Width, UINT Height, UINT MipLevels, DWORD Usage,
                                 D3DFORMAT Format, D3DPOOL Pool, IDirect3DTexture8** ppTexture) {
  if (!pDevice) return D3DERR_INVALIDCALL;
  return pDevice->CreateTexture(Width, Height, MipLevels, Usage, Format, Pool, ppTexture);
}
