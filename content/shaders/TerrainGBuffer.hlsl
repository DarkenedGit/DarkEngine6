// Opaque terrain G-buffer: height-blend metal-rough splat. No lighting/fog/shadow.
#pragma pack_matrix(row_major)

#include "GBuffer.hlsli"

cbuffer FrameConstants : register(b0)
{
    float4x4 worldViewProj;
    float4x4 world;
    float4   color;
    float4   layerTiling;
    float4x4 prevWorldViewProj;
    float    heightBlendK;
    float    heightBlendT;
    float    triplanarSlope;
    uint     layerTint0;
    uint     layerTint1;
    uint     layerTint2;
    uint     layerTint3;
};

Texture2D gAlbedo0 : register(t0);
Texture2D gNormal0 : register(t1);
Texture2D gOrm0    : register(t2);
Texture2D gAlbedo1 : register(t3);
Texture2D gNormal1 : register(t4);
Texture2D gOrm1    : register(t5);
Texture2D gAlbedo2 : register(t6);
Texture2D gNormal2 : register(t7);
Texture2D gOrm2    : register(t8);
Texture2D gAlbedo3 : register(t9);
Texture2D gNormal3 : register(t10);
Texture2D gOrm3    : register(t11);
Texture2D gSplat   : register(t12);
SamplerState gSamp      : register(s0);
SamplerState gSplatSamp : register(s2);

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
    float4 currClip : TEXCOORD1;
    float4 prevClip : TEXCOORD2;
    float3 worldPos : TEXCOORD3;
};

PSInput VSMain(VSInput input)
{
    float4 wp = float4(input.position, 1.0f);
    PSInput o;
    o.currClip = mul(wp, worldViewProj);
    o.prevClip = mul(wp, prevWorldViewProj);
    o.position = o.currClip;
    o.normalWS = mul(float4(input.normal, 0.0f), world).xyz;
    o.uv       = input.uv;
    o.worldPos = mul(wp, world).xyz;
    return o;
}

float3 DecodeTint(uint p)
{
    return float3(p & 255u, (p >> 8) & 255u, (p >> 16) & 255u) * (1.0f / 255.0f);
}

float3 WhiteoutAccum(float3 acc, float3 nLayer, float w)
{
    acc.xy += nLayer.xy * w;
    acc.z  *= lerp(1.0f, nLayer.z, w);
    return acc;
}

float3 SampleAlbedo(Texture2D tex, float2 uvXZ, float2 uvYZ, float2 uvXY, float3 tw, bool triplanar)
{
    if (!triplanar)
        return tex.Sample(gSamp, uvXZ).rgb;
    return tex.Sample(gSamp, uvYZ).rgb * tw.x
         + tex.Sample(gSamp, uvXZ).rgb * tw.y
         + tex.Sample(gSamp, uvXY).rgb * tw.z;
}

float4 SampleOrm(Texture2D tex, float2 uvXZ, float2 uvYZ, float2 uvXY, float3 tw, bool triplanar)
{
    if (!triplanar)
        return tex.Sample(gSamp, uvXZ);
    return tex.Sample(gSamp, uvYZ) * tw.x
         + tex.Sample(gSamp, uvXZ) * tw.y
         + tex.Sample(gSamp, uvXY) * tw.z;
}

GBufferOut PSMain(PSInput input)
{
    float4 splat = gSplat.Sample(gSplatSamp, input.uv);
    float  wsum  = splat.r + splat.g + splat.b + splat.a + 1e-5f;
    splat /= wsum;

    float3 nG = normalize(input.normalWS);
    float  slope = 1.0f - saturate(nG.y);
    bool   triplanar = slope > triplanarSlope;
    float3 tw = 0.0.xxx;
    if (triplanar)
    {
        tw = pow(abs(nG), 4.0f);
        float tws = tw.x + tw.y + tw.z + 1e-5f;
        tw /= tws;
    }

    float  s0 = layerTiling.x;
    float  s1 = layerTiling.y;
    float  s2 = layerTiling.z;
    float  s3 = layerTiling.w;
    float2 uvXZ0 = input.worldPos.xz * s0;
    float2 uvYZ0 = input.worldPos.yz * s0;
    float2 uvXY0 = input.worldPos.xy * s0;
    float2 uvXZ1 = input.worldPos.xz * s1;
    float2 uvYZ1 = input.worldPos.yz * s1;
    float2 uvXY1 = input.worldPos.xy * s1;
    float2 uvXZ2 = input.worldPos.xz * s2;
    float2 uvYZ2 = input.worldPos.yz * s2;
    float2 uvXY2 = input.worldPos.xy * s2;
    float2 uvXZ3 = input.worldPos.xz * s3;
    float2 uvYZ3 = input.worldPos.yz * s3;
    float2 uvXY3 = input.worldPos.xy * s3;

    float4 orm0 = SampleOrm(gOrm0, uvXZ0, uvYZ0, uvXY0, tw, triplanar);
    float4 orm1 = SampleOrm(gOrm1, uvXZ1, uvYZ1, uvXY1, tw, triplanar);
    float4 orm2 = SampleOrm(gOrm2, uvXZ2, uvYZ2, uvXY2, tw, triplanar);
    float4 orm3 = SampleOrm(gOrm3, uvXZ3, uvYZ3, uvXY3, tw, triplanar);

    float w0 = splat.r;
    float w1 = splat.g;
    float w2 = splat.b;
    float w3 = splat.a;
    if (heightBlendK > 0.0f)
    {
        float t = max(heightBlendT, 1.0e-3f);
        float h0 = orm0.a + heightBlendK * w0;
        float h1 = orm1.a + heightBlendK * w1;
        float h2 = orm2.a + heightBlendK * w2;
        float h3 = orm3.a + heightBlendK * w3;
        float hMax = max(max(h0, h1), max(h2, h3));
        float hat0 = saturate((h0 - (hMax - t)) / t);
        float hat1 = saturate((h1 - (hMax - t)) / t);
        float hat2 = saturate((h2 - (hMax - t)) / t);
        float hat3 = saturate((h3 - (hMax - t)) / t);
        float hatSum = hat0 + hat1 + hat2 + hat3;
        if (hatSum > 1.0e-5f)
        {
            float inv = 1.0f / hatSum;
            w0 = hat0 * inv;
            w1 = hat1 * inv;
            w2 = hat2 * inv;
            w3 = hat3 * inv;
        }
    }

    float3 alb = 0.0.xxx;
    float  ao = 0.0f;
    float  rough = 0.0f;
    float  metal = 0.0f;
    float3 nT = float3(0.0f, 0.0f, 1.0f);

    float3 n0 = normalize(gNormal0.Sample(gSamp, uvXZ0).xyz * 2.0f - 1.0f);
    float3 n1 = normalize(gNormal1.Sample(gSamp, uvXZ1).xyz * 2.0f - 1.0f);
    float3 n2 = normalize(gNormal2.Sample(gSamp, uvXZ2).xyz * 2.0f - 1.0f);
    float3 n3 = normalize(gNormal3.Sample(gSamp, uvXZ3).xyz * 2.0f - 1.0f);
    nT = WhiteoutAccum(nT, n0, w0);
    nT = WhiteoutAccum(nT, n1, w1);
    nT = WhiteoutAccum(nT, n2, w2);
    nT = WhiteoutAccum(nT, n3, w3);
    nT = normalize(nT);

    alb += SampleAlbedo(gAlbedo0, uvXZ0, uvYZ0, uvXY0, tw, triplanar) * DecodeTint(layerTint0) * w0;
    alb += SampleAlbedo(gAlbedo1, uvXZ1, uvYZ1, uvXY1, tw, triplanar) * DecodeTint(layerTint1) * w1;
    alb += SampleAlbedo(gAlbedo2, uvXZ2, uvYZ2, uvXY2, tw, triplanar) * DecodeTint(layerTint2) * w2;
    alb += SampleAlbedo(gAlbedo3, uvXZ3, uvYZ3, uvXY3, tw, triplanar) * DecodeTint(layerTint3) * w3;
    ao    += orm0.r * w0 + orm1.r * w1 + orm2.r * w2 + orm3.r * w3;
    rough += orm0.g * w0 + orm1.g * w1 + orm2.g * w2 + orm3.g * w3;
    metal += orm0.b * w0 + orm1.b * w1 + orm2.b * w2 + orm3.b * w3;

    float3 tW = cross(nG, float3(0.0f, 0.0f, 1.0f));
    if (dot(tW, tW) < 1e-6f)
        tW = float3(1.0f, 0.0f, 0.0f);
    tW = normalize(tW);
    float3 bW = cross(tW, nG);
    float3 nWorld = normalize(nT.x * tW + nT.y * bW + nT.z * nG);

    GBufferOut o;
    o.albedo   = float4(alb, 0);
    o.attrib   = float4(EncodeOct(nWorld), rough, metal);
    o.velocity = VelocityUv(input.currClip, input.prevClip);
    o.ao       = ao;
    return o;
}
