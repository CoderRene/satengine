import Common;

Texture2D<float4> hdrImage : register(t0);
Texture2D<float4> secondaryImage : register(t1);
SamplerState linearClamp : register(s0);

cbuffer Bloom : register(b0)
{
    float threshold, knee, intensity, radius;
    float3 tint;
    float highlightClamp;
    uint kernelRadius;
    uint filterStage;
    float coarseWeight;
    uint padding;
};

struct FullscreenVertex
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
FullscreenVertex vsMain(uint index : SV_VertexID)
{
    FullscreenVertex output;
    output.uv = float2((index << 1) & 2, index & 2);
    output.position = float4(output.uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return output;
}

float3 extractHighlight(float4 sampleColor)
{
    float peak = max(sampleColor.r, max(sampleColor.g, sampleColor.b));
    float bright = min(peak, highlightClamp) - threshold;
    float soft = clamp(bright + knee, 0.0, 2.0 * knee);
    float contribution = max(bright, soft * soft / max(4.0 * knee, 1e-5));
    return sampleColor.rgb * (contribution / max(peak, 0.001)) * sampleColor.a;
}

[shader("fragment")]
float4 psMain(FullscreenVertex input) : SV_Target
{
    if (filterStage == 0)
    {
        float3 scene = hdrImage.SampleLevel(linearClamp, input.uv, 0).rgb;
        if (intensity > 0)
        {
            scene += secondaryImage.SampleLevel(linearClamp, input.uv, 0).rgb * tint * intensity;
        }
        return float4(displayColor(max(scene, 0.0)), 1.0);
    }

    uint width, height;
    if (filterStage == 3) { secondaryImage.GetDimensions(width, height); }
    else { hdrImage.GetDimensions(width, height); }
    float2 texel = 1.0 / float2(width, height);
    int extent = int(kernelRadius);
    float3 filtered = float3(0, 0, 0);
    for (int y = -extent; y <= extent; ++y)
    {
        for (int x = -extent; x <= extent; ++x)
        {
            float2 uv = input.uv + float2(x, y) * texel;
            float weight = float((extent + 1 - abs(x)) * (extent + 1 - abs(y)));
            if (filterStage == 3)
            {
                filtered += secondaryImage.SampleLevel(linearClamp, uv, 0).rgb * weight;
            }
            else
            {
                float4 sampleColor = hdrImage.SampleLevel(linearClamp, uv, 0);
                filtered += (filterStage == 1 ? extractHighlight(sampleColor) : sampleColor.rgb) * weight;
            }
        }
    }
    float weightSum = float((extent + 1) * (extent + 1));
    filtered /= weightSum * weightSum;
    if (filterStage == 3)
    {
        filtered = lerp(hdrImage.SampleLevel(linearClamp, input.uv, 0).rgb, filtered, coarseWeight);
    }
    return float4(filtered, 1.0);
}
