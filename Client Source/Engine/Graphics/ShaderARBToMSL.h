#ifndef SE_INCL_SHADERARBTOMSL_H
#define SE_INCL_SHADERARBTOMSL_H
#ifdef PRAGMA_ONCE
  #pragma once
#endif

#include <string>
#include "GpuDevice.h"

// The ARB programs that ShaderAsmToARB.cpp makes from Direct3D's shader assembly, translated into
// Metal Shading Language for the Metal device (MetalDevice.mm). The ARB text is the one
// that runs on OpenGL, so Direct3D's semantics (modifiers, matrix macros, co-issue, registers read
// before written) are already written out in it; this is one MSL statement per ARB instruction.
//
// The interface (the same as the fixed function emulation's, MSL_PROGRAM_INTERFACE below):
//  - a vertex program is the function "vp": constants GpuVPConst at buffer 0, the vertex streams
//    at buffers 1.. (GpuStream + 1), read by vertex index; the output is MtlVSOut;
//  - a pixel program is the function "pp": MtlPSIn in, constants GpuPPConst at buffer 0,
//    textures and samplers 0..3.

// what both the generated programs and the fixed function emulation start with
#define MSL_PROGRAM_INTERFACE \
  "#include <metal_stdlib>\n" \
  "using namespace metal;\n" \
  "struct MtlVSOut {\n" \
  "  float4 pos [[position]];\n" \
  "  float4 col0 [[user(col0)]];\n" \
  "  float4 col1 [[user(col1)]];\n" \
  "  float4 tex0 [[user(tex0)]];\n" \
  "  float4 tex1 [[user(tex1)]];\n" \
  "  float4 tex2 [[user(tex2)]];\n" \
  "  float4 tex3 [[user(tex3)]];\n" \
  "  float fPointSize [[point_size]];\n" \
  "  float afClip [[clip_distance]] [1];\n" \
  "};\n" \
  "struct MtlPSIn {\n" \
  "  float4 pos [[position]];\n" \
  "  float4 col0 [[user(col0)]];\n" \
  "  float4 col1 [[user(col1)]];\n" \
  "  float4 tex0 [[user(tex0)]];\n" \
  "  float4 tex1 [[user(tex1)]];\n" \
  "  float4 tex2 [[user(tex2)]];\n" \
  "  float4 tex3 [[user(tex3)]];\n" \
  "};\n"

// (the constants: GpuVPConst and GpuPPConst, GpuDevice.h)

// Return false with the reason in strError when the program has something with no translation.
bool TranslateARBToMSL(const std::string &strARB, std::string &strMSL, std::string &strError);

#endif  /* include-once check. */
