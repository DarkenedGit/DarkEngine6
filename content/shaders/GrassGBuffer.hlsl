#pragma pack_matrix(row_major)

#ifndef GRASS_LOD
#define GRASS_LOD 0
#endif

#include "GBuffer.hlsli"
#include "GrassBend.hlsli"
#include "Thermal.hlsli"
#include "Ultraviolet.hlsli"

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
    float4 cameraPos : packoffset(c33);
    float4 cameraRight : packoffset(c34);
    float4 cameraLook : packoffset(c35);
    float4 face : packoffset(c36); // x edge start, y yaw blend, z tip lean, w steep start
};

// One logical uint. FXC still declares one 16-byte register, so the root range is 4 DWORDs.
cbuffer GrassDrawConstants : register(b1)
{
    uint baseIndex;
    uint padDraw1;
    uint padDraw2;
    uint padDraw3;
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
StructuredBuffer<uint> gIndices : register(t2);

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

// A full billboard kills the wind silhouette. Turn the card only while it is edge-on, and keep the root planted.
void grassApplyFacing(float3 root, float height, float along, inout float3 widthAxis, inout float3 faceDir, out float3 leanOff, out float3 leanSlope)
{
    float3 toCam = cameraPos.xyz - root;
    float len2 = dot(toCam, toCam);
    float3 viewDir = len2 > 1e-8f ? toCam * rsqrt(len2) : float3(0.0f, 1.0f, 0.0f);

    float facing = abs(dot(viewDir, faceDir));
    float edge = saturate((face.x - facing) / max(face.x, 1e-3f));
    float yawBlend = edge * saturate(face.y);
    if (yawBlend > 0.0f)
    {
        float3 right = cameraRight.xyz;
        // Stay in the card's current half so the width does not flip through zero.
        if (dot(right, widthAxis) < 0.0f)
            right = -right;
        widthAxis = grassSafeNormalize(lerp(widthAxis, right, yawBlend), widthAxis);
        float3 turned = cross(float3(0.0f, 1.0f, 0.0f), widthAxis);
        if (dot(turned, faceDir) < 0.0f)
            turned = -turned;
        faceDir = grassSafeNormalize(turned, faceDir);
    }

    float steep = saturate((viewDir.y - face.w) / max(1.0f - face.w, 1e-3f));
    float3 horiz = float3(viewDir.x, 0.0f, viewDir.z);
    float h2 = dot(horiz, horiz);
    float3 leanDir;
    if (h2 > 0.0025f)
        leanDir = horiz * rsqrt(h2);
    else
    {
        float3 fwd = float3(cameraLook.x, 0.0f, cameraLook.z);
        float f2 = dot(fwd, fwd);
        if (f2 > 1e-8f)
            leanDir = fwd * rsqrt(f2);
        else
        {
            leanDir = float3(cameraRight.x, 0.0f, cameraRight.z);
            float r2 = dot(leanDir, leanDir);
            leanDir = r2 > 1e-8f ? leanDir * rsqrt(r2) : float3(1.0f, 0.0f, 0.0f);
        }
    }

    leanSlope = leanDir * (steep * edge * max(face.z, 0.0f) * height);
    leanOff = leanSlope * saturate(along);
}

PSInput VSMain(VSInput input)
{
    GrassBladeGpu blade = gBlades[gIndices[input.iid + baseIndex]];
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
    float3 leanOff;
    float3 leanSlope;
    grassApplyFacing(root, height, t, widthAxis, faceDir, leanOff, leanSlope);
    float3 world = root + widthAxis * input.position.x + float3(bent.x, bent.y, bent.z) + leanOff;
    world.z += input.position.z;
    // Same facing on the previous position. A view-dependent delta in the velocity buffer smears under TAA.
    float3 prevWorld = root + widthAxis * input.position.x + float3(prevBent.x, prevBent.y, prevBent.z) + leanOff;
    prevWorld.z += input.position.z;

    float3 n = faceDir;
#if GRASS_LOD == 0
    {
        float yTip = grassYTip(tip, height);
        float u = 1.0f - t;
        float3 p0 = float3(0.0f, 0.0f, 0.0f);
        float3 p1 = float3(tip.x * 0.25f, height * 0.55f, tip.z * 0.25f);
        float3 p2 = float3(tip.x, yTip, tip.z);
        float3 deriv = 2.0f * u * (p1 - p0) + 2.0f * t * (p2 - p1) + leanSlope;
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
#ifdef DE_ULTRAVIOLET
    o.albedo = float4(albedo, saturate(DE_UV_CLASS));
#else
    o.albedo = float4(albedo, thermalScalar(0.0f, 0.0f));
#endif
    o.attrib = float4(EncodeOct(n), 0.82f, 0.0f);
    o.velocity = VelocityUv(input.currClip, input.prevClip);
    o.ao = lerp(0.70f, 1.0f, t);
    return o;
}
