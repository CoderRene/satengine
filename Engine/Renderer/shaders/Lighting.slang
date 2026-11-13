public struct PackedLight
{
    public float4 positionType;
    public float4 directionRange;
    public float4 radianceInner;
    public float4 outerPadding;
};

public interface ILight
{
    float3 evaluate(PackedLight light, float3 position, out float3 direction);
};

public struct PunctualLight : ILight
{
    public float3 evaluate(PackedLight light, float3 position, out float3 direction)
    {
        direction = -light.directionRange.xyz;
        if (light.positionType.w == 0.0) { return light.radianceInner.xyz; }
        float3 delta = light.positionType.xyz - position;
        float distanceSquared = dot(delta, delta);
        direction = delta * rsqrt(max(distanceSquared, 1e-8));
        float ratioSquared = distanceSquared / max(light.directionRange.w * light.directionRange.w, 1e-12);
        float window = saturate(1.0 - ratioSquared * ratioSquared);
        float attenuation = window * window / max(distanceSquared, 0.01);
        if (light.positionType.w == 2.0)
        {
            float cone = saturate((dot(-direction, light.directionRange.xyz) - light.outerPadding.x) /
                max(light.radianceInner.w - light.outerPadding.x, 1e-5));
            attenuation *= cone * cone;
        }
        return light.radianceInner.xyz * attenuation;
    }
};
