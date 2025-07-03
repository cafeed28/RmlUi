// "Creation" by Danilo Guanabara
// Based on https://www.shadertoy.com/view/XsXXDn

#include "shared.hlsl"

cbuffer CBuffer_Pixel_Creation : register(b0) {
  float m_value;
  float2 m_dimensions;
};

float4 main(PixelInput_Main input) : SV_TARGET {
  float t = m_value;
  float3 c = float3(0.0, 0.0, 0.0);
  float l = 0.0;

  for (int i = 0; i < 3; i++) {
    float2 p = input.tex_coord;
    float2 uv = p;
    p -= 0.5;
    p.x *= m_dimensions.x / m_dimensions.y;
    float z = t + float(i) * 0.07;
    l = length(p);
    uv += p / l * (sin(z) + 1.0) * abs(sin(l * 9.0 - z - z));

    float2 wrappedUV = uv - 1.0 * floor(uv / 1.0);
    c[i] = 0.01 / length(wrappedUV - 0.5);
  }

  return float4(c / l, input.color.a);
}
