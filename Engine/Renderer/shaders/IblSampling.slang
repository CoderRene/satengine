public static const float kIblPi = 3.14159265358979323846;

public float2 iblSequence(uint index, uint count)
{
    return float2((float(index) + 0.5) / float(count), float(reversebits(index)) * 2.3283064365386963e-10);
}

// D3D cube face order is +X, -X, +Y, -Y, +Z, -Z. Texel Y increases downward.
public float3 cubeDirection(uint face, float2 uv)
{
    float2 p = uv * 2.0 - 1.0;
    switch (face)
    {
    case 0: return normalize(float3(1.0, -p.y, -p.x));
    case 1: return normalize(float3(-1.0, -p.y, p.x));
    case 2: return normalize(float3(p.x, 1.0, p.y));
    case 3: return normalize(float3(p.x, -1.0, -p.y));
    case 4: return normalize(float3(p.x, -p.y, 1.0));
    default: return normalize(float3(-p.x, -p.y, -1.0));
    }
}

public float3 iblHemisphere(float phi, float cosTheta)
{
    float sinTheta = sqrt(saturate(1.0 - cosTheta * cosTheta));
    float sine, cosine;
    sincos(phi, sine, cosine);
    return float3(cosine * sinTheta, sine * sinTheta, cosTheta);
}

public float3 iblGgxHalfVector(float2 xi, float alphaSquared)
{
    float cosTheta = sqrt((1.0 - xi.x) / ((1.0 - xi.x) + alphaSquared * xi.x));
    return iblHemisphere(2.0 * kIblPi * xi.y, cosTheta);
}
