// Instanced skinned forward color. Part world is already baked.
// color.rgb tints albedo. color.a is the host emissive scalar, not base alpha.
#pragma pack_matrix(row_major)

#include "Color.hlsli"
#include "PbrLighting.hlsli"

#ifndef ENCODE_SRGB
#define ENCODE_SRGB 0
#endif

#define DE_SKINNED_INSTANCES_REG t5
#define DE_SKINNED_INDICES_REG t6
#define DE_SKINNED_PALETTES_REG t7
#include "SkinnedInstance.hlsli"

cbuffer MeshForwardBatchConstants : register(b0)
{
    float4x4 viewProj       : packoffset(c0);
    float3   lightDirWS     : packoffset(c4);
    float    ambientScale   : packoffset(c4.w);
    float3   lightColor     : packoffset(c5);
    float    padLight       : packoffset(c5.w);
    float3   cameraPos      : packoffset(c6);
    float    lighting       : packoffset(c6.w);
    float    normalScale    : packoffset(c7);
    float    ao             : packoffset(c7.y);
    float    alphaCutoff    : packoffset(c7.z);
    float    alphaModeMask  : packoffset(c7.w);
    uint     baseIndex      : packoffset(c8);
    uint     pad0           : packoffset(c8.y);
    uint     pad1           : packoffset(c8.z);
    uint     pad2           : packoffset(c8.w);
};

Texture2D    gAlbedo   : register(t0);
Texture2D    gNormal   : register(t1);
Texture2D    gOrm      : register(t2);
Texture2D    gEmissive : register(t3);
SamplerState gSamp     : register(s0);

#include "NormalMap.hlsli"

#define SHADOW_T t4
#include "Shadow.hlsli"

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
    float3 worldPos  : TEXCOORD1;
    float4 tangentWS : TANGENT;
    nointerpolation float4 color : COLOR0;
};

PSInput VSMain(VSInput input)
{
    SkinnedInstanceGpu inst  = gInstances[gIndices[input.iid + baseIndex]];
    float4x4           world = skinnedRows(inst.world0, inst.world1, inst.world2, inst.world3);
    float4             posM  = skinPosCurr(inst.paletteIndex, input.position, input.joints, input.weights);
    float4             prevM = skinPosPrev(inst.paletteIndex, input.position, input.joints, input.weights);
    float3             nM    = skinNrm(inst.paletteIndex, input.normal, input.joints, input.weights);
    float3             tM    = skinNrm(inst.paletteIndex, input.tangent.xyz, input.joints, input.weights);
    float4             wp    = mul(posM, world);
    PSInput            o;
    o.position  = mul(wp, viewProj);
    o.normalWS  = mul(float4(nM, 0.0f), world).xyz;
    o.tangentWS = float4(mul(float4(tM, 0.0f), world).xyz, input.tangent.w);
    o.uv        = input.uv;
    o.worldPos  = wp.xyz;
    o.color     = inst.color;

    // Dead branch keeps the 144-byte batch, the 160-byte instance, and the prev palette chunks.
    uint  keep  = pad0 | pad1 | pad2 | inst.pad0 | inst.pad1 | inst.pad2;
    float keepF = lightDirWS.x + ambientScale + lightColor.x + padLight + cameraPos.x + lighting + normalScale + ao
                + alphaCutoff + alphaModeMask + inst.prev0.x + inst.prev1.y + inst.prev2.z + inst.prev3.w + prevM.x;
    if (keep == 0xFFFFFFFFu && keepF == 0.0f)
        o.position = 0.0f;
    return o;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    float4 albedoS = gAlbedo.Sample(gSamp, input.uv);
    if (alphaModeMask > 0.5f)
        clip(albedoS.a - alphaCutoff);

    uint keep = baseIndex | pad0 | pad1 | pad2;
    if (keep == 0xFFFFFFFFu)
        albedoS.rgb = viewProj._m00.xxx;

    float3 albedo = albedoS.rgb * input.color.rgb;
    if (lighting < 0.5f)
        return float4(encodeSceneRgb(albedo), albedoS.a);

    float3 n      = ApplyNormalMap(input.normalWS, input.tangentWS, input.uv);
    float3 l      = normalize(lightDirWS);
    float  ndotl  = saturate(dot(n, l));
    float  shadow = ComputeShadow(input.worldPos, cameraPos);
    float  aoTerm = saturate(gOrm.Sample(gSamp, input.uv).r * ao);
    float3 ambient = ambientScale * albedo * aoTerm;
    float3 diffuse = ndotl * lightColor * albedo * shadow * (1.0f / DE_PBR_PI);
    float  emis = dot(gEmissive.Sample(gSamp, input.uv).rgb, float3(0.2126f, 0.7152f, 0.0722f)) * input.color.a;
    return float4(encodeSceneRgb(ambient + diffuse + emis), albedoS.a);
}
