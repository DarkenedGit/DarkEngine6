// Generic material aux mask. Same register as today's gUv (space 1, t0) so the G-buffer table
// does not grow. Alpha 0 is the missing-map default (a 1x1 of zero). An authored texel,
// including 0, has alpha 1.

#ifdef DE_GBUFFER_SURFACE_TAG
Texture2D gAux : register(t0, space1);

float materialAuxAt(float2 uv)
{
    float4 texel = gAux.Sample(gSamp, uv);
    return texel.a > 0.5f ? saturate(texel.r) : 0.0f;
}
#endif
