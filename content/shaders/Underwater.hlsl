// Scene-referred underwater column. Travel matches Render/Underwater.h.
// Sigma and the in-scatter stay here. They are linear Rec.709, not a display tint.
#pragma pack_matrix(row_major)

#include "GBuffer.hlsli"

cbuffer UnderwaterConstants : register(b0)
{
    float4x4 invViewProj;
    float3   cameraPos;
    float    waterLevel;
    float    amount;
};

Texture2D gColor : register(t0);
Texture2D gDepth : register(t1);

static const float  kUnderSigma     = 0.12f;
static const float3 kUnderInScatter = float3(0.02f, 0.08f, 0.10f);

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

float UnderwaterTravel(float camY, float hitY, float waterY, float dist)
{
    if (!(dist > 0.0f) || !(camY < waterY))
        return 0.0f;
    if (hitY <= waterY)
        return dist;
    float denom = hitY - camY;
    if (!(denom > 1.0e-6f))
        return dist;
    float t = (waterY - camY) / denom;
    if (!(t > 0.0f))
        return 0.0f;
    if (t >= 1.0f)
        return dist;
    return t * dist;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    int2  texel = int2(input.position.xy);
    float depth = gDepth.Load(int3(texel, 0)).r;
    float3 lit  = gColor.Load(int3(texel, 0)).rgb;

    uint w;
    uint h;
    gDepth.GetDimensions(w, h);
    float ndcX = (input.position.x / float(w)) * 2.0f - 1.0f;
    float ndcY = 1.0f - (input.position.y / float(h)) * 2.0f;
    float3 world = ReconstructWorldPos(ndcX, ndcY, depth, invViewProj);
    float dist   = distance(cameraPos, world);
    float travel = UnderwaterTravel(cameraPos.y, world.y, waterLevel, dist);
    float optical = kUnderSigma * travel * saturate(amount);
    float transmit = exp(-optical);
    float3 color = lit * transmit + kUnderInScatter * (1.0f - transmit);
    return float4(color, 1.0f);
}
