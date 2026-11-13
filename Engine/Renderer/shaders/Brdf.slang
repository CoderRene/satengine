public float3 evaluateBrdf(float3 n, float3 v, float3 l, float3 albedo, float metallic, float roughness, float specularStrength)
{
    float nl = saturate(dot(n, l));
    float nv = max(saturate(dot(n, v)), 0.001);
    float3 halfVector = v + l;
    float3 h = halfVector * rsqrt(max(dot(halfVector, halfVector), 1e-8));
    float nh = saturate(dot(n, h));
    float vh = saturate(dot(v, h));
    float a = roughness * roughness;
    float a2 = a * a;
    float denominator = (1.0 - nh * nh) + nh * nh * a2;
    float distribution = a2 / (3.14159265 * denominator * denominator);
    float k = (roughness + 1.0) * (roughness + 1.0) * 0.125;
    float geometry = (nv / lerp(nv, 1.0, k)) * (nl / lerp(nl, 1.0, k));
    float3 f0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);
    float3 fresnel = specularStrength * (f0 + (1.0 - f0) * pow(1.0 - vh, 5.0));
    float3 diffuse = (1.0 - fresnel) * (1.0 - metallic) * albedo / 3.14159265;
    return (diffuse + distribution * geometry * fresnel / max(4.0 * nv * nl, 0.001)) * nl;
}
