#include "shared.h"

struct VertexInput_Main {
  float2 position : POSITION;
  float4 color : COLOR;
  float2 tex_coord : TEXCOORD0;
};

struct PixelInput_Main {
  float4 position : SV_POSITION;
  float4 color : COLOR;
  float2 tex_coord : TEXCOORD0;
};

struct VertexInput_Passthrough {
  float2 position : POSITION;
  float2 tex_coord : TEXCOORD0;
};

struct PixelInput_Passthrough {
  float4 position : SV_POSITION;
  float2 tex_coord : TEXCOORD0;
};

struct VertexInput_Blur {
  float4 position : POSITION;
  float2 tex_coord : TEXCOORD0;
};

struct PixelInput_Blur {
  float4 position : POSITION;
  float2 tex_coord[BLUR_SIZE] : TEXCOORD0;
};