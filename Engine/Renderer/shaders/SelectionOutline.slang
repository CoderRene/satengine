#ifdef SAT_SKINNED
#include "Skinning.slang"
cbuffer Skin : register(b5) { uint vertexBase; uint3 skinPadding; };
#endif
cbuffer Frame : register(b0)
{
    float4x4 viewProj;
};

cbuffer Object : register(b1)
{
    float4x4 world;
    float4 color;
};

struct VSOut
{
    float4 position : SV_Position;
    float4 color : COLOR;
};

[shader("vertex")]
VSOut vsMain(
#ifdef SAT_SKINNED
    uint vertexId : SV_VertexID
#else
    float3 position : POSITION
#endif
)
{
#ifdef SAT_SKINNED
    float3 position = skinnedVertices[vertexBase + vertexId].position;
#endif
    VSOut output;
    float4 worldPos = mul(world, float4(position, 1.0f));
    output.position = mul(viewProj, worldPos);
    output.color = color;
    return output;
}

[shader("fragment")]
float4 psMain(VSOut input) : SV_Target
{
    return input.color;
}
