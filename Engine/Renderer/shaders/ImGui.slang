cbuffer Frame : register(b0)
{
    float4x4 projection;
};

struct VertexInput
{
    float2 position : POSITION;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
};

struct VertexOutput
{
    float4 position : SV_Position;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
};

Texture2D<float4> image : register(t0);
SamplerState imageSampler : register(s0);

[shader("vertex")]
VertexOutput vsMain(VertexInput input)
{
    VertexOutput output;
    output.position = mul(projection, float4(input.position, 0, 1));
    output.color = input.color;
    output.uv = input.uv;
    return output;
}

[shader("fragment")]
float4 psMain(VertexOutput input) : SV_Target
{
    return input.color * image.Sample(imageSampler, input.uv);
}
