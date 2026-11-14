cbuffer UiEffect : register(b0)
{
    float4x4 colorMatrix;
    float4 shadowColor;
    float2 offset;
    float2 sampleStep;
    float value;
    uint effect;
    float2 padding;
};

Texture2D<float4> image : register(t0);
Texture2D<float4> secondaryImage : register(t1);
SamplerState imageSampler : register(s0);

struct VSOut
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
VSOut vsMain(uint index : SV_VertexID)
{
    VSOut output;
    output.uv = float2((index << 1) & 2, index & 2);
    output.position = float4(output.uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return output;
}

float4 sampleImage(Texture2D<float4> source, float2 uv)
{
    if (any(uv < 0.0f) || any(uv > 1.0f)) { return 0; }
    return source.SampleLevel(imageSampler, uv, 0);
}

float4 gaussian(Texture2D<float4> source, float2 uv)
{
    const float spacing = max(1.0f, ceil(value * 3.0f / 16.0f));
    const int radius = min(16, int(ceil(value * 3.0f / spacing)));
    const float exponent = -0.5f * spacing * spacing / (value * value);
    float4 sum = sampleImage(source, uv);
    float weightSum = 1.0f;
    [loop]
    for (int tap = 1; tap <= radius; ++tap)
    {
        float weight = exp(tap * tap * exponent);
        float2 delta = sampleStep * (tap * spacing);
        sum += weight * (sampleImage(source, uv + delta) + sampleImage(source, uv - delta));
        weightSum += 2.0f * weight;
    }
    return sum / weightSum;
}

[shader("fragment")]
float4 psMain(VSOut input) : SV_Target
{
    float4 color = image.SampleLevel(imageSampler, input.uv, 0);
    switch (effect)
    {
    case 1:
        return float4(clamp(mul(colorMatrix, color).rgb, 0.0f, color.a), color.a);
    case 2:
        return color * value;
    case 3:
        return gaussian(image, input.uv);
    case 4:
    {
        float2 uv = input.uv - offset;
        float alpha = value >= 0.5f ? gaussian(secondaryImage, uv).a : sampleImage(secondaryImage, uv).a;
        return color + (1.0f - color.a) * alpha * shadowColor;
    }
    case 5:
        return color * secondaryImage.SampleLevel(imageSampler, input.uv, 0).a;
    default:
        return color;
    }
}
