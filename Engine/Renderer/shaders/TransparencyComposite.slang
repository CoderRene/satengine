#include "Fullscreen.slang"

Texture2D<float4> accumulated : register(t0);
Texture2D<float> coverage : register(t1);

[shader("fragment")]
float4 psMain(FullscreenVertex input) : SV_Target
{
    int3 pixel = int3(int2(input.position.xy), 0);
    float4 color = accumulated.Load(pixel);
    return float4(color.rgb / max(color.a, 0.0001), 1.0 - coverage.Load(pixel));
}
