// D3DX8's D3DXAssembleShader on top of D3DX9 (Microsoft.DXSDK.D3DX package).
//
// This file must not include d3d8.h: d3dx9shader.h pulls in d3d9.h. The function below
// has C++ linkage and the D3DX8 parameter list, so it does not clash with D3DX9's
// extern "C" D3DXAssembleShader; ID3DXBuffer has the same layout in both versions.
//
// D3D8 vertex shaders ("vs.1.1") take their inputs from the vertex declaration passed to
// CreateVertexShader, while D3DX9 requires dcl_* statements for every v# register. So for
// vertex shaders we declare each used input before assembling and strip the resulting DCL
// instructions from the token stream, which leaves D3D8-compatible tokens.
#include <d3dx9shader.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <string>

static void LogFailure(HRESULT hr, LPD3DXBUFFER pErrors, const char* src, UINT len) {
  // Debug aid shared with the engine: append to ThrowLog.txt when it exists.
  FILE* fLog = fopen("ThrowLog.txt", "r+");
  if (fLog == NULL) return;
  fseek(fLog, 0, SEEK_END);
  fprintf(fLog, "D3DXAssembleShader failed (0x%08lX): %s\n--- source ---\n%.*s\n--- end ---\n", (unsigned long)hr,
          pErrors ? (const char*)pErrors->GetBufferPointer() : "", (int)len, src);
  fclose(fLog);
}

static bool IsVertexShader(const std::string& src) {
  size_t i = src.find_first_not_of(" \t\r\n");
  return i != std::string::npos && src.compare(i, 3, "vs.") == 0;
}

// Inserts "dcl_texcoordN vN" after the version line for every input register v0..v15 used.
static std::string DeclareInputs(const std::string& src) {
  bool used[16] = {};
  for (size_t i = 0; i + 1 < src.size(); i++) {
    if (src[i] == 'v' && isdigit((unsigned char)src[i + 1]) && (i == 0 || !isalnum((unsigned char)src[i - 1]))) {
      int n = atoi(src.c_str() + i + 1);
      if (n >= 0 && n < 16) used[n] = true;
    }
  }
  size_t eol = src.find('\n');
  std::string out = src.substr(0, eol == std::string::npos ? src.size() : eol + 1);
  if (eol == std::string::npos) out += "\n";
  char line[32];
  for (int n = 0; n < 16; n++) {
    if (used[n]) {
      snprintf(line, sizeof(line), "dcl_texcoord%d v%d\n", n, n);
      out += line;
    }
  }
  if (eol != std::string::npos) out += src.substr(eol + 1);
  return out;
}

// Removes DCL instructions (opcode 31: instruction token + 2 parameter tokens in 1.x shaders)
// that directly follow the version token and any comment blocks.
static void StripDeclarations(LPD3DXBUFFER pBuffer) {
  DWORD* tok = (DWORD*)pBuffer->GetBufferPointer();
  const DWORD count = pBuffer->GetBufferSize() / sizeof(DWORD);
  DWORD dst = 1, src = 1;
  while (src < count) {
    const DWORD t = tok[src];
    if ((t & 0xFFFF) == 0xFFFE) {  // comment block: keep it
      const DWORD len = 1 + ((t >> 16) & 0x7FFF);
      for (DWORD k = 0; k < len && src < count; k++) tok[dst++] = tok[src++];
    } else if ((t & 0xFFFF) == 31) {  // D3DSIO_DCL: drop it
      src += 3;
    } else {
      break;
    }
  }
  while (src < count) tok[dst++] = tok[src++];
  while (dst < count) tok[dst++] = 0x0000FFFF;  // pad with END tokens
}

HRESULT WINAPI D3DXAssembleShader(LPCVOID pSrcData, UINT SrcDataLen, DWORD Flags, LPD3DXBUFFER* ppConstants,
                                  LPD3DXBUFFER* ppCompiledShader, LPD3DXBUFFER* ppCompilationErrors) {
  if (ppConstants) *ppConstants = NULL;
  // D3DXASM_DEBUG (D3DX8) and D3DXSHADER_DEBUG (D3DX9) are both bit 0.
  const DWORD flags9 = (Flags & 1) ? D3DXSHADER_DEBUG : 0;

  std::string src((const char*)pSrcData, SrcDataLen);
  const bool vertex = IsVertexShader(src);
  if (vertex) src = DeclareInputs(src);

  LPD3DXBUFFER pErrors = NULL;
  HRESULT hr = ::D3DXAssembleShader(src.c_str(), (UINT)src.size(), NULL, NULL, flags9, ppCompiledShader, &pErrors);
  if (FAILED(hr)) LogFailure(hr, pErrors, src.c_str(), (UINT)src.size());
  else if (vertex && ppCompiledShader && *ppCompiledShader) StripDeclarations(*ppCompiledShader);

  if (ppCompilationErrors) *ppCompilationErrors = pErrors;
  else if (pErrors) pErrors->Release();
  return hr;
}
