public struct GpuInstance
{
    public float4 basisX, basisY, basisZ;
    public float4 normalX, normalY, normalZ;
    public float3 translationHigh;
    public uint paletteOffset; // Base of the computed skinned vertex range.
    public float3 translationLow;
    public uint flags;
    public float4 bounds;
    public float4 thicknessScale;
};

public float3 relativeTranslation(GpuInstance instance, float3 originHigh, float3 originLow)
{
    // Keep the high-part cancellation ahead of the low-part addition.
    precise float3 position = (instance.translationHigh - originHigh) + (instance.translationLow - originLow);
    return position;
}
