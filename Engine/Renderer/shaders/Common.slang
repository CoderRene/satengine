public float3 encodeSrgb(float3 color)
{
    return lerp(12.92 * color, 1.055 * pow(color, 1.0 / 2.4) - 0.055, step(0.0031308, color));
}

public float3 displayColor(float3 linearColor)
{
    return encodeSrgb(saturate(1.0 - 1.0 / (1.0 + linearColor)));
}
