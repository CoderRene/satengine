cbuffer MaterialParameters : register(b2)
{
    float4 baseColor;
    float3 emissive;
    float normalScale;
    float metallic;
    float roughness;
    float hasEmissiveTexture;
    float unlit;
    float alphaMode;
    float alphaCutoff;
    float specularStrength;
    float alphaPadding;
};

Texture2D<float4> baseMap : register(t0);
Texture2D<float4> metallicRoughnessMap : register(t1);
Texture2D<float4> normalMap : register(t2);
Texture2D<float4> emissiveMap : register(t3);
SamplerState materialSampler : register(s0);

public float materialSpecularStrength()
{
    return specularStrength;
}

public bool isUnlit()
{
    return unlit != 0.0;
}

public float4 sampleBaseColor(float2 uv)
{
    return baseColor * baseMap.Sample(materialSampler, uv);
}

public float materialOpacity(float alpha)
{
    if (alphaMode == 1.0) { clip(alpha - alphaCutoff); }
    return alphaMode == 2.0 ? alpha : 1.0;
}

public struct Surface
{
    public float3 albedo;
    public float3 normal;
    public float3 emission;
    public float metal;
    public float rough;
};

public Surface sampleMaterial(float2 uv, float3 albedo, float3 n, float4 tangent)
{
    Surface surface;
    surface.albedo = albedo;
    float2 mr = metallicRoughnessMap.Sample(materialSampler, uv).gb;
    surface.metal = metallic * mr.y;
    surface.rough = max(0.045, roughness * mr.x);
    float3 t = normalize(tangent.xyz - n * dot(n, tangent.xyz));
    float3 b = cross(n, t) * tangent.w;
    float3 mapped = normalMap.Sample(materialSampler, uv).xyz * 2.0 - 1.0;
    surface.normal = normalize(t * mapped.x * normalScale + b * mapped.y * normalScale + n * mapped.z);
    surface.emission = emissive * lerp(float3(1, 1, 1), emissiveMap.Sample(materialSampler, uv).rgb, hasEmissiveTexture);
    return surface;
}
