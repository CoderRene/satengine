import Material;
import MeshFragment;

// The shadow pass pairs this pixel shader with the mesh vertex shader.
[shader("vertex")]
float4 vsMain(uint index : SV_VertexID) : SV_Position
{
    return float4(float(index), 0, 0, 1);
}

[shader("fragment")]
void psMain(MeshFragment input)
{
    materialOpacity(sampleBaseColor(input.uv, input.uv1).a * input.color.a);
}
