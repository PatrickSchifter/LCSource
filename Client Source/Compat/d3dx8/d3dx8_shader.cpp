// D3DX8's D3DXAssembleShader on top of D3DX9 (Microsoft.DXSDK.D3DX package).
//
// This file must not include d3d8.h: d3dx9shader.h pulls in d3d9.h. The function below
// has C++ linkage and the D3DX8 parameter list, so it does not clash with D3DX9's
// extern "C" D3DXAssembleShader; ID3DXBuffer has the same layout in both versions.
#include <d3dx9shader.h>

HRESULT WINAPI D3DXAssembleShader(LPCVOID pSrcData, UINT SrcDataLen, DWORD Flags, LPD3DXBUFFER* ppConstants,
                                  LPD3DXBUFFER* ppCompiledShader, LPD3DXBUFFER* ppCompilationErrors) {
  if (ppConstants) *ppConstants = NULL;
  // D3DXASM_DEBUG (D3DX8) and D3DXSHADER_DEBUG (D3DX9) are both bit 0.
  const DWORD flags9 = (Flags & 1) ? D3DXSHADER_DEBUG : 0;
  return ::D3DXAssembleShader((LPCSTR)pSrcData, SrcDataLen, NULL, NULL, flags9, ppCompiledShader,
                              ppCompilationErrors);
}
