// Fullscreen atmosphere: Rayleigh/Mie-inspired sky + coverage clouds.
// Must stay visually consistent with Sky::Environment::evaluateSky (CPU).
#pragma pack_matrix(row_major)

#include "Color.hlsli"
#ifndef ENCODE_SRGB
#define ENCODE_SRGB 0
#endif

#define SHADOW_T t0
#include "Shadow.hlsli"
#define FOG_SAMPLE_CSM 1
#include "Fog.hlsli"

cbuffer FrameConstants : register(b0)
{
    float3 cameraPos;
    float  coverage;
    float3 sunDir;
    float  turbidity;
    float3 sunColor;
    float  cloudTime;
    float3 moonDir;
    float  windSpeed;
    float3 moonColor;
    float  rain;
    float2 windDir;
    float  sunElevation;
    float  exposure;
    float3 cameraRight;
    float  tanHalfFovX;
    float3 cameraUp;
    float  tanHalfFovY;
    float3 cameraLook;
    float  fogDensity;
    float3 fogColor;
    float  heightFogDensity;
    float3 lightColor;
    float  heightFogFalloff;
    float3 ambientColor;
    float  heightFogHeight;
    float  volumetricFogDensity;
    float  volumetricHeight;
    float  waterLevel;
    float  fogScale;
    float3 fogLightDir;
    float  _fogPad2;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float2 clipXY   : TEXCOORD0;
};

PSInput VSCommon(uint id, float clipZ)
{
    float2 pos = float2((id << 1) & 2, id & 2) * 2.0f - 1.0f;
    // Standard fullscreen triangle: (-1,-1), (-1,3), (3,-1) via vertex id 0,1,2
    // The bit trick above gives (-1,-1), (3,-1), (-1,3) — also covers the screen.
    PSInput o;
    o.position = float4(pos, clipZ, 1.0f);
    o.clipXY   = pos;
    return o;
}

PSInput VSMain(uint id : SV_VertexID)
{
    return VSCommon(id, 0.0f);
}

PSInput VSMainDeferred(uint id : SV_VertexID)
{
    return VSCommon(id, 0.0f);
}

#include "SkyEval.hlsli"

float4 PSMain(PSInput input) : SV_TARGET
{
    // Reconstruct the world ray from the camera basis (no matrix inverse).
    float3 dir = normalize(
        cameraLook
        + cameraRight * (input.clipXY.x * tanHalfFovX)
        + cameraUp * (input.clipXY.y * tanHalfFovY));
    float3 sky = EvaluateSky(dir);
    if (fogScale > 0.0f)
    {
        FogParams fp;
        fp.cameraPos            = cameraPos;
        fp.lightDir             = fogLightDir;
        fp.lightColor           = lightColor;
        fp.ambientColor         = ambientColor;
        fp.fogColor             = fogColor;
        fp.fogDensity           = fogDensity * fogScale;
        fp.heightFogDensity     = heightFogDensity * fogScale;
        fp.heightFogFalloff     = heightFogFalloff;
        fp.heightFogHeight      = heightFogHeight;
        fp.volumetricFogDensity = volumetricFogDensity * fogScale;
        fp.waterLevel           = waterLevel;
        fp.volumetricHeight     = volumetricHeight;
        fp.heightOrigin         = 0.0.xx;
        fp.heightCellSize       = 0.0f;
        fp.heightWorldSize      = 1.0.xx;
        float3 farPos = cameraPos + dir * 280.0f;
        FogResult fog = FogIntegrateNoHeight(cameraPos, farPos, fp, 1.0f);
        sky = ApplyLitFog(sky, fog);
    }
    return float4(encodeSceneRgb(sky), 1.0f);
}
