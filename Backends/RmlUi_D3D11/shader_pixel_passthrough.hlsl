#include "shared.hlsl"

Texture2D g_texture : register(t0);
SamplerState g_sampler : register(s0);

float4 main(PixelInput_Passthrough input) : SV_TARGET {
  return g_texture.Sample(g_sampler, input.tex_coord);
}