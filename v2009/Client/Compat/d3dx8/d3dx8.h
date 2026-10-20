// Minimal replacement for the DirectX 8.1 SDK's d3dx8.h (see d3dx8math.h).
//
// ID3DXBuffer has the same COM layout in D3DX8 and D3DX9, so the shader assembler
// is implemented on top of D3DX9 in d3dx8_compat.cpp, a translation unit that never
// sees d3d8.h.
#pragma once

#include <d3d8.h>
#include "d3dx8math.h"

#define D3DXASM_DEBUG (1 << 0)
#define D3DX_FILTER_NONE (1 << 0)

#define _FACDD 0x876
#define MAKE_DDHRESULT(code) MAKE_HRESULT(1, _FACDD, code)
#define D3DXERR_INVALIDDATA MAKE_DDHRESULT(2905)

#undef INTERFACE
#define INTERFACE ID3DXBuffer
DECLARE_INTERFACE_(ID3DXBuffer, IUnknown) {
  STDMETHOD(QueryInterface)(THIS_ REFIID iid, LPVOID* ppv) PURE;
  STDMETHOD_(ULONG, AddRef)(THIS) PURE;
  STDMETHOD_(ULONG, Release)(THIS) PURE;
  STDMETHOD_(LPVOID, GetBufferPointer)(THIS) PURE;
  STDMETHOD_(DWORD, GetBufferSize)(THIS) PURE;
};
#undef INTERFACE
typedef ID3DXBuffer* LPD3DXBUFFER;

// D3DX8 signature: assembles vs.1.1 / ps.1.x source into shader tokens.
HRESULT WINAPI D3DXAssembleShader(LPCVOID pSrcData, UINT SrcDataLen, DWORD Flags,
                                  LPD3DXBUFFER* ppConstants, LPD3DXBUFFER* ppCompiledShader,
                                  LPD3DXBUFFER* ppCompilationErrors);

#include "d3dx8tex.h"
