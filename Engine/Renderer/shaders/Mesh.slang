import Common;
import Brdf;
import Lighting;
import Material;
import MeshFragment;

cbuffer Frame : register(b0)
{
    float4x4 viewProj;
    float3 eye;
    uint lightCount;
    float4 ambient;
    float4 viewport;
    float environmentIntensity;
    int shadowLight;
    float2 lightingPadding;
    float4x4 cascadeViewProj[4];
    float4 cascadeSplits;
    float4 cascadeTexel;
    uint cascadeCount;
    float cascadeBlend;
    float cascadeNear;
    float orthoDepthScale;
};
cbuffer Object : register(b1)
{
    float4x4 world;
    float4x4 normalMatrix;
};
cbuffer Lights : register(b3)
{
    PackedLight lights[128];
};

cbuffer Clusters : register(b4)
{
    uint4 clusterMasks[512];
};

Texture2D<float4> environmentMap : register(t4);
SamplerState environmentSampler : register(s1);
Texture2DArray<float> shadowMap : register(t5);
SamplerComparisonState shadowSampler : register(s2);

float2 environmentUv(float3 direction)
{
    return float2(atan2(direction.x, direction.z) * 0.159154943 + 0.5,
        acos(clamp(direction.y, -1.0, 1.0)) * 0.318309886);
}

float sampleCascade(uint cascade, float3 position, float3 normal, float normalBias)
{
    float3 offsetPosition = position + normal * (cascadeTexel[cascade] * normalBias);
    float4 clipPosition = mul(cascadeViewProj[cascade], float4(offsetPosition, 1.0));
    float2 uv = clipPosition.xy * float2(0.5, -0.5) + 0.5;
    if (any(uv <= 0.0) || any(uv >= 1.0) || clipPosition.z <= 0.0 || clipPosition.z >= 1.0) { return 1.0; }

    float3 location = float3(uv, float(cascade));
    float depth = clipPosition.z - 0.0001;
    float visibility = 0.0;
    visibility += shadowMap.SampleCmpLevelZero(shadowSampler, location, depth, int2(-1, -1));
    visibility += shadowMap.SampleCmpLevelZero(shadowSampler, location, depth, int2(0, -1));
    visibility += shadowMap.SampleCmpLevelZero(shadowSampler, location, depth, int2(1, -1));
    visibility += shadowMap.SampleCmpLevelZero(shadowSampler, location, depth, int2(-1, 0));
    visibility += shadowMap.SampleCmpLevelZero(shadowSampler, location, depth, int2(0, 0));
    visibility += shadowMap.SampleCmpLevelZero(shadowSampler, location, depth, int2(1, 0));
    visibility += shadowMap.SampleCmpLevelZero(shadowSampler, location, depth, int2(-1, 1));
    visibility += shadowMap.SampleCmpLevelZero(shadowSampler, location, depth, int2(0, 1));
    visibility += shadowMap.SampleCmpLevelZero(shadowSampler, location, depth, int2(1, 1));
    return visibility * (1.0 / 9.0);
}

float shadowVisibility(float3 position, float3 normal, float3 l, float viewDepth)
{
    float nearDepth = cascadeNear;
    uint cascade = 0;
    for (; cascade < cascadeCount; ++cascade)
    {
        float split = cascadeSplits[cascade];
        if (viewDepth < split) { break; }
        nearDepth = split;
    }
    if (cascade == cascadeCount) { return 1.0; }

    float normalBias = 1.0 - saturate(dot(normal, l));
    float visibility = sampleCascade(cascade, position, normal, normalBias);
    float farDepth = cascadeSplits[cascade];
    float blendWidth = (farDepth - nearDepth) * cascadeBlend;
    float blend = saturate(1.0 - (farDepth - viewDepth) / blendWidth);
    if (blend > 0.0)
    {
        float next = 1.0;
        if (cascade + 1 < cascadeCount)
        {
            next = sampleCascade(cascade + 1, position, normal, normalBias);
        }
        visibility = lerp(visibility, next, blend);
    }
    return visibility;
}

struct VertexInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    float4 tangent : TANGENT;
};

[shader("vertex")]
MeshFragment vsMain(VertexInput input)
{
    MeshFragment output;
    float4 position = mul(world, float4(input.position, 1.0));
    output.position = mul(viewProj, position);
    output.worldPosition = position.xyz;
    output.clipW = output.position.w;
    output.normal = mul((float3x3)normalMatrix, input.normal);
    output.tangent = float4(mul((float3x3)world, input.tangent.xyz), input.tangent.w * normalMatrix[3][3]);
    output.uv = input.uv;
    return output;
}

[shader("fragment")]
float4 psMain(MeshFragment input) : SV_Target
{
    float4 base = sampleBaseColor(input.uv);
    float opacity = materialOpacity(base.a);
    if (isUnlit()) { return float4(ambient.w != 0 ? base.rgb : encodeSrgb(saturate(base.rgb)), opacity); }
    Surface surface = sampleMaterial(input.uv, base.rgb, normalize(input.normal), input.tangent);
    float3 toEye = eye - input.worldPosition;
    float3 v = toEye * rsqrt(max(dot(toEye, toEye), 1e-8));
    float3 color = ambient.rgb * surface.albedo * (1.0 - surface.metal) + surface.emission;
    float specularStrength = materialSpecularStrength();
    if (environmentIntensity > 0.0)
    {
        uint width, height, levels;
        environmentMap.GetDimensions(0, width, height, levels);
        float maxLod = float(levels - 1);
        float3 diffuse = environmentMap.SampleLevel(environmentSampler, environmentUv(surface.normal), maxLod).rgb;
        float3 reflection = reflect(-v, surface.normal);
        float3 specular = environmentMap.SampleLevel(environmentSampler, environmentUv(reflection), surface.rough * maxLod).rgb;
        float3 f0 = lerp(float3(0.04, 0.04, 0.04), surface.albedo, surface.metal);
        float3 fresnel = specularStrength * (f0 + (1.0 - f0) * pow(1.0 - saturate(dot(surface.normal, v)), 5.0));
        color += environmentIntensity * (diffuse * surface.albedo * (1.0 - surface.metal) * (1.0 - fresnel) +
            specular * fresnel);
    }
    PunctualLight evaluator;
    uint2 tile = uint2(clamp((input.position.xy - viewport.xy) * viewport.zw, 0.0, 7.0));
    uint slice = uint(clamp(log2(max(input.clipW, 0.125)) + 3.0, 0.0, 7.0));
    uint4 mask = clusterMasks[(slice * 8 + tile.y) * 8 + tile.x];
    for (uint word = 0; word < 4 && word * 32 < lightCount; ++word)
    {
        uint bits = mask[word];
        while (bits != 0)
        {
            uint i = word * 32 + uint(firstbitlow(bits));
            bits &= bits - 1;
            float3 l;
            float3 radiance = evaluator.evaluate(lights[i], input.worldPosition, l);
            if (int(i) == shadowLight)
            {
                float viewDepth = orthoDepthScale != 0.0 ? cascadeNear + input.position.z * orthoDepthScale : input.clipW;
                radiance *= shadowVisibility(input.worldPosition, surface.normal, l, viewDepth);
            }
            color += radiance * evaluateBrdf(surface.normal, v, l, surface.albedo, surface.metal, surface.rough, specularStrength);
        }
    }
    return float4(ambient.w != 0 ? color : displayColor(color), opacity);
}
