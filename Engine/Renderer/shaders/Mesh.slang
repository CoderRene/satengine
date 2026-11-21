import GpuInstance;
#include "Skinning.slang"
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
    float environmentMaxLod;
    float lightingPadding;
    float4x4 cascadeViewProj[4];
    float4 cascadeSplits;
    float4 cascadeTexel;
    uint cascadeCount;
    float cascadeBlend;
    float cascadeNear;
    float orthoDepthScale;
    float3 orthoViewToEye;
    uint orthographic;
    float4 originHigh;
    float4 originLow;
    float4x4 spotViewProj;
    int spotLight;
    float3 spotPadding;
};
cbuffer Lights : register(b3)
{
    PackedLight lights[128];
};

cbuffer Clusters : register(b4)
{
    uint4 clusterMasks[512];
};

Texture2DArray<float> shadowMap : register(t5);
SamplerComparisonState shadowSampler : register(s2);
Texture2D<float4> opaqueScene : register(t12);
SamplerState linearClampSampler : register(s12);

Texture2DArray<float> spotShadowMap : register(t15);
TextureCube<float4> diffuseEnvironment : register(t16);
TextureCube<float4> specularEnvironment : register(t17);
Texture2D<float2> environmentBrdf : register(t18);

float spotVisibility(float3 position)
{
    float4 clip = mul(spotViewProj, float4(position, 1.0));
    if (clip.w <= 0.0) { return 1.0; }
    float3 projected = clip.xyz / clip.w;
    float2 uv = projected.xy * float2(0.5, -0.5) + 0.5;
    if (any(uv <= 0.0) || any(uv >= 1.0) || projected.z <= 0.0 || projected.z >= 1.0) { return 1.0; }
    return spotShadowMap.SampleCmpLevelZero(shadowSampler, float3(uv, 0.0), projected.z - 0.0001);
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
#ifndef SAT_SKINNED
    float3 position : POSITION;
    float3 normal : NORMAL;
    float4 tangent : TANGENT;
#endif
    float2 uv : TEXCOORD0;
    float2 uv1 : TEXCOORD1;
    float4 color : COLOR0;
    uint instanceIndex : INSTANCE0;
};

StructuredBuffer<GpuInstance> instances : register(t13);

[shader("vertex")]
MeshFragment vsMain(VertexInput input
#ifdef SAT_SKINNED
    , uint vertexId : SV_VertexID
#endif
)
{
    GpuInstance instance = instances[input.instanceIndex];
#ifdef SAT_SKINNED
    // Skinned meshes have dedicated buffers, with an indexed draw base vertex of zero.
    SkinnedVertex vertex = skinnedVertices[instance.paletteOffset + vertexId];
#else
    SkinnedVertex vertex;
    vertex.position = input.position;
    vertex.normal = input.normal;
    vertex.tangent = input.tangent;
#endif
    MeshFragment output;
    float3 translation = relativeTranslation(instance, originHigh.xyz, originLow.xyz);
    float3 position = instance.basisX.xyz * vertex.position.x + instance.basisY.xyz * vertex.position.y +
        instance.basisZ.xyz * vertex.position.z + translation;
    output.position = mul(viewProj, float4(position, 1));
    output.worldPosition = position;
    output.clipW = output.position.w;
    output.normal = instance.normalX.xyz * vertex.normal.x + instance.normalY.xyz * vertex.normal.y +
        instance.normalZ.xyz * vertex.normal.z;
    float3 tangent = instance.basisX.xyz * vertex.tangent.x + instance.basisY.xyz * vertex.tangent.y +
        instance.basisZ.xyz * vertex.tangent.z;
    output.tangent = float4(tangent, vertex.tangent.w * instance.normalX.w);
    output.uv = input.uv;
    output.uv1 = input.uv1;
    output.color = input.color;
    output.thicknessScale = instance.thicknessScale.xyz;
    return output;
}

float3 transmittedScene(MeshFragment input, Surface surface, float3 v, float ior, uint3 dimensions)
{
    float2 sceneUv = input.position.xy;
    float maxLod = float(dimensions.z - 1);
    // Bound the automatic blur in pixels so roughness-map detail cannot select
    // increasingly coarse scene mips as viewport resolution grows.
    const float maxRoughnessLod = 8.0;
    float roughnessLod = surface.perceptualRoughness * saturate(1.5 * (ior - 1.0)) * min(maxLod, maxRoughnessLod);
    float lod = clamp(materialTransmissionMip(roughnessLod), 0.0, maxLod);
    float travel = 0.0;
    float thickness = sampleThickness(input.uv, input.uv1);
    if (thickness > 0.0)
    {
        float3 ray = refract(-v, surface.normal, 1.0 / ior) * input.thicknessScale * thickness;
        travel = length(ray);
        float4 exitClip = mul(viewProj, float4(input.worldPosition + ray, 1.0));
        if (exitClip.w > 0.0)
        {
            float2 exitUv = (exitClip.xy / exitClip.w) * float2(0.5, -0.5) + 0.5;
            float2 viewportSize = 8.0 / viewport.zw;
            float2 exitPixel = exitUv * viewportSize;
            float2 edgeDistance = min(exitPixel, viewportSize - exitPixel);
            // Fade toward the unrefracted pixel before the blur footprint crosses the viewport edge.
            float edgeFade = smoothstep(0.0, max(2.0, exp2(lod)), min(edgeDistance.x, edgeDistance.y));
            sceneUv = lerp(sceneUv, viewport.xy + exitPixel, edgeFade);
        }
    }
    float3 background = opaqueScene.SampleLevel(linearClampSampler, sceneUv / float2(dimensions.xy), lod).rgb;
    if (ambient.w == 0.0)
    {
        background = lerp(background / 12.92, pow((background + 0.055) / 1.055, 2.4), step(0.04045, background));
    }
    return background * volumeAttenuation(travel);
}

float4 shadeMesh(MeshFragment input, bool frontFace)
{
    float4 base = sampleBaseColor(input.uv, input.uv1) * input.color;
    float opacity = materialOpacity(base.a);
    if (isUnlit()) { return float4(ambient.w != 0 ? base.rgb : encodeSrgb(saturate(base.rgb)), opacity); }
    float3 geometricNormal = normalize(input.normal);
    Surface surface = sampleMaterial(input.uv, input.uv1, base.rgb, geometricNormal, input.tangent,
        input.worldPosition, frontFace);
    Clearcoat coat = sampleClearcoat(input.uv, input.uv1, geometricNormal, input.tangent, input.worldPosition, frontFace);
    float3 toEye = eye - input.worldPosition;
    float3 v = orthographic != 0 ? orthoViewToEye : toEye * rsqrt(max(dot(toEye, toEye), 1e-8));
    float ior = materialIor();
    float reflectance = ior == 0.0 ? 1.0 : (ior - 1.0) / (ior + 1.0);
    float dielectricF0 = reflectance * reflectance;
    float3 f0 = lerp(float3(dielectricF0, dielectricF0, dielectricF0), surface.albedo, surface.metal);
    float specularStrength = materialSpecularStrength();
    float nv = saturate(dot(surface.normal, v));
    float3 viewFresnel = specularStrength * (f0 + (1.0 - f0) * pow(1.0 - nv, 5.0));
    float coatWeight = coat.factor * (0.04 + 0.96 * pow(1.0 - saturate(abs(dot(coat.normal, v))), 5.0));
    float3 coatLight = float3(0.0, 0.0, 0.0);
    float3 coatIndirect = float3(0.0, 0.0, 0.0);
    float transmission = sampleTransmission(input.uv, input.uv1);
    float3 transmitted = float3(0.0, 0.0, 0.0);
    if (transmission > 0.0 && surface.metal < 1.0)
    {
        uint3 dimensions;
        opaqueScene.GetDimensions(0, dimensions.x, dimensions.y, dimensions.z);
        if (all(dimensions > 0))
        {
            transmitted = transmittedScene(input, surface, v, ior, dimensions) *
                (1.0 - viewFresnel) * (1.0 - surface.metal) * transmission * surface.albedo;
        }
        else { transmission = 0.0; }
    }
    float3 diffuseColor = surface.albedo * (1.0 - surface.metal) * (1.0 - transmission);
    float3 color = ambient.rgb * diffuseColor * surface.occlusion + surface.emission;
    if (environmentMaxLod > 0.0)
    {
        float envScale = environmentIntensity * surface.occlusion;
        float roughness = saturate(surface.perceptualRoughness);
        float3 diffuse = diffuseEnvironment.SampleLevel(linearClampSampler, surface.normal, 0.0).rgb;
        float3 specular = specularEnvironment.SampleLevel(linearClampSampler, reflect(-v, surface.normal),
            roughness * environmentMaxLod).rgb;
        float2 brdf = environmentBrdf.SampleLevel(linearClampSampler, float2(nv, roughness), 0.0);
        color += envScale * (diffuse * diffuseColor * (1.0 - viewFresnel) +
            specular * specularStrength * (f0 * brdf.x + brdf.y));
        if (coatWeight > 0.0)
        {
            float coatNv = saturate(dot(coat.normal, v));
            float coatRoughness = saturate(coat.roughness);
            float3 coatRadiance = specularEnvironment.SampleLevel(linearClampSampler, reflect(-v, coat.normal),
                coatRoughness * environmentMaxLod).rgb;
            float2 coatBrdf = environmentBrdf.SampleLevel(linearClampSampler, float2(coatNv, coatRoughness), 0.0);
            coatIndirect = envScale * coat.factor * coatRadiance * (0.04 * coatBrdf.x + coatBrdf.y);
        }
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
            if (int(i) == spotLight) { radiance *= spotVisibility(input.worldPosition); }
            color += radiance * evaluateSurfaceBrdf(surface.normal, v, l, diffuseColor, surface.rough, specularStrength, f0);
            if (coatWeight > 0.0)
            {
                coatLight += radiance * evaluateClearcoat(coat.normal, v, l, coat.roughness);
            }
        }
    }
    color = lerp(color, coatLight, coatWeight) + coatIndirect;
    transmitted *= 1.0 - coatWeight;
    if (ambient.w != 0.0) { return float4(color + transmitted, opacity); }
    float3 displayLinear = saturate(1.0 - 1.0 / (1.0 + color));
    return float4(encodeSrgb(saturate(displayLinear + transmitted)), opacity);
}

#ifdef SAT_WEIGHTED_BLEND
struct WeightedFragment
{
    float4 accumulated : SV_Target0;
    float4 coverage : SV_Target1;
};

[shader("fragment")]
WeightedFragment psMain(MeshFragment input, bool frontFace : SV_IsFrontFace)
{
    float4 color = shadeMesh(input, frontFace);
    float alpha = saturate(color.a);
    float depthWeight = 1.0 - 0.9 * saturate(input.position.z);
    float weight = max(0.01, depthWeight * depthWeight * depthWeight);
    WeightedFragment result;
    result.accumulated = float4(clamp(color.rgb, 0.0, 1024.0) * alpha, alpha) * weight;
    result.coverage = float4(alpha, alpha, alpha, alpha);
    return result;
}
#else
[shader("fragment")]
float4 psMain(MeshFragment input, bool frontFace : SV_IsFrontFace) : SV_Target
{
    return shadeMesh(input, frontFace);
}
#endif
