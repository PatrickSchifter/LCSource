#ifndef SE_INCL_SHADERARBTOGLSL_H
#define SE_INCL_SHADERARBTOGLSL_H
#ifdef PRAGMA_ONCE
  #pragma once
#endif

#include <string>
#include "GpuDevice.h"

// The ARB programs that ShaderAsmToARB.cpp makes from Direct3D's shader assembly, translated into
// GLSL 4.50 for Vulkan (compiled into SPIR-V by the Vulkan device, VulkanDevice.cpp), as they are
// into MSL for Metal (ShaderARBTranslate.cpp has both).
//
// The interface (the same as the fixed function emulation's, the GLSL_* macros below), all in
// descriptor set 0, pushed with each draw:
//  - binding 0: the vertex constants (GpuVPConst, or GpuFFVertexConst), std140;
//  - binding 1: the fragment constants (GpuPPConst, or GpuFFFragmentConst), std140;
//  - bindings 2..10: the vertex streams (GpuStream + 2), storage buffers read by vertex index from
//    the 32-bit word given for each in the push constants (GpuStreamBases), as Metal reads them;
//  - bindings 11..14: the textures of units 0..3 with their samplers;
//  - between the stages, the block GpuVSOut at location 0.
// A vertex program is "main" of a vertex shader, a pixel program of a fragment shader.

#define GLSL_BINDING_VERTEXCONST   0
#define GLSL_BINDING_FRAGMENTCONST 1
#define GLSL_BINDING_STREAMS       2
#define GLSL_BINDING_TEXTURES      11
#define GLSL_BINDINGS              15

// the push constants: where each stream starts in its buffer, in 32-bit words
struct GpuStreamBases {
  uint32_t aulBase[GPUS_COUNT];
};

// what a vertex shader starts with: the output block, and the streams with functions that read
// them (as the fixed function emulation and the vertex programs do)
#define GLSL_VERTEX_INTERFACE \
  "#version 450\n" \
  "layout(location = 0) out GpuVSOut { vec4 col0; vec4 col1; vec4 tex0; vec4 tex1; vec4 tex2; vec4 tex3; } o;\n" \
  "out float gl_ClipDistance[1];\n" \
  "layout(push_constant) uniform GpuStreamBases { uint b[9]; } P;\n" \
  "layout(set = 0, binding = 2, std430) readonly buffer GpuStreamPos { float f[]; } s_pos;\n" \
  "layout(set = 0, binding = 3, std430) readonly buffer GpuStreamCol { uint u[]; } s_col;\n" \
  "layout(set = 0, binding = 4, std430) readonly buffer GpuStreamNor { float f[]; } s_nor;\n" \
  "layout(set = 0, binding = 5, std430) readonly buffer GpuStreamTex0 { float f[]; } s_tex0;\n" \
  "layout(set = 0, binding = 6, std430) readonly buffer GpuStreamTex1 { float f[]; } s_tex1;\n" \
  "layout(set = 0, binding = 7, std430) readonly buffer GpuStreamTex2 { float f[]; } s_tex2;\n" \
  "layout(set = 0, binding = 8, std430) readonly buffer GpuStreamTex3 { float f[]; } s_tex3;\n" \
  "layout(set = 0, binding = 9, std430) readonly buffer GpuStreamWgh { uint u[]; } s_wgh;\n" \
  "layout(set = 0, binding = 10, std430) readonly buffer GpuStreamTan { float f[]; } s_tan;\n" \
  "vec3 GpuPos(uint vid) { const uint i = P.b[0] + vid*3u; return vec3(s_pos.f[i], s_pos.f[i+1u], s_pos.f[i+2u]); }\n" \
  "vec4 GpuCol(uint vid) { return unpackUnorm4x8(s_col.u[P.b[1] + vid]); }\n" \
  "vec3 GpuNor(uint vid) { const uint i = P.b[2] + vid*3u; return vec3(s_nor.f[i], s_nor.f[i+1u], s_nor.f[i+2u]); }\n" \
  "vec4 GpuTex0(uint vid, bool b4) { if (b4) { const uint i = P.b[3] + vid*4u; return vec4(s_tex0.f[i], s_tex0.f[i+1u], s_tex0.f[i+2u], s_tex0.f[i+3u]); }\n" \
  "  const uint i = P.b[3] + vid*2u; return vec4(s_tex0.f[i], s_tex0.f[i+1u], 0.0f, 1.0f); }\n" \
  "vec4 GpuTex1(uint vid, bool b4) { if (b4) { const uint i = P.b[4] + vid*4u; return vec4(s_tex1.f[i], s_tex1.f[i+1u], s_tex1.f[i+2u], s_tex1.f[i+3u]); }\n" \
  "  const uint i = P.b[4] + vid*2u; return vec4(s_tex1.f[i], s_tex1.f[i+1u], 0.0f, 1.0f); }\n" \
  "vec4 GpuTex2(uint vid, bool b4) { if (b4) { const uint i = P.b[5] + vid*4u; return vec4(s_tex2.f[i], s_tex2.f[i+1u], s_tex2.f[i+2u], s_tex2.f[i+3u]); }\n" \
  "  const uint i = P.b[5] + vid*2u; return vec4(s_tex2.f[i], s_tex2.f[i+1u], 0.0f, 1.0f); }\n" \
  "vec4 GpuTex3(uint vid, bool b4) { if (b4) { const uint i = P.b[6] + vid*4u; return vec4(s_tex3.f[i], s_tex3.f[i+1u], s_tex3.f[i+2u], s_tex3.f[i+3u]); }\n" \
  "  const uint i = P.b[6] + vid*2u; return vec4(s_tex3.f[i], s_tex3.f[i+1u], 0.0f, 1.0f); }\n" \
  "vec4 GpuIdx(uint vid) { return unpackUnorm4x8(s_wgh.u[P.b[7] + vid*2u]); }\n" \
  "vec4 GpuWgh(uint vid) { return unpackUnorm4x8(s_wgh.u[P.b[7] + vid*2u + 1u]); }\n" \
  "vec4 GpuTan(uint vid) { const uint i = P.b[8] + vid*4u; return vec4(s_tan.f[i], s_tan.f[i+1u], s_tan.f[i+2u], s_tan.f[i+3u]); }\n"

// what a fragment shader starts with: the input block, the color out, and the textures
#define GLSL_FRAGMENT_INTERFACE \
  "#version 450\n" \
  "layout(location = 0) in GpuVSOut { vec4 col0; vec4 col1; vec4 tex0; vec4 tex1; vec4 tex2; vec4 tex3; } vin;\n" \
  "layout(location = 0) out vec4 gpu_FragColor;\n" \
  "layout(set = 0, binding = 11) uniform sampler2D tex0;\n" \
  "layout(set = 0, binding = 12) uniform sampler2D tex1;\n" \
  "layout(set = 0, binding = 13) uniform sampler2D tex2;\n" \
  "layout(set = 0, binding = 14) uniform sampler2D tex3;\n"

// Return false with the reason in strError when the program has something with no translation.
bool TranslateARBToGLSL(const std::string &strARB, std::string &strGLSL, std::string &strError);

#endif  /* include-once check. */
