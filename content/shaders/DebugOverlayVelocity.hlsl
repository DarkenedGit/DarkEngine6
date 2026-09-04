#include "DebugOverlay.hlsli"

Texture2D gVelocity : register(t0);

// Signed UV velocity → visible color. contrast scales how much motion fills the range.
// R = +X, G = +Y, B = magnitude. Mid-gray is no motion.
float4 PSMain(PSInput input) : SV_TARGET
{
    float2 v   = gVelocity.SampleLevel(gSamp, input.uv, 0).rg;
    float  s   = max(contrast, 0.01f);
    float2 n   = saturate(v * s * 0.5f + 0.5f);
    float  mag = saturate(length(v) * s);
    return float4(n.x, n.y, mag, 1.0f);
}
