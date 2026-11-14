cbuffer UiDraw : register(b0)
{
    float4x4 transform;
    float2 translation;
    float2 scale;
};

Texture2D<float4> image : register(t0);
SamplerState imageSampler : register(s0);

struct VSIn
{
    float2 position : POSITION;
    float4 color : COLOR;
    float2 uv : TEXCOORD;
};

struct VSOut
{
    float4 position : SV_Position;
    float4 color : COLOR;
    float2 uv : TEXCOORD;
};

[shader("vertex")]
VSOut vsMain(VSIn input)
{
    VSOut output;
    float4 pixel = mul(transform, float4(input.position + translation, 0.0f, 1.0f));
    output.position = float4(pixel.xy * scale + float2(-pixel.w, pixel.w), 0.0f, pixel.w);
    output.color = input.color;
    output.uv = input.uv;
    return output;
}

[shader("fragment")]
float4 psMain(VSOut input) : SV_Target
{
    return input.color * image.Sample(imageSampler, input.uv);
}
