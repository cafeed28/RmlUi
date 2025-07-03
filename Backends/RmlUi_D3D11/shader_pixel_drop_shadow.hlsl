#include "shared.hlsl"

Texture2D g_texture : register(t0);
SamplerState g_sampler : register(s0);

cbuffer CBuffer_Pixel_DropShadow : register(b0) {
  float2 m_tex_coord_min;
  float2 m_tex_coord_max;
  float4 m_color;
};

float4 main(PixelInput_Passthrough input) : SV_TARGET {
  // float2 in_region = step(m_tex_coord_min, input.tex_coord) * step(input.tex_coord, m_tex_coord_max);
  float2 in_region = float2(1.0, 1.0);
  return g_texture.Sample(g_sampler, input.tex_coord).a * in_region.x * in_region.y * m_color;
}