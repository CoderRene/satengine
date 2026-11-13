cbuffer Frame : register(b0)
{
    float4x4 viewProj;
};

struct VSIn
{
    float3 position : POSITION;
    float4 color : COLOR;
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
    output.position = mul(viewProj, float4(input.position, 1.0f));
    output.color = input.color;
    return output;
}

[shader("fragment")]
float4 psMain(VSOut input) : SV_Target
{
    return input.color;
}
