// UV reflectance mask. Register space 1 so t4 stays free for instance buffers.
// Space 1 needs shader model 5.1, so only this variant is compiled as ps_5_1.
// Alpha 0 is the missing-map default (a 1x1 of zero). An authored texel, including 0, has alpha 1.

#include "Ultraviolet.hlsli"

#ifdef DE_ULTRAVIOLET
Texture2D gUv : register(t0, space1);

float uvReflectanceAt(float2 uv, float metallic)
{
    float4 texel = gUv.Sample(gSamp, uv);
    float fallback = lerp(DE_UV_CLASS, kUvMetal, saturate(metallic));
    return saturate(texel.a > 0.5f ? texel.r : fallback);
}
#endif
