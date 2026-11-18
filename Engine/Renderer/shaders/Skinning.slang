// Match the four contiguous columns of the CPU's Mat4.
struct SkinTransform
{
    float4 x;
    float4 y;
    float4 z;
    float4 translation;
};

StructuredBuffer<SkinTransform> skinPalettes : register(t8);
cbuffer Skin : register(b5) { uint paletteOffset; uint3 skinPadding; };

float4x4 skinColumns(uint4 joints, float4 weights)
{
    SkinTransform a = skinPalettes[paletteOffset + joints.x];
    SkinTransform b = skinPalettes[paletteOffset + joints.y];
    SkinTransform c = skinPalettes[paletteOffset + joints.z];
    SkinTransform d = skinPalettes[paletteOffset + joints.w];
    return float4x4(
        a.x * weights.x + b.x * weights.y + c.x * weights.z + d.x * weights.w,
        a.y * weights.x + b.y * weights.y + c.y * weights.z + d.y * weights.w,
        a.z * weights.x + b.z * weights.y + c.z * weights.z + d.z * weights.w,
        a.translation * weights.x + b.translation * weights.y + c.translation * weights.z + d.translation * weights.w);
}
