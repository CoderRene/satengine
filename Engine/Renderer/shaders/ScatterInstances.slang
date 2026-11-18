import GpuInstance;

// DirtyGpuInstance is uploaded through a typed, CPU-writable SRV.
Buffer<uint4> dirty : register(t0);
RWStructuredBuffer<GpuInstance> instances : register(u0);
cbuffer Scatter : register(b0) { uint count; uint3 padding; };

[shader("compute")]
[numthreads(64, 1, 1)]
void csMain(uint3 thread : SV_DispatchThreadID)
{
    if (thread.x >= count) { return; }
    uint offset = thread.x * 11;
    uint index = dirty[offset].x;
    GpuInstance instance;
    instance.basisX = asfloat(dirty[offset + 1]);
    instance.basisY = asfloat(dirty[offset + 2]);
    instance.basisZ = asfloat(dirty[offset + 3]);
    instance.normalX = asfloat(dirty[offset + 4]);
    instance.normalY = asfloat(dirty[offset + 5]);
    instance.normalZ = asfloat(dirty[offset + 6]);
    uint4 high = dirty[offset + 7], low = dirty[offset + 8];
    instance.translationHigh = asfloat(high.xyz);
    instance.paletteOffset = high.w;
    instance.translationLow = asfloat(low.xyz);
    instance.flags = low.w;
    instance.bounds = asfloat(dirty[offset + 9]);
    instance.thicknessScale = asfloat(dirty[offset + 10]);
    instances[index] = instance;
}
