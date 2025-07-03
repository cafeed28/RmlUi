#include "shared.hlsl"

cbuffer CBuffer_Pixel_Gradient : register(b0) {
  // linear: starting point
  // radial: center
  // conic: center
  float2 m_p;

  // linear: vector to ending point
  // radial: 2d curvature (inverse radius)
  // conic: angled unit vector
  float2 m_v;

  int m_func;
  int m_num_stops;

  float4 m_stop_colors[MAX_NUM_STOPS];

  // normalized, 0 -> starting point, 1 -> ending point
  float m_stop_positions[MAX_NUM_STOPS];
};

float4 mix_stop_colors(float t) {
  float4 color = m_stop_colors[0];

  for (int i = 1; i < m_num_stops; i++)
    color = lerp(color, m_stop_colors[i],
                 smoothstep(m_stop_positions[i - 1], m_stop_positions[i], t));

  return color;
}

float4 main(PixelInput_Main input) : SV_TARGET {
  float t = 0.0;

  if (m_func == GRADIENT_LINEAR || m_func == GRADIENT_REPEATING_LINEAR) {
    float dist_square = dot(m_v, m_v);
    float2 V = input.tex_coord - m_p;
    t = dot(m_v, V) / dist_square;
  } else if (m_func == GRADIENT_RADIAL || m_func == GRADIENT_REPEATING_RADIAL) {
    float2 V = input.tex_coord - m_p;
    t = length(m_v * V);
  } else if (m_func == GRADIENT_CONIC || m_func == GRADIENT_REPEATING_CONIC) {
    float2x2 R = float2x2(m_v.x, m_v.y, -m_v.y, m_v.x);
    float2 V = mul(R, input.tex_coord - m_p);
    t = 0.5 + atan2(-V.x, V.y) / (2.0 * PI);
  }

  if (m_func == GRADIENT_REPEATING_LINEAR ||
      m_func == GRADIENT_REPEATING_RADIAL ||
      m_func == GRADIENT_REPEATING_CONIC) {
    float t0 = m_stop_positions[0];
    float t1 = m_stop_positions[m_num_stops - 1];
    t = t0 + fmod(t - t0, t1 - t0);
  }

  return input.color * mix_stop_colors(t);
}
