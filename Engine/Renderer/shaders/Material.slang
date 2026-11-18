struct MaterialUv
{
    float4 rows;
    float2 offset;
    uint texCoord;
    uint useDerivativeFrame;
};

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
    float occlusionStrength;
    MaterialUv textureUv[7];
    float transmissionFactor;
    float indexOfRefraction;
    float thicknessFactor;
    float attenuationDistance;
    float3 attenuationColor;
    float transmissionMip;
    MaterialUv coatUv[3];
    float clearcoatFactor;
    float clearcoatRoughnessFactor;
    float clearcoatNormalScale;
    float hasClearcoatNormal;
    float transmissionRoughnessStrength;
};

Texture2D<float4> baseMap : register(t0);
Texture2D<float4> metallicRoughnessMap : register(t1);
Texture2D<float4> normalMap : register(t2);
Texture2D<float4> emissiveMap : register(t3);
Texture2D<float4> occlusionMap : register(t6);
Texture2D<float4> transmissionMap : register(t7);
Texture2D<float4> thicknessMap : register(t8);
Texture2D<float4> clearcoatMap : register(t9);
Texture2D<float4> clearcoatRoughnessMap : register(t10);
Texture2D<float4> clearcoatNormalMap : register(t11);
SamplerState materialSampler : register(s0);
SamplerState metallicRoughnessSampler : register(s3);
SamplerState normalSampler : register(s4);
SamplerState emissiveSampler : register(s5);
SamplerState occlusionSampler : register(s6);
SamplerState transmissionSampler : register(s7);
SamplerState thicknessSampler : register(s8);
SamplerState clearcoatSampler : register(s9);
SamplerState clearcoatRoughnessSampler : register(s10);
SamplerState clearcoatNormalSampler : register(s11);

float2 mapUv(MaterialUv mapping, float2 uv0, float2 uv1)
{
    float2 uv = mapping.texCoord == 0 ? uv0 : uv1;
    return float2(dot(mapping.rows.xy, uv), dot(mapping.rows.zw, uv)) + mapping.offset;
}

public float materialIor()
{
    return indexOfRefraction;
}

public float sampleTransmission(float2 uv0, float2 uv1)
{
    return transmissionFactor > 0.0 ?
        transmissionFactor * transmissionMap.Sample(transmissionSampler, mapUv(textureUv[5], uv0, uv1)).r : 0.0;
}

public float materialTransmissionMip(float roughnessLod)
{
    return roughnessLod * transmissionRoughnessStrength + (transmissionMip - 1.0);
}

public float sampleThickness(float2 uv0, float2 uv1)
{
    return thicknessFactor > 0.0 ?
        thicknessFactor * thicknessMap.Sample(thicknessSampler, mapUv(textureUv[6], uv0, uv1)).g : 0.0;
}

public float3 volumeAttenuation(float distance)
{
    if (attenuationDistance == 0.0 || distance == 0.0) { return float3(1.0, 1.0, 1.0); }
    return pow(attenuationColor, min(distance / attenuationDistance, 1e6));
}

public float materialSpecularStrength()
{
    return specularStrength;
}

public bool isUnlit()
{
    return unlit != 0.0;
}

public float4 sampleBaseColor(float2 uv0, float2 uv1)
{
    return baseColor * baseMap.Sample(materialSampler, mapUv(textureUv[0], uv0, uv1));
}

public float4 sampleBaseColor(float2 uv)
{
    return sampleBaseColor(uv, uv);
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
    public float perceptualRoughness;
    public float occlusion;
};

float3 mapNormal(float3 n, float4 tangent, float3 position, float2 uv, uint useDerivativeFrame,
    float3 mapped, float scale)
{
    if (useDerivativeFrame != 0)
    {
        // Reconstruct the frame when the normal texture changes the UV basis.
        float2 dx = ddx(uv);
        float2 dy = ddy(uv);
        float3 px = ddx(position);
        float3 py = ddy(position);
        float det = dx.x * dy.y - dx.y * dy.x;
        float sign = det < 0.0 ? -1.0 : 1.0;
        float3 t = (px * dy.y - py * dx.y) * sign;
        t -= n * dot(n, t);
        if (dot(t, t) > 0.0 && det != 0.0)
        {
            float3 b = (py * dx.x - px * dy.x) * sign;
            tangent = float4(t, dot(cross(n, t), b) < 0.0 ? -1.0 : 1.0);
        }
    }
    float3 t = normalize(tangent.xyz - n * dot(n, tangent.xyz));
    float3 b = cross(n, t) * tangent.w;
    return normalize(t * mapped.x * scale + b * mapped.y * scale + n * mapped.z);
}

public struct Clearcoat
{
    public float3 normal;
    public float roughness;
    public float factor;
};

public Clearcoat sampleClearcoat(float2 uv0, float2 uv1, float3 n, float4 tangent, float3 position, bool frontFace)
{
    Clearcoat coat;
    coat.factor = clearcoatFactor;
    coat.roughness = clearcoatRoughnessFactor;
    coat.normal = n;
    if (clearcoatFactor > 0.0)
    {
        coat.factor *= clearcoatMap.Sample(clearcoatSampler, mapUv(coatUv[0], uv0, uv1)).r;
        coat.roughness *= clearcoatRoughnessMap.Sample(clearcoatRoughnessSampler, mapUv(coatUv[1], uv0, uv1)).g;
        if (hasClearcoatNormal != 0.0)
        {
            float2 uv = mapUv(coatUv[2], uv0, uv1);
            float3 mapped = clearcoatNormalMap.Sample(clearcoatNormalSampler, uv).xyz * 2.0 - 1.0;
            coat.normal = mapNormal(n, tangent, position, uv, coatUv[2].useDerivativeFrame, mapped, clearcoatNormalScale);
        }
    }
    if (!frontFace) { coat.normal = -coat.normal; }
    return coat;
}

public Surface sampleMaterial(float2 uv0, float2 uv1, float3 albedo, float3 n, float4 tangent,
    float3 position, bool frontFace)
{
    Surface surface;
    surface.albedo = albedo;
    float2 mr = metallicRoughnessMap.Sample(metallicRoughnessSampler, mapUv(textureUv[1], uv0, uv1)).gb;
    surface.metal = metallic * mr.y;
    surface.perceptualRoughness = roughness * mr.x;
    surface.rough = max(0.045, surface.perceptualRoughness);
    float2 normalUv = mapUv(textureUv[2], uv0, uv1);
    float3 mapped = normalMap.Sample(normalSampler, normalUv).xyz * 2.0 - 1.0;
    surface.normal = mapNormal(n, tangent, position, normalUv, textureUv[2].useDerivativeFrame, mapped, normalScale);
    if (!frontFace) { surface.normal = -surface.normal; }
    surface.emission = emissive * lerp(float3(1, 1, 1),
        emissiveMap.Sample(emissiveSampler, mapUv(textureUv[3], uv0, uv1)).rgb, hasEmissiveTexture);
    surface.occlusion = occlusionStrength > 0.0 ?
        lerp(1.0, occlusionMap.Sample(occlusionSampler, mapUv(textureUv[4], uv0, uv1)).r, occlusionStrength) : 1.0;
    return surface;
}

public Surface sampleMaterial(float2 uv, float3 albedo, float3 n, float4 tangent)
{
    return sampleMaterial(uv, uv, albedo, n, tangent, float3(0, 0, 0), true);
}
