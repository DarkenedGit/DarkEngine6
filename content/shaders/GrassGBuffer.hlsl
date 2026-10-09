#pragma pack_matrix(row_major)

#ifndef GRASS_LOD
#define GRASS_LOD 0
#endif

#include "GBuffer.hlsli"
#include "GrassBend.hlsli"

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

    float2 shove = float2(0.0f, 0.0f);
    float2 prevShove = float2(0.0f, 0.0f);
#if GRASS_LOD < 2
    float velX;
    float velZ;
    float prevVelX;
    float prevVelZ;
    grassReadPackedYaw(pad0, velX, velZ, prevVelX, prevVelZ);
    // Distance rejects a disc before that blade normalizes a direction.
    shove = grassGatherShove(blade.x, blade.z, pushMetres, velX, velZ, interactor, footprint);
    prevShove = grassGatherShove(blade.x, blade.z, pushMetres, prevVelX, prevVelZ, prevInteractor, prevFootprint);
#endif

    GrassTipIn tipIn;
    tipIn.height = height;
    tipIn.flex = flex;
    tipIn.windX = windX;
    tipIn.windZ = windZ;
    tipIn.shoveX = shove.x;
    tipIn.shoveZ = shove.y;
    GrassTip tip = grassTipOffset(tipIn);

    GrassTipIn prevIn = tipIn;
    prevIn.shoveX = prevShove.x;
    prevIn.shoveZ = prevShove.y;
    GrassTip prevTip = grassTipOffset(prevIn);

    float t = input.position.y;
    GrassLocal bent = grassBladeLocal(GRASS_LOD, t, tip, height);
    GrassLocal prevBent = grassBladeLocal(GRASS_LOD, t, prevTip, height);

    float s;
    float c;
    sincos(blade.yaw, s, c);
    float3 faceDir = grassYaw(input.normal, s, c);
    float3 widthAxis = grassYaw(input.tangent.xyz, s, c);
    float3 root = float3(blade.x, blade.y, blade.z);
    float3 world = root + widthAxis * input.position.x + float3(bent.x, bent.y, bent.z);
    world.z += input.position.z;
    float3 prevWorld = root + widthAxis * input.position.x + float3(prevBent.x, prevBent.y, prevBent.z);
    prevWorld.z += input.position.z;

    float3 n = faceDir;
#if GRASS_LOD == 0
    {
        float yTip = grassYTip(tip, height);
        float u = 1.0f - t;
        float3 p0 = float3(0.0f, 0.0f, 0.0f);
        float3 p1 = float3(tip.x * 0.25f, height * 0.55f, tip.z * 0.25f);
        float3 p2 = float3(tip.x, yTip, tip.z);
        float3 deriv = 2.0f * u * (p1 - p0) + 2.0f * t * (p2 - p1);
        n = grassSafeNormalize(cross(widthAxis, deriv), faceDir);
    }
#elif GRASS_LOD < 3
    n = grassSafeNormalize(faceDir + float3(tip.x, 0.0f, tip.z) * t, faceDir);
#endif
    n *= input.tangent.w;

    float4 wp = float4(world, 1.0f);
    float4 prevWp = float4(prevWorld, 1.0f);
    PSInput o;
    o.currClip = mul(wp, viewProj);
    // Previous shove is a real blade, not a zero clip, even when the interactor block is empty.
    o.prevClip = mul(prevWp, prevViewProj);
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
