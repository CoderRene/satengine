import Material;

import MeshFragment;

[shader("fragment")]
float4 psMain(MeshFragment input) : SV_Target
{
    float4 base = sampleBaseColor(input.uv);
    float alpha = materialOpacity(base.a);
    float band = smoothstep(0.35, 0.65, frac(input.uv.x * 8.0));
    float3 color = lerp(float3(0.18, 0.40, 1.0), float3(1.0, 0.65, 0.15), band);
    return float4(color * base.rgb, alpha);
}
