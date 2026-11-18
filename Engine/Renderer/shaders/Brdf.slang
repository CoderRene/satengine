float specularLobe(float nv, float nl, float nh, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float denominator = (1.0 - nh * nh) + nh * nh * a2;
    float distribution = a2 / (3.14159265 * denominator * denominator);
    float k = (roughness + 1.0) * (roughness + 1.0) * 0.125;
    float geometry = (nv / lerp(nv, 1.0, k)) * (nl / lerp(nl, 1.0, k));
    return distribution * geometry / max(4.0 * nv * nl, 0.001);
}

public float evaluateClearcoat(float3 n, float3 v, float3 l, float roughness)
{
    float3 halfVector = v + l;
    float3 h = halfVector * rsqrt(max(dot(halfVector, halfVector), 1e-8));
    float nl = saturate(dot(n, l));
    float nv = max(saturate(dot(n, v)), 0.001);
    return specularLobe(nv, nl, saturate(dot(n, h)), max(0.045, roughness)) * nl;
}

public float3 evaluateSurfaceBrdf(float3 n, float3 v, float3 l, float3 diffuseColor, float roughness,
    float specularStrength, float3 f0)
{
    float nl = saturate(dot(n, l));
    float nv = max(saturate(dot(n, v)), 0.001);
    float3 halfVector = v + l;
    float3 h = halfVector * rsqrt(max(dot(halfVector, halfVector), 1e-8));
    float nh = saturate(dot(n, h));
    float vh = saturate(dot(v, h));
    float3 fresnel = specularStrength * (f0 + (1.0 - f0) * pow(1.0 - vh, 5.0));
    float3 diffuse = (1.0 - fresnel) * diffuseColor / 3.14159265;
    return (diffuse + specularLobe(nv, nl, nh, roughness) * fresnel) * nl;
}

public float3 evaluateBrdf(float3 n, float3 v, float3 l, float3 albedo, float metallic, float roughness, float specularStrength)
{
    float3 f0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);
    return evaluateSurfaceBrdf(n, v, l, (1.0 - metallic) * albedo, roughness, specularStrength, f0);
}
