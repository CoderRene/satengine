#define SAT_SKIN_COMPUTE 1
#include "Skinning.slang"

ByteAddressBuffer meshVertices : register(t0);
ByteAddressBuffer meshSkin : register(t1);
RWStructuredBuffer<SkinnedVertex> outputVertices : register(u0);

cbuffer Dispatch : register(b0)
{
    uint vertexStart;
    uint vertexCount;
    uint paletteOffset;
    uint outputBase;
};

uint4 unpackBytes(uint packed)
{
    return (uint4(packed) >> uint4(0, 8, 16, 24)) & 255u;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void csMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= vertexCount) { return; }
    uint vertex = vertexStart + id.x;
    uint source = vertex * 72u;
    uint2 skinData = meshSkin.Load2(vertex * 8u);
    uint4 joints = unpackBytes(skinData.x);
    float4 weights = float4(unpackBytes(skinData.y)) * (1.0f / 255.0f);
    float4x4 skin = skinColumns(joints, weights, paletteOffset);

    float3 position = asfloat(meshVertices.Load3(source));
    float3 normal = asfloat(meshVertices.Load3(source + 12u));
    float4 tangent = asfloat(meshVertices.Load4(source + 32u));
    float3 sx = skin[0].xyz;
    float3 sy = skin[1].xyz;
    float3 sz = skin[2].xyz;
    float sign = dot(sx, cross(sy, sz)) < 0.0f ? -1.0f : 1.0f;

    SkinnedVertex result;
    result.position = mul(float4(position, 1), skin).xyz;
    result.normal = (cross(sy, sz) * normal.x + cross(sz, sx) * normal.y + cross(sx, sy) * normal.z) * sign;
    result.tangent = float4(mul(float4(tangent.xyz, 0), skin).xyz, tangent.w * sign);
    outputVertices[outputBase + vertex] = result;
}
