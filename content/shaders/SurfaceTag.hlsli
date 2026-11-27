#ifndef DE_SURFACE_TAG_HLSLI
#define DE_SURFACE_TAG_HLSLI

// Generic SurfaceTag MRT (render-graph plan §3.4.2). R is tag/255, G is the material aux mask.
// RT0.a stays emissive; this is an extra target, not a rewrite of albedo.a.

#ifndef DE_SURFACE_CATEGORY
#define DE_SURFACE_CATEGORY 0
#endif

#ifdef DE_GBUFFER_SURFACE_TAG
float2 packSurfaceTag(uint tag, float aux)
{
    return float2(saturate((float)(tag & 255) * (1.0f / 255.0f)), saturate(aux));
}

uint resolveSurfaceTag(uint packed)
{
    // Bit 8 means "use bits 0–7"; otherwise the pipeline's default category.
    if ((packed & 256u) != 0u)
        return packed & 255u;
    return (uint)DE_SURFACE_CATEGORY;
}
#endif

#endif
