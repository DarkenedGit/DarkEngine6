#include "DebugOverlay.hlsli"

Texture2DArray gDepthArray : register(t0);

float4 PSMain(PSInput input) : SV_TARGET
{
    float d = gDepthArray.SampleLevel(gSamp, float3(input.uv, round(slice)), 0).r;
    float v = visDepth(d);
    return float4(v, v, v, 1.0f);
}
