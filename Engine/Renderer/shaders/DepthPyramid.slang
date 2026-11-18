Texture2D<float> sourceDepth : register(t0);
RWTexture2D<float> targetDepth : register(u0);
cbuffer DepthReduction : register(b0)
{
    uint2 sourceSize, targetSize;
};

[shader("compute")]
[numthreads(8, 8, 1)]
void csMain(uint3 thread : SV_DispatchThreadID)
{
    uint2 pixel = thread.xy;
    if (any(pixel >= targetSize)) { return; }
    uint2 a = pixel * 2;
    if (any(a >= sourceSize))
    {
        targetDepth[pixel] = 1.0;
        return;
    }
    uint2 b = min(a + 1, sourceSize - 1);
    targetDepth[pixel] = max(
        max(sourceDepth.Load(int3(int2(a), 0)), sourceDepth.Load(int3(int2(b.x, a.y), 0))),
        max(sourceDepth.Load(int3(int2(a.x, b.y), 0)), sourceDepth.Load(int3(int2(b), 0))));
}
