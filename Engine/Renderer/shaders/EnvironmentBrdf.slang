import IblSampling;

RWTexture2D<float2> brdfOutput : register(u0);

[shader("compute")]
[numthreads(8, 8, 1)]
void csMain(uint3 id : SV_DispatchThreadID)
{
    uint width, height;
    brdfOutput.GetDimensions(width, height);
    if (id.x >= width || id.y >= height) { return; }
    const uint sampleCount = 256;
    float2 uv = (float2(id.xy) + 0.5) / float2(width, height);
    float nv = uv.x;
    float alpha = uv.y * uv.y;
    float k = alpha * 0.5; // Schlick-Smith geometry for IBL.
    float3 v = float3(sqrt(1.0 - nv * nv), 0.0, nv);
    float2 coefficients = float2(0.0);
    [loop]
    for (uint i = 0; i < sampleCount; ++i)
    {
        float3 h = iblGgxHalfVector(iblSequence(i, sampleCount), alpha * alpha);
        float vh = saturate(dot(v, h));
        float nl = 2.0 * vh * h.z - nv;
        if (nl > 0.0)
        {
            float visibility = (nl * vh) / (lerp(nv, 1.0, k) * lerp(nl, 1.0, k) * h.z);
            float f = 1.0 - vh;
            float f2 = f * f;
            float fresnel = f2 * f2 * f;
            coefficients += visibility * float2(1.0 - fresnel, fresnel);
        }
    }
    brdfOutput[id.xy] = coefficients / float(sampleCount);
}
