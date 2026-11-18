#ifdef SAT_SKINNED
#include "Skinning.slang"
#endif
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
    float3 orthoViewToEye;
    uint orthographic;
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
Texture2D<float4> opaqueScene : register(t12);
SamplerState opaqueSceneSampler : register(s12);

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
#ifdef SAT_SKINNED
    uint4 joints : BLENDINDICES;
    float4 weights : BLENDWEIGHT;
#endif
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    float2 uv1 : TEXCOORD1;
    float4 color : COLOR0;
    float4 tangent : TANGENT;
    float4 worldX : INSTANCE0;
    float4 worldY : INSTANCE1;
    float4 worldZ : INSTANCE2;
    float4 worldTranslation : INSTANCE3;
    float4 normalX : INSTANCE4;
    float3 normalY : INSTANCE5;
    float3 normalZ : INSTANCE6;
    float3 thicknessScale : INSTANCE7;
};

[shader("vertex")]
MeshFragment vsMain(VertexInput input)
{
#ifdef SAT_SKINNED
    float4x4 skin = skinColumns(input.joints, input.weights);
    input.position = mul(float4(input.position, 1), skin).xyz;
    float3 sx = skin[0].xyz;
    float3 sy = skin[1].xyz;
    float3 sz = skin[2].xyz;
    float determinant = dot(sx, cross(sy, sz));
    input.normal = (cross(sy, sz) * input.normal.x + cross(sz, sx) * input.normal.y + cross(sx, sy) * input.normal.z) * (determinant < 0 ? -1.0 : 1.0);
    input.tangent.xyz = mul(float4(input.tangent.xyz, 0), skin).xyz;
    input.tangent.w *= determinant < 0 ? -1.0 : 1.0;
#endif
    MeshFragment output;
    float4 position = input.worldX * input.position.x + input.worldY * input.position.y +
        input.worldZ * input.position.z + input.worldTranslation;
    output.position = mul(viewProj, position);
    output.worldPosition = position.xyz;
    output.clipW = output.position.w;
    output.normal = input.normalX.xyz * input.normal.x + input.normalY * input.normal.y +
        input.normalZ * input.normal.z;
    float3 tangent = input.worldX.xyz * input.tangent.x + input.worldY.xyz * input.tangent.y +
        input.worldZ.xyz * input.tangent.z;
    output.tangent = float4(tangent, input.tangent.w * input.normalX.w);
    output.uv = input.uv;
    output.uv1 = input.uv1;
    output.color = input.color;
    output.thicknessScale = input.thicknessScale;
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
    float3 background = opaqueScene.SampleLevel(opaqueSceneSampler, sceneUv / float2(dimensions.xy), lod).rgb;
    if (ambient.w == 0.0)
    {
        background = lerp(background / 12.92, pow((background + 0.055) / 1.055, 2.4), step(0.04045, background));
    }
    return background * volumeAttenuation(travel);
}

[shader("fragment")]
float4 psMain(MeshFragment input, bool frontFace : SV_IsFrontFace) : SV_Target
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
    if (environmentIntensity > 0.0)
    {
        uint width, height, levels;
        environmentMap.GetDimensions(0, width, height, levels);
        float maxLod = float(levels - 1);
        float3 diffuse = environmentMap.SampleLevel(environmentSampler, environmentUv(surface.normal), maxLod).rgb;
        float3 reflection = reflect(-v, surface.normal);
        float3 specular = environmentMap.SampleLevel(environmentSampler, environmentUv(reflection), surface.rough * maxLod).rgb;
        color += environmentIntensity * surface.occlusion *
            (diffuse * diffuseColor * (1.0 - viewFresnel) + specular * viewFresnel);
        if (coatWeight > 0.0)
        {
            float3 coatReflection = reflect(-v, coat.normal);
            coatLight = environmentIntensity * surface.occlusion * environmentMap.SampleLevel(
                environmentSampler, environmentUv(coatReflection), coat.roughness * maxLod).rgb;
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
            color += radiance * evaluateSurfaceBrdf(surface.normal, v, l, diffuseColor, surface.rough, specularStrength, f0);
            if (coatWeight > 0.0)
            {
                coatLight += radiance * evaluateClearcoat(coat.normal, v, l, coat.roughness);
            }
        }
    }
    color = lerp(color, coatLight, coatWeight);
    transmitted *= 1.0 - coatWeight;
    if (ambient.w != 0.0) { return float4(color + transmitted, opacity); }
    float3 displayLinear = saturate(1.0 - 1.0 / (1.0 + color));
    return float4(encodeSrgb(saturate(displayLinear + transmitted)), opacity);
}
