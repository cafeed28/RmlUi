#include "shared.hlsl"

Texture2D g_texture : register(t0);
SamplerState g_sampler : register(s0);

cbuffer CBuffer_Pixel_Blur : register(b0) {
  float2 m_tex_coord_min;
  float2 m_tex_coord_max;
  float m_weights[BLUR_NUM_WEIGHTS];
};

float4 main(PixelInput_Blur input) : SV_TARGET {
  float4 color = float4(0.0, 0.0, 0.0, 0.0);

  for (int i = 0; i < BLUR_SIZE; i++) {
    float2 in_region = step(m_tex_coord_min, input.tex_coord[i]) * step(input.tex_coord[i], m_tex_coord_max);
    color += g_texture.Sample(g_sampler, input.tex_coord[i]) * in_region.x * in_region.y * m_weights[abs(i - BLUR_NUM_WEIGHTS + 1)];
  }

  return color;
}

/* 
uniform sampler2D _tex;
uniform float _weights[BLUR_NUM_WEIGHTS];
uniform vec2 _texCoordMin;
uniform vec2 _texCoordMax;

in vec2 fragTexCoord[BLUR_SIZE];
out vec4 finalColor;

void main() {
  vec4 color = vec4(0.0, 0.0, 0.0, 0.0);
  for(int i = 0; i < BLUR_SIZE; i++)
  {
    vec2 in_region = step(_texCoordMin, fragTexCoord[i]) * step(fragTexCoord[i], _texCoordMax);
    color += texture(_tex, fragTexCoord[i]) * in_region.x * in_region.y * _weights[abs(i - BLUR_NUM_WEIGHTS + 1)];
  }
  finalColor = color;
} */