#pragma pack_matrix(row_major)

#include "GBuffer.hlsli"
#include "GrassBend.hlsli"

#ifndef GRASS_LOD
#define GRASS_LOD 0
#endif

cbuffer GrassFrameConstants : register(b0)
{
    float4x4 viewProj : packoffset(c0);
    float4x4 prevViewProj : packoffset(c4);
    float heightMetres : packoffset(c8.x);
    float flexibility : packoffset(c8.y);
    float pushMetres : packoffset(c8.z);
    float pad0 : packoffset(c8.w);
    float4 interactor[4] : packoffset(c9);
    float4 prevInteractor[4] : packoffset(c13);
    float4 footprint[8] : packoffset(c17);
    float4 prevFootprint[8] : packoffset(c25);
};

struct GrassBladeGpu
{
    float x;
    float y;
    float z;
    float yaw;
    float heightMul;
    float flexMul;
    float phase;
    uint tileSlot;
};

struct GrassTileWindGpu
{
    float windX;
    float windZ;
    float fade;
    float pad;
};

StructuredBuffer<GrassBladeGpu> gBlades : register(t0);
StructuredBuffer<GrassTileWindGpu> gTiles : register(t1);

struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    float4 tangent : TANGENT;
    uint iid : SV_InstanceID;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float3 normalWS : NORMAL;
    float t : TEXCOORD0;
    float4 currClip : TEXCOORD1;
    float4 prevClip : TEXCOORD2;
};

float3 grassYaw(float3 v, float s, float c)
{
    return float3(v.x * c + v.z * s, v.y, -v.x * s + v.z * c);
}

float3 grassSafeNormalize(float3 v, float3 fallback)
{
    float len2 = dot(v, v);
    if (len2 > 1e-8f)
        return v * rsqrt(len2);
    return fallback;
}

PSInput VSMain(VSInput input)
{
    GrassBladeGpu blade = gBlades[input.iid];
    GrassTileWindGpu tile = gTiles[blade.tileSlot];

    float height = heightMetres * blade.heightMul * tile.fade;
    float windX = tile.windX;
    float windZ = tile.windZ;
    float flex;
#if GRASS_LOD >= 3
    flex = saturate(flexibility * 0.65f);
    windX *= tile.fade;
    windZ *= tile.fade;
#else
    flex = saturate(flexibility * blade.flexMul);
    float windScale = lerp(0.85f, 1.15f, blade.phase * 0.5f + 0.5f);
    windX *= windScale;
    windZ *= windScale;
#endif

    GrassTipIn tipIn;
    tipIn.height = height;
    tipIn.flex = flex;
    tipIn.windX = windX;
    tipIn.windZ = windZ;
    // LOD 2 and 3 have no player shove. LOD 0 and 1 stay at 0 until that loop exists.
    tipIn.shoveX = 0.0f;
    tipIn.shoveZ = 0.0f;
    GrassTip tip = grassTipOffset(tipIn);

    float t = input.position.y;
    GrassLocal bent = grassBladeLocal(GRASS_LOD, t, tip, height);

    float s;
    float c;
    sincos(blade.yaw, s, c);
    float3 faceDir = grassYaw(input.normal, s, c);
    float3 widthAxis = grassYaw(input.tangent.xyz, s, c);
    float3 world = float3(blade.x, blade.y, blade.z) + widthAxis * input.position.x + float3(bent.x, bent.y, bent.z);
    world.z += input.position.z;

    float3 n = faceDir;
#if GRASS_LOD == 0
    n = grassSafeNormalize(cross(widthAxis, grassBezierDeriv(t, tip, height)), faceDir);
#elif GRASS_LOD < 3
    n = grassSafeNormalize(faceDir + float3(tip.x, 0.0f, tip.z) * t, faceDir);
#endif
    n *= input.tangent.w;

    float4 wp = float4(world, 1.0f);
    PSInput o;
    o.currClip = mul(wp, viewProj);
    // Previous wind is not a separate buffer yet. The previous view still has to be finite.
    o.prevClip = mul(wp, prevViewProj);
    o.position = o.currClip;
    o.normalWS = n;
    o.t = input.uv.y;
    return o;
}

GBufferOut PSMain(PSInput input, bool isFront : SV_IsFrontFace)
{
    float t = saturate(input.t);
    float3 albedo = lerp(float3(0.10f, 0.16f, 0.05f), float3(0.22f, 0.38f, 0.10f), t);
    float3 n = input.normalWS;
    if (!isFront)
        n = -n;

    GBufferOut o;
    o.albedo = float4(albedo, 0.0f);
    o.attrib = float4(EncodeOct(n), 0.82f, 0.0f);
    o.velocity = VelocityUv(input.currClip, input.prevClip);
    o.ao = lerp(0.70f, 1.0f, t);
    return o;
}
