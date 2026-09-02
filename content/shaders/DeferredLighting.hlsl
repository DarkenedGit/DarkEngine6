// Fullscreen deferred Lambert + CSM + fog. Reconstructs world position from depth.
#pragma pack_matrix(row_major)

#include "GBuffer.hlsli"
#define SHADOW_T t3
#include "Shadow.hlsli"

cbuffer LightingConstants : register(b0)
{
    float4x4 invViewProj;
    float3   cameraPos;
    float    fogDensity;
    float3   lightDirWS;
    float    lighting; // 1 = Lambert+CSM+fog, 0 = albedo copy
    float3   lightColor;
    float    _pad0;
    float3   ambientColor;
    float    _pad1;
    float3   fogColor;
    float    _pad2;
};

Texture2D gAlbedo : register(t0);
Texture2D gAttrib : register(t1);
Texture2D gDepth  : register(t2);

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

float3 ReconstructWorldPos(float ndcX, float ndcY, float depth)
{
    float4 clip = float4(ndcX, ndcY, depth, 1.0f);
    float4 w    = mul(clip, invViewProj);
    return w.xyz / max(w.w, 1e-6f);
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
    float3 worldPos = ReconstructWorldPos(ndcX, ndcY, depth);

    float3 n     = DecodeOct(attrib.rg);
    float3 l     = normalize(lightDirWS);
    float  ndotl = saturate(dot(n, l));
    float  recvOffset = 0.06f + 0.28f * (1.0f - ndotl) * (1.0f - ndotl);
    float  shadow = ComputeShadow(worldPos + n * recvOffset, cameraPos);
    float3 lit    = ambientColor * albedo.rgb + ndotl * lightColor * albedo.rgb * shadow;
    float  dist   = length(worldPos - cameraPos);
    float  fog    = 1.0f - exp(-fogDensity * dist);
    lit = lerp(lit, fogColor, saturate(fog));
    return float4(lit, 1);
}
