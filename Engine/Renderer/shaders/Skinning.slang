// Match ShaderLayout.hpp. Structured buffers pack these vectors on 4-byte boundaries.
struct SkinnedVertex
{
    float3 position;
    float3 normal;
    float4 tangent;
};

#ifdef SAT_SKIN_COMPUTE
// Match the four contiguous columns of the CPU's Mat4.
struct SkinTransform
{
    float4 x;
    float4 y;
    float4 z;
    float4 translation;
};

StructuredBuffer<SkinTransform> skinPalettes : register(t2);

float4x4 skinColumns(uint4 joints, float4 weights, uint offset)
{
    SkinTransform a = skinPalettes[offset + joints.x];
    SkinTransform b = skinPalettes[offset + joints.y];
    SkinTransform c = skinPalettes[offset + joints.z];
    SkinTransform d = skinPalettes[offset + joints.w];
    return float4x4(
        a.x * weights.x + b.x * weights.y + c.x * weights.z + d.x * weights.w,
        a.y * weights.x + b.y * weights.y + c.y * weights.z + d.y * weights.w,
        a.z * weights.x + b.z * weights.y + c.z * weights.z + d.z * weights.w,
        a.translation * weights.x + b.translation * weights.y + c.translation * weights.z + d.translation * weights.w);
}
#elif defined(SAT_SKINNED)
StructuredBuffer<SkinnedVertex> skinnedVertices : register(t14);
#endif
