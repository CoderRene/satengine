import GpuInstance;

struct DrawItem { uint instance, batch, visibleStart, passes; };
StructuredBuffer<GpuInstance> instances : register(t0);
StructuredBuffer<DrawItem> items : register(t1);
Texture2D<float> hiZ : register(t2);
ByteAddressBuffer retestCount : register(t3);
Buffer<float4> animationProbes : register(t4);
RWByteAddressBuffer args : register(u0);
RWByteAddressBuffer visible : register(u1);
RWByteAddressBuffer rejected : register(u2);
RWByteAddressBuffer retestDispatch : register(u3);
RWByteAddressBuffer animationOcclusion : register(u4);

cbuffer Cull : register(b0)
{
    float4 planes[6];
    float4x4 hiZViewProj;
    float4 originHigh, originLow;
    float4 hiZOriginHigh, hiZOriginLow;
    uint itemCount, firstItem, phase, hiZLevels;
    uint2 viewSize, pyramidSize;
};

bool occluded(float3 center, float radius)
{
    // Bound the sphere's clip-space box before the perspective divide.
    // Independent W extrema make the screen bounds conservative.
    float4 clip = mul(hiZViewProj, float4(center, 1));
    float4 dx = radius * mul(hiZViewProj, float4(1, 0, 0, 0));
    float4 dy = radius * mul(hiZViewProj, float4(0, 1, 0, 0));
    float4 dz = radius * mul(hiZViewProj, float4(0, 0, 1, 0));
    float4 extent = abs(dx) + abs(dy) + abs(dz);
    float4 lowClip = clip - extent, highClip = clip + extent;
    if (lowClip.w <= 0 || lowClip.z <= 0) { return false; }
    float2 lowNdc = min(lowClip.xy / lowClip.w, lowClip.xy / highClip.w);
    float2 highNdc = max(highClip.xy / lowClip.w, highClip.xy / highClip.w);
    float2 low = float2(lowNdc.x, -highNdc.y) * 0.5 + 0.5;
    float2 high = float2(highNdc.x, -lowNdc.y) * 0.5 + 0.5;
    // Linear-fractional depth reaches its minimum at a box corner
    // while all W values are strictly positive.
    float nearest = 1;
    [unroll]
    for (uint corner = 0; corner < 8; ++corner)
    {
        float2 zw = clip.zw + ((corner & 1) != 0 ? dx.zw : -dx.zw) +
            ((corner & 2) != 0 ? dy.zw : -dy.zw) + ((corner & 4) != 0 ? dz.zw : -dz.zw);
        nearest = min(nearest, zw.x / zw.y);
    }
    // History may cover a different region after a camera move.
    if (any(low < 0) || any(high > 1) || nearest >= 1) { return false; }
    float2 scale = float2(viewSize) * 0.5;
    float2 lo = low * scale, hi = high * scale;
    float size = max(hi.x - lo.x, hi.y - lo.y);
    uint level = min(uint(ceil(log2(max(size, 1.0)))), hiZLevels - 1);
    uint2 mipSize = max(pyramidSize >> level, uint2(1, 1));
    uint2 a = min(uint2(lo) >> level, mipSize - 1);
    uint2 b = min(uint2(hi) >> level, mipSize - 1);
    // The selected mip spans the rectangle in at most two texels per axis.
    float depth = max(
        max(hiZ.Load(int3(int2(a), int(level))), hiZ.Load(int3(int2(b.x, a.y), int(level)))),
        max(hiZ.Load(int3(int2(a.x, b.y), int(level))), hiZ.Load(int3(int2(b), int(level)))));
    return nearest > depth + 0.00001;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void csMain(uint3 thread : SV_DispatchThreadID)
{
    if (phase == 3)
    {
        if (thread.x < itemCount)
        {
            float4 sphere = animationProbes[thread.x];
            bool hidden = all(isfinite(sphere)) && occluded(sphere.xyz, sphere.w);
            animationOcclusion.Store(thread.x * 4, hidden ? 1u : 0u);
        }
        return;
    }
    uint count = phase == 2 ? retestCount.Load(12) : itemCount;
    if (thread.x >= count) { return; }
    uint index = phase == 2 ? rejected.Load(thread.x * 4) : firstItem + thread.x;
    DrawItem item = items[index];
    GpuInstance instance = instances[item.instance];
    float radius = instance.bounds.w;
    if (radius >= 0)
    {
        float3 center = relativeTranslation(instance, originHigh.xyz, originLow.xyz) + instance.bounds.xyz;
        for (uint i = 0; i < 6; ++i)
        {
            if (dot(planes[i], float4(center, 1)) < -radius) { return; }
        }
        if (hiZLevels != 0 && occluded(relativeTranslation(instance, hiZOriginHigh.xyz, hiZOriginLow.xyz) +
                instance.bounds.xyz, radius))
        {
            if (phase == 1)
            {
                uint slot, unused;
                retestDispatch.InterlockedAdd(12, 1, slot);
                rejected.Store(slot * 4, index);
                retestDispatch.InterlockedMax(0, slot / 64 + 1, unused);
            }
            return;
        }
    }
    uint slot;
    args.InterlockedAdd(item.batch * 20 + 4, 1, slot);
    visible.Store((item.visibleStart + slot) * 4, item.instance);
}
