#include "shared.hlsl"

cbuffer CBuffer_Vertex_Blur : register(b0) {
  float2 m_texel_offset;
};

PixelInput_Blur main(VertexInput_Blur input) {
  PixelInput_Blur output;

  for (int i = 0; i < BLUR_SIZE; i++)
    output.tex_coord[i] = input.tex_coord - float(i - BLUR_NUM_WEIGHTS + 1) * m_texel_offset;

  output.position = float4(input.position.xy, 1.0, 1.0);

  return output;
}

/*
uniform vec2 _texelOffset;

in vec3 inPosition;
in vec2 inTexCoord0;

out vec2 fragTexCoord[BLUR_SIZE];

void main() {
  for (int i = 0; i < BLUR_SIZE; i++)
    fragTexCoord[i] = inTexCoord0 - float(i - BLUR_NUM_WEIGHTS + 1) * _texelOffset;
  gl_Position = vec4(inPosition, 1.0);
}
 */