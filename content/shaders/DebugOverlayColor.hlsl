#include "DebugOverlay.hlsli"

Texture2D gColor : register(t0);

float4 PSMain(PSInput input) : SV_TARGET
{
    return float4(gColor.SampleLevel(gSamp, input.uv, 0).rgb, 1.0f);
}
