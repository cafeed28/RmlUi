#include "shared.hlsl"

cbuffer CBuffer_Vertex_Main : register(b0) {
  float4x4 m_transform;
  float2 m_translate;
};

PixelInput_Main main(VertexInput_Main input) {
  PixelInput_Main output;

  float2 translated = input.position + m_translate;
  output.position = mul(m_transform, float4(translated, 0.0f, 1.0f));

  output.color = input.color;
  output.tex_coord = input.tex_coord;

  return output;
}
