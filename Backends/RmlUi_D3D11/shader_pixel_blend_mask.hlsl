#include "shared.hlsl"

Texture2D g_texture : register(t0);
Texture2D g_texture_mask : register(t1);
SamplerState g_sampler : register(s0);

float4 main(PixelInput_Passthrough input) : SV_TARGET {
  float4 tex_color = g_texture.Sample(g_sampler, input.tex_coord);
  float mask_alpha = g_texture_mask.Sample(g_sampler, input.tex_coord).a;

  return tex_color * mask_alpha;
}