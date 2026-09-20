// Fullscreen deferred GGX + CSM + lit distance/height/valley fog.
// Reconstructs world position from depth.
#pragma pack_matrix(row_major)

#include "GBuffer.hlsli"
#include "PbrLighting.hlsli"
#include "IblSampling.hlsli"
#define SHADOW_T t3
#include "Shadow.hlsli"
#define FOG_SAMPLE_CSM 1
#include "Fog.hlsli"

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
    float    ssrEnabled;    // 11.z
    float    _padPbr1;      // 11.w
    float3   pbrLightColor; // 12.xyz — π-scaled; PbrDirectional only
    float    iblIntensity;  // 12.w
    float    iblRotationRadY;
    float    iblMaxRoughnessMip;
    float    iblEnabled;
    float    iblDebug;
};

Texture2D    gAlbedo    : register(t0);
Texture2D    gAttrib    : register(t1);
Texture2D    gDepth     : register(t2);
Texture2D    gHeightMap : register(t4);
Texture2D    gAo        : register(t5);
TextureCube  gIblIrradiance : register(t6);
TextureCube  gIblPrefilter  : register(t7);
Texture2D    gIblBrdfLut    : register(t8);
Texture2D    gSsr           : register(t9);
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

    if (IsSkyDepth(depth))
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
    float  ao        = gAo.Load(int3(texel, 0)).r;
    float3 v         = normalize(cameraPos - worldPos);
    float3 F0        = lerp(float3(0.04, 0.04, 0.04), albedo.rgb, metallic);

    float3 nRot = IblRotateY(n, iblRotationRadY);
    float3 r    = reflect(-v, n);
    float3 rRot = IblRotateY(r, iblRotationRadY);

    // Function scope — debug views must compile when iblEnabled is 0 (dummy SRVs are bound).
    float3 irr = 0;
    float3 pre = 0;
    float2 dfg = 0;

    if (iblEnabled >= 0.5f || iblDebug >= 0.5f)
    {
        irr = gIblIrradiance.Sample(gHeightSamp, nRot).rgb;
        pre = gIblPrefilter.SampleLevel(gHeightSamp, rRot, saturate(roughness) * iblMaxRoughnessMip).rgb;
        float NdotV = max(saturate(dot(n, v)), 1e-4f);
        dfg = gIblBrdfLut.Sample(gHeightSamp, float2(NdotV, saturate(roughness))).rg;
    }

    float4 ssr  = gSsr.Load(int3(texel, 0));
    float  conf = (ssrEnabled >= 0.5f && iblDebug < 0.5f) ? saturate(ssr.a) : 0.0f;

    float3 specIblTerm = 0.0.xxx;
    float3 diffTerm    = ambientColor * albedo.rgb * ao;
    if (iblEnabled >= 0.5f)
    {
        float3 Fd      = albedo.rgb * (1.0f - metallic) * (1.0f / DE_PBR_PI);
        float3 specIbl = pre * (F0 * dfg.x + dfg.y);
        specIblTerm    = specIbl * ao * iblIntensity;
        diffTerm       = Fd * irr * ao * iblIntensity;
    }
    float3 spec = lerp(specIblTerm, ssr.rgb, conf); // SSR: no AO, no iblIntensity

    float3 l          = normalize(lightDirWS);
    float  ndotl      = saturate(dot(n, l));
    float  recvOffset = 0.06f + 0.28f * (1.0f - ndotl) * (1.0f - ndotl);
    float  shadow     = ComputeShadow(worldPos + n * recvOffset, cameraPos);

    float3 lit = diffTerm + spec
        + PbrDirectional(n, v, albedo.rgb, roughness, metallic, lightDirWS, pbrLightColor) * shadow
        + albedo.rgb * emissive * emissiveGain;

    // Debug is gated on iblDebug only, not iblEnabled. Dummy cubes → black. Skip fog.
    if (iblDebug >= 0.5f && iblDebug < 1.5f)
        return float4(irr, 1); // irradiance along nRot
    if (iblDebug >= 1.5f && iblDebug < 2.5f)
        return float4(gIblPrefilter.SampleLevel(gHeightSamp, nRot, 0).rgb, 1);
        // lod0 along **nRot** (the cube itself), not rRot — not a reflection preview
    if (iblDebug >= 2.5f)
        return float4(dfg.x, dfg.y, 0, 1);

    FogResult fog = FogIntegrate(cameraPos, worldPos, MakeFogParams(), gHeightMap, gHeightSamp, shadow);
    return float4(ApplyLitFog(lit, fog), 1);
}
