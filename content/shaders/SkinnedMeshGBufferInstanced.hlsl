// Instanced skinned G-buffer. Part world is already baked. Palette is gPalettes[paletteIndex].
#pragma pack_matrix(row_major)

#include "GBuffer.hlsli"
#include "Thermal.hlsli"
#include "SkinnedInstance.hlsli"

cbuffer MeshBatchConstants : register(b0)
{
    float4x4 viewProj;
    float4x4 prevViewProj;
    float    roughness;
    float    metallic;
    float    ao;
    float    normalScale;
    float    alphaCutoff;
    float    alphaModeMask;
    uint     baseIndex;
    uint     pad;
};

Texture2D    gAlbedo   : register(t0);
Texture2D    gNormal   : register(t1);
Texture2D    gOrm      : register(t2);
Texture2D    gEmissive : register(t3);
SamplerState gSamp     : register(s0);

#include "UvMap.hlsli"
#include "NormalMap.hlsli"

struct VSInput
{
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD0;
    float4 tangent  : TANGENT;
    uint4  joints   : BLENDINDICES;
    float4 weights  : BLENDWEIGHT;
    uint   iid      : SV_InstanceID;
};

struct PSInput
{
    float4 position  : SV_POSITION;
    float3 normalWS  : NORMAL;
    float2 uv        : TEXCOORD0;
    float4 currClip  : TEXCOORD1;
    float4 prevClip  : TEXCOORD2;
    float4 tangentWS : TANGENT;
    nointerpolation float4 color : COLOR0;
};

PSInput VSMain(VSInput input)
{
    SkinnedInstanceGpu inst    = gInstances[gIndices[input.iid + baseIndex]];
    float4x4           world   = skinnedRows(inst.world0, inst.world1, inst.world2, inst.world3);
    float4x4           prev    = skinnedRows(inst.prev0, inst.prev1, inst.prev2, inst.prev3);
    float4             posM    = skinPosCurr(inst.paletteIndex, input.position, input.joints, input.weights);
    float4             prevM   = skinPosPrev(inst.paletteIndex, input.position, input.joints, input.weights);
    float3             nM      = skinNrm(inst.paletteIndex, input.normal, input.joints, input.weights);
    float3             tM      = skinNrm(inst.paletteIndex, input.tangent.xyz, input.joints, input.weights);
    PSInput            o;
    o.currClip  = mul(mul(posM, world), viewProj);
    o.prevClip  = mul(mul(prevM, prev), prevViewProj);
    o.position  = o.currClip;
    o.normalWS  = mul(float4(nM, 0.0f), world).xyz;
    o.tangentWS = float4(mul(float4(tM, 0.0f), world).xyz, input.tangent.w);
    o.uv        = input.uv;
    o.color     = inst.color;

    uint  keep  = inst.pad0 | inst.pad1 | inst.pad2 | pad;
    float keepF = roughness + metallic + ao + normalScale + alphaCutoff + alphaModeMask;
    if (keep == 0xFFFFFFFFu && keepF == 0.0f)
        o.position = 0.0f;
    return o;
}

GBufferOut PSMain(PSInput input)
{
    float4 albedoS = gAlbedo.Sample(gSamp, input.uv);
    if (alphaModeMask > 0.5f)
        clip(albedoS.a - alphaCutoff);

    uint keep = baseIndex | pad;
    if (keep == 0xFFFFFFFFu)
        albedoS.rgb = (viewProj._m00 + prevViewProj._m00).xxx;

    float3 albedo = albedoS.rgb * input.color.rgb;
    float3 n      = ApplyNormalMap(input.normalWS, input.tangentWS, input.uv);

    float4 orm   = gOrm.Sample(gSamp, input.uv);
    float  rough = saturate(orm.g * roughness);
    float  metal = saturate(orm.b * metallic);
    float  aoOut = saturate(orm.r * ao);

    float emis = dot(gEmissive.Sample(gSamp, input.uv).rgb, float3(0.2126f, 0.7152f, 0.0722f)) * input.color.a;

    GBufferOut o;
#ifdef DE_ULTRAVIOLET
    o.albedo   = float4(albedo, uvReflectanceAt(input.uv, metal));
#else
    o.albedo   = float4(albedo, thermalScalar(emis, metal));
#endif
    o.attrib   = float4(EncodeOct(n), rough, metal);
    o.velocity = VelocityUv(input.currClip, input.prevClip);
    o.ao       = aoOut;
    return o;
}
