// Fullscreen HDR → swap-chain copy. mode 0 = saturate; mode 1 = Narkowicz ACES (display curve).
#pragma pack_matrix(row_major)

cbuffer TonemapConstants : register(b0)
{
    float exposure;
    float mode;
    float _pad0;
    float _pad1;
};

Texture2D gHdr : register(t0);

struct PSInput
{
    float4 position : SV_POSITION;
};

PSInput VSMain(uint id : SV_VertexID)
{
    float2 pos = float2((id << 1) & 2, id & 2) * 2.0f - 1.0f;
    PSInput o;
    o.position = float4(pos, 0.0f, 1.0f);
    return o;
}

float3 aces(float3 x)
{
    const float a = 2.51f, b = 0.03f, c = 2.43f, d = 0.59f, e = 0.14f;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

float4 PSMain(PSInput input) : SV_TARGET
{
    float3 hdr = gHdr.Load(int3(int2(input.position.xy), 0)).rgb;
    if (mode < 0.5f)
        return float4(saturate(hdr), 1);
    return float4(aces(hdr * exposure), 1);
}
