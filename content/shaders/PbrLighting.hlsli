#ifndef DE_PBR_LIGHTING_HLSLI
#define DE_PBR_LIGHTING_HLSLI

// Filament / Frostbite V form. V_SmithGGXCorrelated ALREADY includes 1/(4 NdotV NdotL). Do NOT divide again.
// Diffuse is albedo*(1-metallic)*NdotL with no 1/π (engine units).

#ifndef DE_PBR_PI
#define DE_PBR_PI 3.14159265f
#endif

float NDF_GGX(float NdotH, float a)
{
    float a2 = a * a;
    float d  = (NdotH * a2 - NdotH) * NdotH + 1.0f;
    return a2 / (DE_PBR_PI * max(d * d, 1e-7f));
}

float V_SmithGGXCorrelated(float NdotV, float NdotL, float a)
{
    // Visibility G/(4 NdotV NdotL). Do NOT divide by 4 NdotV NdotL again.
    float a2   = a * a;
    float ggxV = NdotL * sqrt(NdotV * NdotV * (1.0f - a2) + a2);
    float ggxL = NdotV * sqrt(NdotL * NdotL * (1.0f - a2) + a2);
    return 0.5f / max(ggxV + ggxL, 1e-5f);
}

float3 F_Schlick(float3 F0, float VdotH)
{
    float f = pow(saturate(1.0f - VdotH), 5.0f);
    return F0 + (1.0f - F0) * f;
}

float3 PbrEvaluate(float3 n, float3 v, float3 l, float3 albedo, float roughness, float metallic, float3 lightColor)
{
    n = normalize(n);
    v = normalize(v);
    l = normalize(l);

    float NdotL = saturate(dot(n, l));
    float NdotV = max(saturate(dot(n, v)), 1e-4f);
    float3 h    = normalize(v + l);
    float NdotH = saturate(dot(n, h));
    float VdotH = saturate(dot(v, h));

    float  a          = roughness * roughness;
    float3 F0         = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metallic);
    float3 diffuseCol = albedo * (1.0f - metallic);
    float  D          = NDF_GGX(NdotH, a);
    float  Vvis       = V_SmithGGXCorrelated(NdotV, NdotL, a);
    float3 F          = F_Schlick(F0, VdotH);
    float3 Fr         = D * Vvis * F;
    float3 Fd         = diffuseCol;
    return (Fd * (1.0f - F) + Fr) * NdotL * lightColor;
}

float3 PbrDirectional(float3 n, float3 v, float3 albedo, float roughness, float metallic, float3 lightDir, float3 lightColor)
{
    return PbrEvaluate(n, v, lightDir, albedo, roughness, metallic, lightColor);
}

float windowedDistanceAttenuation(float d2, float invRange2)
{
    float s = saturate(1.0f - d2 * invRange2);
    s *= s;
    return s / max(d2, 1e-4f);
}

float spotAngleAttenuation(float cosTheta, float innerCos, float outerCos)
{
    float inv = 1.0f / max(innerCos - outerCos, 1e-4f);
    float t   = saturate((cosTheta - outerCos) * inv);
    return t * t;
}

// l = normalize(lightPos - worldPos); cosTheta = dot(-l, dir) where dir is spot axis toward base.
float spotCosTheta(float3 lightPos, float3 worldPos, float3 spotDirTowardBase)
{
    float3 l = normalize(lightPos - worldPos);
    return dot(-l, spotDirTowardBase);
}

// L is surface-to-light (lightPos - worldPos). sourceRadius==0 keeps L unchanged.
float3 applySourceRadius(float3 L, float3 n, float3 v, float sourceRadius)
{
    if (sourceRadius <= 0.0f)
        return L;
    float3 r           = reflect(-v, n);
    float3 centerToRay = L - r * dot(L, r);
    float  len         = max(length(centerToRay), 1e-4f);
    return L - centerToRay * saturate(sourceRadius / len);
}

#endif
