// Multi-frequency Gerstner water. Vertex Y is rest water level.
// VS displaces so LOD seams share the same closed form; PS re-evaluates the
// analytic normal so specular/fresnel are per-pixel (not Gouraud on big tris).
// TEXCOORD0.y is terrain height at that XZ, used for shore fade.
#pragma pack_matrix(row_major)

#include "PbrLighting.hlsli"

cbuffer FrameConstants : register(b0)
{
    float4x4 worldViewProj;
    float3   cameraPos;
    float    time;
    float3   lightDir;
    float    waterLevel;
    float2   flowDir;
    float    flowStrength;
    float    specPower;
    float4   waves[4];     // xy dir, z freq, w amp
    float4   waveSpeed;
    float3   deepColor;
    float    opacity;
    float3   shallowColor;
    float    shoreDepth;
    float3   skyZenith;
    float    fresnelF0;
    float3   skyHorizon;
    float    steepness;
    uint     lightCount;
    // Consecutive scalars pack like C++ uint waterIndex[8]. A cbuffer array would be 16-byte strided.
    uint     waterIndex0;
    uint     waterIndex1;
    uint     waterIndex2;
    uint     waterIndex3;
    uint     waterIndex4;
    uint     waterIndex5;
    uint     waterIndex6;
    uint     waterIndex7;
};

struct GpuLocalLight
{
    float3 pos;
    float  range;
    float3 color;
    float  invRange2;
    float3 dir;
    float  type;
    float  innerCos;
    float  outerCos;
    float  sourceRadius;
    float  pad;
};

StructuredBuffer<GpuLocalLight> gLights : register(t0);

struct VSInput
{
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD0; // y = terrain height
};

struct PSInput
{
    float4 position : SV_POSITION;
    float2 restXZ   : TEXCOORD0;
    float  terrainY : TEXCOORD1;
};

void Gerstner(float2 xz, out float3 offset, out float3 normal)
{
    float y = 0.0f;
    float nx = 0.0f;
    float ny = 1.0f;
    float nz = 0.0f;
    float2 horiz = 0.0f;
    float Q = saturate(steepness);

    [unroll]
    for (int i = 0; i < 4; ++i)
    {
        float2 D = waves[i].xy;
        float  k = waves[i].z;
        float  A = waves[i].w;
        if (A <= 0.0f || k <= 0.0f)
            continue;

        float dp = dot(D, xz) * k + time * waveSpeed[i];
        float s  = sin(dp);
        float c  = cos(dp);
        float wa = k * A;

        y     += A * s;
        horiz += D * (Q * A * c);
        nx    += D.x * wa * c;
        nz    += D.y * wa * c;
        ny    -= Q * wa * s;
    }

    offset = float3(horiz.x, y, horiz.y);
    normal = normalize(float3(-nx, ny, -nz));
}

float3 GerstnerDisplace(float2 xz)
{
    float3 offset;
    float3 n;
    Gerstner(xz, offset, n);
    return offset;
}

PSInput VSMain(VSInput input)
{
    PSInput o;
    float3 world = input.position + GerstnerDisplace(input.position.xz);
    o.restXZ     = input.position.xz;
    o.terrainY   = input.uv.y;
    o.position   = mul(float4(world, 1.0f), worldViewProj);
    return o;
}

float3 SkyColor(float3 dir)
{
    float t = saturate(dir.y * 0.5f + 0.5f);
    return lerp(skyHorizon, skyZenith, t);
}

float4 PSMain(PSInput input) : SV_TARGET
{
    float depth = waterLevel - input.terrainY;
    float shallow = saturate(1.0f - depth / max(shoreDepth, 1e-3f));
    float3 body = lerp(deepColor, shallowColor, shallow);
    float alpha = opacity * saturate(depth / max(shoreDepth * 0.35f, 1e-3f));

    if (specPower < 0.0f)
    {
        alpha = saturate(alpha);
        return float4(body, alpha);
    }

    float3 offset;
    float3 n;
    Gerstner(input.restXZ, offset, n);
    float3 worldPos = float3(input.restXZ.x, waterLevel, input.restXZ.y) + offset;

    float3 v = normalize(cameraPos - worldPos);
    float3 l = normalize(lightDir);

    float ndotv = saturate(dot(n, v));
    float fres  = fresnelF0 + (1.0f - fresnelF0) * pow(1.0f - ndotv, 5.0f);

    float3 r    = reflect(-v, n);
    float3 sky  = SkyColor(r);

    float  ndotl = saturate(dot(n, l));

    float3 color = body * (0.18f + 0.55f * ndotl);
    color = lerp(color, sky, fres);
    // Frozen 0.15 — do not derive from specPower (1 - 96/256 would dull the highlight).
    // metallic 0 => PbrEvaluate F0 = 0.04, matching fresnelF0.
    color += PbrDirectional(n, v, 0.0.xxx, 0.15f, 0.0f, l, 0.85.xxx);

    if (lightCount > 0)
    {
        const uint idx[8] = { waterIndex0, waterIndex1, waterIndex2, waterIndex3, waterIndex4, waterIndex5, waterIndex6, waterIndex7 };
        [unroll]
        for (uint i = 0; i < 8; ++i)
        {
            if (i < lightCount)
            {
                GpuLocalLight light    = gLights[idx[i]];
                float3        toLight  = light.pos - worldPos;
                float         d        = length(toLight);
                float3        li       = toLight / max(d, 1e-4f);
                float         cosTheta = dot(-li, light.dir);
                float3        lit      = PbrPunctual(n, v, body, 0.15f, 0.0f, toLight, light.color, light.sourceRadius);
                lit *= windowedDistanceAttenuation(d * d, light.invRange2);
                if (light.type >= 0.5f)
                    lit *= spotAngleAttenuation(cosTheta, light.innerCos, light.outerCos);
                color += lit;
            }
        }
    }

    // Shore: fade out as the land rises through the surface.
    alpha = saturate(alpha + fres * 0.15f);
    return float4(color, alpha);
}
