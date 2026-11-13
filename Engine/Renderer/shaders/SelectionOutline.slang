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
};

struct VSOut
{
    float4 position : SV_Position;
    float4 color : COLOR;
};

[shader("vertex")]
VSOut vsMain(VSIn input)
{
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
