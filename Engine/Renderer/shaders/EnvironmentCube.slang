import Common;
import IblSampling;

cbuffer CubeBake : register(b0)
{
    uint faceSize;
};
Texture2D<float4> panorama : register(t0);
SamplerState panoramaSampler : register(s0);
RWTexture2DArray<float4> cubeOutput : register(u0);

[shader("compute")]
[numthreads(8, 8, 1)]
void csMain(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= faceSize)) { return; }
    float3 radiance = float3(0.0);
    // Four samples cover the destination texel independently of the asset's mip settings.
    [unroll]
    for (uint i = 0; i < 4; ++i)
    {
        float2 offset = 0.25 + 0.5 * float2(i & 1, i >> 1);
        float3 direction = cubeDirection(id.z, (float2(id.xy) + offset) / float(faceSize));
        radiance += panorama.SampleLevel(panoramaSampler, environmentUv(direction), 0).rgb;
    }
    cubeOutput[id] = float4(radiance * 0.25, 1.0);
}
