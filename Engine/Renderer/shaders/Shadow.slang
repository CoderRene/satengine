import Material;

// The shadow pass pairs this pixel shader with the mesh vertex shader.
[shader("vertex")]
float4 vsMain(uint index : SV_VertexID) : SV_Position
{
    return float4(float(index), 0, 0, 1);
}

[shader("fragment")]
void psMain(float2 uv : TEXCOORD2)
{
    float opacity = materialOpacity(sampleBaseColor(uv).a);
    clip(opacity - 0.001);
}
