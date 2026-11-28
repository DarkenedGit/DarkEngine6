// Full-screen heat picture (VisionModesFeature). Reads the G-buffer, depth and SurfaceTag and
// writes HDR. The PSO blends by the blend factor, so strength 1 replaces HDR and a fade lerps.
// VisionHeatConstants and VisionHeatLut must match Game/VisionModesPipeline.h.
#pragma pack_matrix(row_major)

#include "GBuffer.hlsli"
#include "Thermal.hlsli"

cbuffer VisionHeatConstants : register(b0)
{
    float4x4 invViewProj;
    float3   cameraPos;
    float    _heatPad0;
    float3   sunDir;
    float    _heatPad1;
};

// 256 heat classes, four per register (SurfaceTag R is tag / 255).
cbuffer VisionHeatLut : register(b1)
{
    float4 heatLut[64];
};

Texture2D gAlbedo     : register(t0);
Texture2D gAttrib     : register(t1);
Texture2D gDepth      : register(t2);
Texture2D gSurfaceTag : register(t3);

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

float heatClass(uint tag)
{
    const float4 row = heatLut[(tag & 255u) >> 2];
    const uint   lane = tag & 3u;
    return lane == 0u ? row.x : (lane == 1u ? row.y : (lane == 2u ? row.z : row.w));
}

float4 PSMain(PSInput input) : SV_TARGET
{
    const int2  texel = int2(input.position.xy);
    const float depth = gDepth.Load(int3(texel, 0)).r;
    if (IsSkyDepth(depth))
    {
        // Cold air, with the sun disk at the hot stop.
        uint w, h;
        gDepth.GetDimensions(w, h);
        const float  ndcX = (input.position.x / float(w)) * 2.0f - 1.0f;
        const float  ndcY = 1.0f - (input.position.y / float(h)) * 2.0f;
        const float3 dir  = normalize(ReconstructWorldPos(ndcX, ndcY, 1.0f, invViewProj) - cameraPos);
        const float  disk = smoothstep(0.9995f, 0.99985f, saturate(dot(dir, normalize(sunDir))));
        return float4(thermalFalseColor(lerp(kThermalSky, 1.0f, disk)), 1.0f);
    }
    const float4 albedo = gAlbedo.Load(int3(texel, 0));
    const float4 attrib = gAttrib.Load(int3(texel, 0));
    const uint   tag    = (uint)round(saturate(gSurfaceTag.Load(int3(texel, 0)).r) * 255.0f);
    return float4(thermalFalseColor(thermalScalar(heatClass(tag), albedo.a, attrib.a)), 1.0f);
}
