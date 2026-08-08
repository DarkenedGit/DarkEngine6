// Basic lit textured mesh shader — row-major matrices (matches Dark::Math::Matrix4f).
// Embedded copy lives in MeshPipeline.cpp (runtime compile); keep in sync.
#pragma pack_matrix(row_major)

cbuffer FrameConstants : register(b0)
{
    float4x4 worldViewProj;
    float4x4 world;
    float4   color;
    float3   lightDirWS; // direction toward the light
    float    _pad0;
};

Texture2D    gAlbedo : register(t0);
SamplerState gSamp   : register(s0);

struct VSInput
{
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD0;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float3 normalWS : NORMAL;
    float2 uv       : TEXCOORD0;
};

PSInput VSMain(VSInput input)
{
    PSInput o;
    o.position = mul(float4(input.position, 1.0f), worldViewProj);
    o.normalWS = mul(float4(input.normal, 0.0f), world).xyz;
    o.uv       = input.uv;
    return o;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    float4 albedo = gAlbedo.Sample(gSamp, input.uv) * color;
    float3 n = normalize(input.normalWS);
    float3 l = normalize(lightDirWS);
    float  ndotl = saturate(dot(n, l));
    float3 ambient = 0.22f * albedo.rgb;
    float3 diffuse = ndotl * albedo.rgb;
    return float4(ambient + diffuse, albedo.a);
}
