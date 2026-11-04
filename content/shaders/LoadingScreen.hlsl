// Loading splash: fullscreen triangle (SV_VertexID). pass 0 = logo+spinner, pass 1 = font.
#pragma pack_matrix(row_major)

cbuffer LoadingScreenConstants : register(b0)
{
    float  timeSec;
    float  fade;
    float  phase;
    float  reducedMotion;
    float4 background;
    float3 spinnerColor;
    float  drawPass; // C++ LoadingScreenConstants::pass (HLSL 'pass' is reserved)
    float2 resolution;
    float  logoAspect;
    float  spinnerOpacity;
};

Texture2D    gLogo : register(t0);
Texture2D    gFont : register(t1);
SamplerState gSamp : register(s0);

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD0;
};

PSInput VSMain(uint id : SV_VertexID)
{
    PSInput o;
    float2 uv = float2((id << 1) & 2, id & 2);
    o.position = float4(uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    o.uv       = uv;
    return o;
}

float2 letterboxUv(float2 uv, float2 res, float aspect)
{
    float la = max(aspect, 1.0e-4);
    float sa = res.x / max(res.y, 1.0);
    float2 size = (sa > la) ? float2(la / sa, 1.0) : float2(1.0, sa / la);
    return (uv - 0.5) / size + 0.5;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    if (drawPass > 0.5)
    {
        float g = gFont.Sample(gSamp, input.uv).r;
        return float4(1.0, 1.0, 1.0, g * fade);
    }

    float2 logoUv = letterboxUv(input.uv, resolution, logoAspect);
    float  inLogo = (logoUv.x >= 0.0 && logoUv.x <= 1.0 && logoUv.y >= 0.0 && logoUv.y <= 1.0) ? 1.0 : 0.0;
    float4 logo   = gLogo.Sample(gSamp, saturate(logoUv));
    logo.a *= inLogo;

    float4 color = lerp(background, logo, logo.a * fade);

    color.a = 1.0;
    return color;
}
