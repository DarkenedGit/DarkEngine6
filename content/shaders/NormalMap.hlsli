#ifndef DE_NORMALMAP_HLSLI
#define DE_NORMALMAP_HLSLI

// Expects gNormal t1, gSamp s0, and cbuffer normalScale in the including shader.
float3 ApplyNormalMap(float3 nW, float4 tangentWS, float2 uv)
{
    nW = normalize(nW);
    if (length(tangentWS.xyz) < 1e-6f)
        return nW;
    float3 tW = normalize(tangentWS.xyz);
    tW = normalize(tW - nW * dot(nW, tW));
    float3 bW = cross(nW, tW) * tangentWS.w;
    float3 nt = gNormal.Sample(gSamp, uv).xyz * 2.0f - 1.0f;
    nt.xy *= normalScale;
    nt = normalize(nt);
    return normalize(nt.x * tW + nt.y * bW + nt.z * nW);
}

#endif
