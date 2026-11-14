cbuffer UiDraw : register(b1)
{
    float4x4 transform;
    float2 translation;
    float2 scale;
};

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

cbuffer UiGradient : register(b0)
{
    float2 origin;
    float2 direction;
    uint gradientType;
    uint repeating;
    uint stopCount;
    uint padding;
    float4 stopPositions[16];
    float4 stopColors[64];
};

float positionAt(uint index)
{
    return stopPositions[index / 4][index % 4];
}

[shader("fragment")]
float4 psMain(VSOut input) : SV_Target
{
    float2 point = input.uv - origin;
    float t;
    if (gradientType == 0)
    {
        t = dot(point, direction) / max(dot(direction, direction), 0.000001f);
    }
    else if (gradientType == 1)
    {
        t = length(point * direction);
    }
    else
    {
        float x = dot(direction, point);
        float y = dot(float2(-direction.y, direction.x), point);
        t = 0.5f + atan2(-x, y) * 0.159154943f;
    }
    float first = positionAt(0);
    float last = positionAt(stopCount - 1);
    if (repeating != 0 && last > first)
    {
        t = first + frac((t - first) / (last - first)) * (last - first);
    }
    float4 result = stopColors[0];
    [loop]
    for (uint index = 1; index < stopCount; ++index)
    {
        float previous = positionAt(index - 1);
        float next = positionAt(index);
        if (t < next)
        {
            result = lerp(stopColors[index - 1], stopColors[index],
                saturate((t - previous) / max(next - previous, 0.000001f)));
            break;
        }
        result = stopColors[index];
    }
    return result * input.color.a;
}
