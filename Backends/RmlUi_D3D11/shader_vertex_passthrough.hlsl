#include "shared.hlsl"

PixelInput_Passthrough main(VertexInput_Passthrough input) {
  PixelInput_Passthrough output;

  output.position = float4(input.position, 0.0f, 1.0f);
  output.tex_coord = input.tex_coord;

  return output;
}
