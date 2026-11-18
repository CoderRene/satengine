#ifdef SAT_SKINNED
#include "Skinning.slang"
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

struct VSIn
{
    float3 position : POSITION;
#ifdef SAT_SKINNED
    uint4 joints : BLENDINDICES;
    float4 weights : BLENDWEIGHT;
#endif
};

struct VSOut
{
    float4 position : SV_Position;
    float4 color : COLOR;
};

[shader("vertex")]
VSOut vsMain(VSIn input)
{
#ifdef SAT_SKINNED
    input.position = mul(float4(input.position, 1), skinColumns(input.joints, input.weights)).xyz;
#endif
    VSOut output;
    float4 worldPos = mul(world, float4(input.position, 1.0f));
    output.position = mul(viewProj, worldPos);
    output.color = color;
    return output;
}

[shader("fragment")]
float4 psMain(VSOut input) : SV_Target
{
    return input.color;
}
