// Fullscreen deferred GGX + CSM + lit distance/height/valley fog.
// Reconstructs world position from depth.
#pragma pack_matrix(row_major)

#include "GBuffer.hlsli"
#include "PbrLighting.hlsli"
#include "Fog.hlsli"
#define SHADOW_T t3
#include "Shadow.hlsli"

cbuffer LightingConstants : register(b0)
{
    float4x4 invViewProj;
    float3   cameraPos;
    float    fogDensity;
    float3   lightDirWS;
    float    lighting; // 1 = GGX+CSM+fog, 0 = albedo copy
    float3   lightColor;
    float    emissiveGain;
    float3   ambientColor;
    float    heightFogDensity;
    float3   fogColor;
    float    heightFogFalloff;
    float    heightFogHeight;
    float    volumetricFogDensity;
    float    waterLevel;
    float    volumetricHeight;
    float    heightOriginX;
    float    heightOriginZ;
    float    heightCellSize;
    float    heightWorldSizeX;
    float    heightWorldSizeZ;
    float    _padFog;
};

Texture2D    gAlbedo    : register(t0);
Texture2D    gAttrib    : register(t1);
Texture2D    gDepth     : register(t2);
Texture2D    gHeightMap : register(t4);
SamplerState gHeightSamp : register(s2);

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

FogParams MakeFogParams()
{
    FogParams p;
    p.cameraPos            = cameraPos;
    p.lightDir             = lightDirWS;
    p.lightColor           = lightColor;
    p.ambientColor         = ambientColor;
    p.fogColor             = fogColor;
    p.fogDensity           = fogDensity;
    p.heightFogDensity     = heightFogDensity;
    p.heightFogFalloff     = heightFogFalloff;
    p.heightFogHeight      = heightFogHeight;
    p.volumetricFogDensity = volumetricFogDensity;
    p.waterLevel           = waterLevel;
    p.volumetricHeight     = volumetricHeight;
    p.heightOrigin         = float2(heightOriginX, heightOriginZ);
    p.heightCellSize       = heightCellSize;
    p.heightWorldSize      = float2(heightWorldSizeX, heightWorldSizeZ);
    return p;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    int2   texel  = int2(input.position.xy);
    float  depth  = gDepth.Load(int3(texel, 0)).r;
    float4 albedo = gAlbedo.Load(int3(texel, 0));
    float4 attrib = gAttrib.Load(int3(texel, 0));

    if (depth >= 1.0f - 1e-6f)
        discard;

    if (lighting < 0.5f)
        return float4(albedo.rgb, 1);

    uint w, h;
    gDepth.GetDimensions(w, h);
    float ndcX = (input.position.x / float(w)) * 2.0f - 1.0f;
    float ndcY = 1.0f - (input.position.y / float(h)) * 2.0f;
    float3 worldPos = ReconstructWorldPos(ndcX, ndcY, depth, invViewProj);

    float3 n         = DecodeOct(attrib.rg);
    float  roughness = attrib.b;
    float  metallic  = attrib.a;
    float  emissive  = albedo.a;
    float3 v         = normalize(cameraPos - worldPos);
    float3 l         = normalize(lightDirWS);
    float  ndotl     = saturate(dot(n, l));
    float  recvOffset = 0.06f + 0.28f * (1.0f - ndotl) * (1.0f - ndotl);
    float  shadow = ComputeShadow(worldPos + n * recvOffset, cameraPos);
    float3 lit    = ambientColor * albedo.rgb
                  + PbrDirectional(n, v, albedo.rgb, roughness, metallic, lightDirWS, lightColor) * shadow
                  + albedo.rgb * emissive * emissiveGain;

    FogResult fog = FogIntegrate(cameraPos, worldPos, MakeFogParams(), gHeightMap, gHeightSamp, shadow);
    return float4(ApplyLitFog(lit, fog), 1);
}
