// Development check for the D3DX8 shader shim: assembles each vertex shader file given on the
// command line with D3DXAssembleShader (d3dx8_shader.cpp) and creates it on a Direct3D 8 device,
// printing the HRESULT and the shader tokens. Shows which shaders the D3D8 runtime rejects.
//
//   ShaderCheck <shader.vsh> [...]
//
// The vertex declaration is the engine's for SKA models (Gfx_Direct3D.cpp: position, normal,
// texture coordinates, blend weights and indices).
#include <windows.h>
#include <d3d8.h>
#include <stdio.h>
#include <string>

struct ID3DXBuffer;
HRESULT WINAPI D3DXAssembleShader(LPCVOID pSrcData, UINT SrcDataLen, DWORD Flags, ID3DXBuffer** ppConstants,
                                  ID3DXBuffer** ppCompiledShader, ID3DXBuffer** ppCompilationErrors);
// ID3DXBuffer as seen through IUnknown: GetBufferPointer and GetBufferSize follow the IUnknown methods.
struct ID3DXBuffer : IUnknown {
  virtual LPVOID STDMETHODCALLTYPE GetBufferPointer() = 0;
  virtual DWORD STDMETHODCALLTYPE GetBufferSize() = 0;
};

static const DWORD _adwDeclaration[] = {
  D3DVSD_STREAM(0), D3DVSD_REG(0, D3DVSDT_FLOAT3),
  D3DVSD_STREAM(2), D3DVSD_REG(5, D3DVSDT_FLOAT2),
  D3DVSD_STREAM(6), D3DVSD_REG(1, D3DVSDT_FLOAT3),
  D3DVSD_STREAM(7), D3DVSD_REG(3, D3DVSDT_D3DCOLOR), D3DVSD_REG(2, D3DVSDT_D3DCOLOR),
  D3DVSD_END()
};

static std::string ReadFile(const char* strName) {
  std::string str;
  FILE* f = fopen(strName, "rb");
  if (f == NULL) return str;
  char buf[4096];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), f)) > 0) str.append(buf, n);
  fclose(f);
  return str;
}

int main(int argc, char* argv[]) {
  HWND hWnd = CreateWindowA("STATIC", "ShaderCheck", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, NULL, NULL, NULL, NULL);
  // Like the engine, load D3D8.DLL at run time (there is no import library for it).
  typedef IDirect3D8* (WINAPI *Direct3DCreate8_t)(UINT);
  HMODULE hD3D8 = LoadLibraryA("d3d8.dll");
  Direct3DCreate8_t pCreate = hD3D8 ? (Direct3DCreate8_t)GetProcAddress(hD3D8, "Direct3DCreate8") : NULL;
  IDirect3D8* pD3D = pCreate ? pCreate(D3D_SDK_VERSION) : NULL;
  if (pD3D == NULL) { printf("Direct3DCreate8 failed\n"); return 1; }
  D3DDISPLAYMODE mode;
  pD3D->GetAdapterDisplayMode(D3DADAPTER_DEFAULT, &mode);
  D3DPRESENT_PARAMETERS pp = {};
  pp.Windowed = TRUE;
  pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
  pp.BackBufferFormat = mode.Format;
  IDirect3DDevice8* pDevice = NULL;
  HRESULT hr = pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hWnd, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &pDevice);
  if (FAILED(hr)) { printf("CreateDevice failed (0x%08lX)\n", (unsigned long)hr); return 1; }

  for (int i = 1; i < argc; i++) {
    std::string src = ReadFile(argv[i]);
    ID3DXBuffer* pCode = NULL;
    hr = D3DXAssembleShader(src.c_str(), (UINT)src.size(), 0, NULL, &pCode, NULL);
    if (FAILED(hr)) { printf("%s: assembly failed (0x%08lX)\n", argv[i], (unsigned long)hr); continue; }
    DWORD dwHandle = 0;
    hr = pDevice->CreateVertexShader(_adwDeclaration, (const DWORD*)pCode->GetBufferPointer(), &dwHandle, 0);
    printf("%s: CreateVertexShader 0x%08lX\n", argv[i], (unsigned long)hr);
    const DWORD* pdw = (const DWORD*)pCode->GetBufferPointer();
    for (DWORD t = 0; t < pCode->GetBufferSize() / 4; t++) printf("%08lX%c", (unsigned long)pdw[t], t % 8 == 7 ? '\n' : ' ');
    printf("\n");
    if (SUCCEEDED(hr)) pDevice->DeleteVertexShader(dwHandle);
    pCode->Release();
  }
  pDevice->Release();
  pD3D->Release();
  return 0;
}
