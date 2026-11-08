// Expands one camera-facing quad per instance. Dead slots are zero-area.
// Tint matches ParticleRenderer: rgb *= baseColor.rgb * (1 + emissive * gain).
#pragma pack_matrix(row_major)

#include "ParticleGpu.hlsli"
#include "Color.hlsli"

#ifndef ENCODE_SRGB
#define ENCODE_SRGB 0
#endif

StructuredBuffer<GpuParticle> gParticles : register(t0);

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD0;
    float4 color    : COLOR0;
};

float4 unpackRgba8(uint packed)
{
    return float4(float(packed & 255u), float((packed >> 8u) & 255u), float((packed >> 16u) & 255u), float((packed >> 24u) & 255u)) * (1.0 / 255.0);
}

PSInput VSMain(uint vid : SV_VertexID, uint iid : SV_InstanceID)
{
    PSInput o;
    o.position = float4(0, 0, 0, 1);
    o.uv = float2(0, 0);
    o.color = float4(0, 0, 0, 0);
    if (iid >= sim0.x)
        return o;

    GpuParticle p = gParticles[iid];
    float3 center = float3(p.position0, p.position1, p.position2);
    bool alive = (p.flags & kGpuParticleAlive) != 0u;
    float size = 0;
    float4 col = float4(0, 0, 0, 0);
    if (alive && p.maxLife > 1.0e-6)
    {
        float t = saturate(1.0 - p.life / p.maxLife);
        size = lerp(p.size0, p.size1, t);
        col = lerp(unpackRgba8(p.color0), unpackRgba8(p.color1), t);
        float emissiveScale = 1.0 + sim2.x * sim2.y;
        col.rgb *= baseColor.rgb * emissiveScale;
        col.a *= baseColor.a;
    }

    float hx = size * 0.5;
    float hy = size * 0.5;
    float3 right = cameraRight.xyz * hx;
    float3 up = cameraUp.xyz * hy;
    float3 pos = center;
    float2 uv = float2(0, 0);
    if (vid == 0u || vid == 3u)
    {
        pos = center - right - up;
        uv = float2(0, 1);
    }
    else if (vid == 1u)
    {
        pos = center + right - up;
        uv = float2(1, 1);
    }
    else if (vid == 2u || vid == 4u)
    {
        pos = center + right + up;
        uv = float2(1, 0);
    }
    else
    {
        pos = center - right + up;
        uv = float2(0, 0);
    }

    o.position = mul(float4(pos, 1.0), viewProj);
    o.uv = uv;
    o.color = col;
    return o;
}
