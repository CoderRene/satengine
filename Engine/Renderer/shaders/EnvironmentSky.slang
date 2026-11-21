import Common;
#include "Fullscreen.slang"

cbuffer Sky : register(b0)
{
    float4x4 worldFromClip;
    float intensity;
    float hdr;
};

Texture2D<float4> environmentMap : register(t4);
SamplerState environmentSampler : register(s1);

[shader("fragment")]
float4 psMain(FullscreenVertex input) : SV_Target
{
    float2 ndc = input.uv * float2(2, -2) + float2(-1, 1);
    float3 ray = normalize(mul(worldFromClip, float4(ndc, 0.5, 1.0)).xyz);
    float2 uv = environmentUv(ray);
    float dx = ddx(uv.x);
    float dy = ddy(uv.x);
    float3 sky = environmentMap.SampleGrad(environmentSampler, uv,
        float2(dx - round(dx), ddx(uv.y)), float2(dy - round(dy), ddy(uv.y))).rgb * intensity;
    return float4(hdr != 0.0 ? sky : displayColor(sky), 1.0);
}
