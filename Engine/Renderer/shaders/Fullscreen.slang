struct FullscreenVertex
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
FullscreenVertex vsMain(uint index : SV_VertexID)
{
    FullscreenVertex result;
    result.uv = float2((index << 1) & 2, index & 2);
    result.position = float4(result.uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return result;
}
