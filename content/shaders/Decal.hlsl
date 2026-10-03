// Oriented-box deferred decals. RMW albedo and attrib as rasterizer-ordered R32_UINT.
#pragma pack_matrix(row_major)

#include "GBuffer.hlsli"
#include "Color.hlsli"
#include "Depth.hlsli"

cbuffer DecalPassConstants : register(b0)
{
    float4x4 invViewProj;
    float4x4 viewProj;
};

RasterizerOrderedTexture2D<uint> gAlbedoUav : register(u0);
RasterizerOrderedTexture2D<uint> gAttribUav : register(u1);
Texture2D gDepth : register(t0);
Texture2D gAlbedo : register(t1);
Texture2D gNormal : register(t2);
SamplerState gSamp : register(s0);

// Same bytes as DecalGpuInstance. angleFadeRange is byte 208, channelMask is byte 212.
struct DecalGpuInstance
{
    float4 world0, world1, world2, world3;
    float4 inv0, inv1, inv2, inv3;
    float4 tintAlbedo;  // tint.xyz, albedoWeight
    float4 normalRough; // scale, weight, roughTarget, roughWeight
    float4 metalEmis;   // metalTarget, metalWeight, emisTarget, emisWeight
    float4 uvScaleBias;
    float4 axisYAngle;     // axisY.xyz, angleFadeStart
    float  angleFadeRange; // byte offset 208
    uint   channelMask;    // byte offset 212
    float  clipLocalYMin;
    float  clipLocalYMax;
};

StructuredBuffer<DecalGpuInstance> gInst : register(t3);

static const uint kChannelAlbedo    = 1u;
static const uint kChannelNormal    = 2u;
static const uint kChannelRoughness = 4u;
static const uint kChannelMetallic  = 8u;
static const uint kChannelEmissive  = 16u;
static const float kEdgeBand        = 0.15f;

struct PSIn
{
    float4 position : SV_POSITION;
    nointerpolation uint iid : INSTANCEID;
};

uint packRgba(uint r, uint g, uint b, uint a)
{
    return (r & 255u) | ((g & 255u) << 8) | ((b & 255u) << 16) | ((a & 255u) << 24);
}

uint unorm8(float c)
{
    return uint(saturate(c) * 255.0f + 0.5f);
}

uint3 linearToSrgb8(float3 c)
{
    float3 s = linearToSrgb(c);
    return uint3(unorm8(s.r), unorm8(s.g), unorm8(s.b));
}

float3 unpackRgb01(uint raw)
{
    return float3(float(raw & 255u), float((raw >> 8) & 255u), float((raw >> 16) & 255u)) / 255.0f;
}

PSIn VSMain(float3 pos : POSITION, uint iid : SV_InstanceID)
{
    PSIn o;
    DecalGpuInstance inst = gInst[iid];
    float3 world = mul(float4(pos, 1.0f), float4x4(inst.world0, inst.world1, inst.world2, inst.world3)).xyz;
    o.position = mul(float4(world, 1.0f), viewProj);
    o.iid = iid;
    return o;
}

PSIn VSFullscreen(uint id : SV_VertexID, uint iid : SV_InstanceID)
{
    PSIn o;
    float2 pos = float2((id << 1) & 2, id & 2) * 2.0f - 1.0f;
    o.position = float4(pos, 0.0f, 1.0f);
    o.iid = iid;
    return o;
}

[earlydepthstencil]
void PSMain(float4 svPos : SV_POSITION, nointerpolation uint iid : INSTANCEID)
{
    int2 pix = int2(svPos.xy);
    float d = gDepth.Load(int3(pix, 0)).r;
    if (IsSkyDepth(d))
        discard;

    DecalGpuInstance inst = gInst[iid];
    uint w, h;
    gDepth.GetDimensions(w, h);
    float ndcX = (svPos.x / float(w)) * 2.0f - 1.0f;
    float ndcY = 1.0f - (svPos.y / float(h)) * 2.0f;
    float3 worldPos = ReconstructWorldPos(ndcX, ndcY, d, invViewProj);
    float3 local = mul(float4(worldPos, 1.0f), float4x4(inst.inv0, inst.inv1, inst.inv2, inst.inv3)).xyz;
    if (abs(local.x) > 1.0f || abs(local.z) > 1.0f || local.y < inst.clipLocalYMin || local.y > inst.clipLocalYMax)
        discard;

    uint attrib = gAttribUav[pix];
    uint dstR = attrib & 255u;
    uint dstG = (attrib >> 8) & 255u;
    uint dstB = (attrib >> 16) & 255u;
    uint dstA = (attrib >> 24) & 255u;
    float3 geomN = DecodeOct(float2(float(dstR), float(dstG)) / 255.0f);

    float nd = abs(dot(geomN, inst.axisYAngle.xyz));
    if (nd < inst.axisYAngle.w)
        discard;
    float angleFade = 1.0f;
    if (inst.angleFadeRange > 1.0e-6f)
        angleFade = saturate((nd - inst.axisYAngle.w) / inst.angleFadeRange);

    float2 uv = (local.xz * float2(0.5f, -0.5f) + 0.5f) * inst.uvScaleBias.xy + inst.uvScaleBias.zw;
    float edge = saturate((1.0f - abs(local.y)) / kEdgeBand);
    float4 albedoTex = gAlbedo.SampleLevel(gSamp, uv, 0.0f);
    float spatial = albedoTex.a * angleFade * edge;

    float wA = (inst.channelMask & kChannelAlbedo) ? saturate(inst.tintAlbedo.w * spatial) : 0.0f;
    float wE = (inst.channelMask & kChannelEmissive) ? saturate(inst.metalEmis.w * spatial) : 0.0f;
    float wN = (inst.channelMask & kChannelNormal) ? saturate(inst.normalRough.y * spatial) : 0.0f;
    float wR = (inst.channelMask & kChannelRoughness) ? saturate(inst.normalRough.w * spatial) : 0.0f;
    float wM = (inst.channelMask & kChannelMetallic) ? saturate(inst.metalEmis.y * spatial) : 0.0f;

    if (wA > 0.0f || wE > 0.0f)
    {
        uint raw = gAlbedoUav[pix];
        float3 dstLin = srgbToLinear(unpackRgb01(raw));
        float dstE = float((raw >> 24) & 255u) / 255.0f;
        float3 srcLin = albedoTex.rgb * inst.tintAlbedo.xyz;
        float3 outLin = lerp(dstLin, srcLin, wA);
        float outE = lerp(dstE, inst.metalEmis.z, wE);
        uint3 rgb = wA > 0.0f ? linearToSrgb8(outLin) : uint3(raw & 255u, (raw >> 8) & 255u, (raw >> 16) & 255u);
        uint a = wE > 0.0f ? unorm8(outE) : ((raw >> 24) & 255u);
        gAlbedoUav[pix] = packRgba(rgb.r, rgb.g, rgb.b, a);
    }

    if (wN > 0.0f || wR > 0.0f || wM > 0.0f)
    {
        uint pr = dstR;
        uint pg = dstG;
        uint pb = dstB;
        uint pa = dstA;
        if (wN > 0.0f)
        {
            float3 axisX = normalize(inst.world0.xyz);
            float3 axisZ = normalize(inst.world2.xyz);
            float3 N = normalize(geomN);
            float3 T = axisX - N * dot(N, axisX);
            if (dot(T, T) < 1.0e-6f)
                T = axisZ - N * dot(N, axisZ);
            T = normalize(T);
            float3 B = cross(N, T);
            if (dot(B, axisZ) < 0.0f)
                B = -B;

            float3 ts = gNormal.SampleLevel(gSamp, uv, 0.0f).xyz * 2.0f - 1.0f;
            ts.xy *= inst.normalRough.x;
            float3 decalN = normalize(ts.x * T + ts.y * B + ts.z * N);
            float3 n = normalize(lerp(geomN, decalN, wN));
            float2 oct = EncodeOct(n);
            pr = unorm8(oct.x);
            pg = unorm8(oct.y);
        }
        if (wR > 0.0f)
            pb = unorm8(lerp(float(dstB) / 255.0f, inst.normalRough.z, wR));
        if (wM > 0.0f)
            pa = unorm8(lerp(float(dstA) / 255.0f, inst.metalEmis.x, wM));
        gAttribUav[pix] = packRgba(pr, pg, pb, pa);
    }
}
