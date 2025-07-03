#include "shared.hlsl"

Texture2D g_texture : register(t0);
SamplerState g_sampler : register(s0);

cbuffer CBuffer_Pixel_ColorMatrix : register(b0) {
  float4x4 m_color_matrix;
};

float4 main(PixelInput_Passthrough input) : SV_TARGET {
  // The general case uses a 4x5 color matrix for full rgba transformation, plus
  // a constant term with the last column. However, we only consider the case of
  // rgb transformations. Thus, we could in principle use a 3x4 matrix, but we
  // keep the alpha row for simplicity.
  // In the general case we should do the matrix transformation in
  // non-premultiplied space. However, without alpha transformations, we can do
  // it directly in premultiplied space to avoid the extra division and
  // multiplication steps. In this space, the constant term needs to be
  // multiplied by the alpha value, instead of unity.
  float4 tex_color = g_texture.Sample(g_sampler, input.tex_coord);
  float3 transformed_color = mul(m_color_matrix, tex_color).rgb;

  return float4(transformed_color, tex_color.a);
}