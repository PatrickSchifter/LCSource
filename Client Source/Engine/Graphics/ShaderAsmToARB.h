#ifndef SE_INCL_SHADERASMTOARB_H
#define SE_INCL_SHADERASMTOARB_H
#ifdef PRAGMA_ONCE
  #pragma once
#endif

#include <string>

// Direct3D 8 shader assembly (vs.1.1, ps.1.1 to ps.1.4) translated into ARB_vertex_program and
// ARB_fragment_program text, so the engine's shaders run on OpenGL as they are.
//
// Vertex inputs as Direct3D declares them (GetShaderDeclaration_D3D) and where OpenGL reads them:
//   v0 position, v1 normal, v4 color, v5..v8 texcoord 0..3: the fixed arrays;
//   v2 weights, v3 bone indices (D3DCOLOR, components read as Direct3D does) and v9 tangent:
//   generic attributes below. Constants c0..c95 are program.env[] of each program type.

#define ARB_ATTRIB_TANGENT  1   // v9 (aliases vertex.weight, which the engine never uses)
#define ARB_ATTRIB_WEIGHTS  6   // v2, GFXWeight::aubWeights
#define ARB_ATTRIB_INDICES  7   // v3, GFXWeight::aubIndices

#define ARB_VERTEX_CONSTANTS 96

// Return false with the reason in strError when the program uses something with no translation.
bool TranslateVertexProgramToARB(const char *strD3D, std::string &strARB, std::string &strError);
bool TranslatePixelProgramToARB(const char *strD3D, std::string &strARB, std::string &strError);

#endif  /* include-once check. */
