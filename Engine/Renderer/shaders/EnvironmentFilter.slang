import IblSampling;

cbuffer FilterBake : register(b0)
{
    uint faceSize;
    uint diffuse;
    float roughness;
    float sourceTexelSolidAngle;
};
TextureCube<float4> sourceCube : register(t0);
SamplerState cubeSampler : register(s0);
RWTexture2DArray<float4> filteredCube : register(u0);

[shader("compute")]
[numthreads(8, 8, 1)]
void csMain(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= faceSize)) { return; }
    const uint sampleCount = 128;
    float3 n = cubeDirection(id.z, (float2(id.xy) + 0.5) / float(faceSize));
    float3 up = abs(n.z) < 0.999 ? float3(0, 0, 1) : float3(1, 0, 0);
    float3 tangent = normalize(cross(up, n));
    float3 bitangent = cross(n, tangent);
    float alpha = roughness * roughness;
    float alphaSquared = alpha * alpha;
    float3 radiance = float3(0.0);
    float weight = 0.0;
    [loop]
    for (uint i = 0; i < sampleCount; ++i)
    {
        float2 xi = iblSequence(i, sampleCount);
        float3 sampleVector;
        float pdf;
        float sampleWeight;
        if (diffuse != 0)
        {
            sampleVector = iblHemisphere(2.0 * kIblPi * xi.y, sqrt(1.0 - xi.x));
            pdf = sampleVector.z / kIblPi;
            sampleWeight = 1.0; // Cosine weighting leaves the average equal to irradiance / pi.
        }
        else
        {
            float3 h = iblGgxHalfVector(xi, alphaSquared);
            // The prefilter assumes N = V = R, so NoH cancels VoH in the reflected PDF.
            sampleVector = 2.0 * h.z * h - float3(0, 0, 1);
            if (sampleVector.z <= 0.0) { continue; }
            float nh2 = h.z * h.z;
            float denominator = (1.0 - nh2) + nh2 * alphaSquared;
            pdf = alphaSquared / (4.0 * kIblPi * denominator * denominator);
            sampleWeight = sampleVector.z;
        }
        float3 direction = tangent * sampleVector.x + bitangent * sampleVector.y + n * sampleVector.z;
        float lod = 0.5 * log2(1.0 / (float(sampleCount) * pdf * sourceTexelSolidAngle));
        // Keep the weighted sum in range for bright float32 panoramas.
        sampleWeight *= 1.0 / float(sampleCount);
        radiance += sourceCube.SampleLevel(cubeSampler, direction, max(0.0, lod)).rgb * sampleWeight;
        weight += sampleWeight;
    }
    filteredCube[id] = float4(radiance / max(weight, 1e-6 / float(sampleCount)), 1.0);
}
