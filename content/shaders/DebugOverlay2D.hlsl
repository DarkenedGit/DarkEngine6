#include "DebugOverlay.hlsli"

Texture2D gDepth2D : register(t0);

float4 PSMain(PSInput input) : SV_TARGET
{
    float d = gDepth2D.SampleLevel(gSamp, input.uv, 0).r;
    float v = visDepth(d);
    return float4(v, v, v, 1.0f);
}
