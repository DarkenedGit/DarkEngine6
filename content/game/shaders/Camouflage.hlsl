// Octopus camouflage: silhouette filled with a warped copy of the scene, plus a fresnel outline.
#pragma pack_matrix(row_major)

cbuffer FrameConstants : register(b0)
{
    float4x4 worldViewProj;
    float4x4 world;
    float3   cameraPos;
    float    time;
    float2   invSize;
    float    distort;
    float    rimStrength;
};

cbuffer BonePalette : register(b1)
{
    float4x4 boneCurr[64];
    float4x4 bonePrev[64];
};

Texture2D    gScene : register(t0);
SamplerState gSamp  : register(s0);

struct VSInput
{
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD0;
    float4 tangent  : TANGENT;
    uint4  joints   : BLENDINDICES;
    float4 weights  : BLENDWEIGHT;
};

struct VSInputStatic
{
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD0;
    float4 tangent  : TANGENT;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float3 normalWS : NORMAL;
    float3 worldPos : TEXCOORD0;
};

float4 skinPos(float3 p, uint4 j, float4 w)
{
    float4 hp = float4(p, 1.0f);
    return w.x * mul(hp, boneCurr[j.x]) + w.y * mul(hp, boneCurr[j.y])
         + w.z * mul(hp, boneCurr[j.z]) + w.w * mul(hp, boneCurr[j.w]);
}

float3 skinNrm(float3 n, uint4 j, float4 w)
{
    return w.x * mul(n, (float3x3)boneCurr[j.x])
         + w.y * mul(n, (float3x3)boneCurr[j.y])
         + w.z * mul(n, (float3x3)boneCurr[j.z])
         + w.w * mul(n, (float3x3)boneCurr[j.w]);
}

PSInput VSMain(VSInput input)
{
    float4 posM = skinPos(input.position, input.joints, input.weights);
    float3 nM   = skinNrm(input.normal, input.joints, input.weights);
    PSInput o;
    o.worldPos  = mul(posM, world).xyz;
    o.normalWS  = mul(float4(nM, 0.0f), world).xyz;
    o.position  = mul(posM, worldViewProj);
    return o;
}

PSInput VSMainStatic(VSInputStatic input)
{
    float4 wp = float4(input.position, 1.0f);
    PSInput o;
    o.worldPos = mul(wp, world).xyz;
    o.normalWS = mul(float4(input.normal, 0.0f), world).xyz;
    o.position = mul(wp, worldViewProj);
    return o;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    float3 n = normalize(input.normalWS);
    float3 v = normalize(cameraPos - input.worldPos);
    float  ndotv = saturate(dot(n, v));
    float  rim   = pow(1.0f - ndotv, 2.2f);

    float2 uv = input.position.xy * invSize;
    float2 wobble = 0.0035f * float2(
        sin(time * 2.7f + input.worldPos.y * 4.1f + input.worldPos.x * 1.3f),
        cos(time * 2.1f + input.worldPos.x * 3.6f + input.worldPos.z * 1.7f));
    float2 offset = n.xy * (distort * (0.35f + 0.85f * (1.0f - ndotv))) + wobble;

    float2 uvR = saturate(uv + offset * 1.12f);
    float2 uvG = saturate(uv + offset);
    float2 uvB = saturate(uv + offset * 0.88f);
    float3 bg;
    bg.r = gScene.SampleLevel(gSamp, uvR, 0).r;
    bg.g = gScene.SampleLevel(gSamp, uvG, 0).g;
    bg.b = gScene.SampleLevel(gSamp, uvB, 0).b;

    float3 col = bg * (0.90f + 0.08f * ndotv);
    float3 outline = float3(0.28f, 0.48f, 0.58f) * rim * rimStrength;
    col = col * (1.0f - rim * 0.35f) + outline;
    return float4(col, 1.0f);
}
