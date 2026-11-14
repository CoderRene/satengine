struct UiFragment
{
    float4 position : SV_Position;
    float4 color : COLOR;
    float2 uv : TEXCOORD0;
};

[shader("fragment")]
float4 psMain(UiFragment input) : SV_Target
{
    float stripes = smoothstep(0.3f, 0.7f, frac(input.uv.x * 12.0f - input.uv.y));
    float3 color = lerp(float3(0.24f, 0.50f, 0.84f), float3(0.48f, 0.89f, 0.71f), stripes);
    return float4(color * input.color.a, input.color.a);
}
