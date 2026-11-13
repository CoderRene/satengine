public struct MeshFragment
{
    public float4 position : SV_Position;
    public float3 worldPosition : TEXCOORD0;
    public float3 normal : TEXCOORD1;
    public float2 uv : TEXCOORD2;
    public float4 tangent : TEXCOORD3;
    public float clipW : TEXCOORD4;
};

