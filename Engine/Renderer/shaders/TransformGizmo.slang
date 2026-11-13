cbuffer Frame : register(b0)
{
    float4x4 viewProj;
    float4 selection;
};

struct VSIn
{
    float3 position : POSITION;
    float4 color : COLOR;
    float handle : TEXCOORD0;
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
    output.color = input.handle == selection.x ? (selection.y > 0 ? float4(1, 0.65f, 0.1f, 1) : float4(1, 0.9f, 0.25f, 1)) : input.color;
    return output;
}

[shader("fragment")]
float4 psMain(VSOut input) : SV_Target
{
    return input.color;
}
