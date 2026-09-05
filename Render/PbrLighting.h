#pragma once

#include "Math/MathDefines.h"
#include "Math/MathHelper.h"
#include "Math/Vector3f.h"

#include <cmath>

namespace Dark
{

    // Filament / Frostbite V form. V_SmithGGXCorrelated ALREADY includes 1/(4 NdotV NdotL). Do NOT divide again.
    // Diffuse is albedo*(1-metallic)*NdotL with no 1/π (engine units).

    inline float pbrSaturate(float x)
    {
        return Math::Clamp(x, 0.0f, 1.0f);
    }

    inline float ndfGgx(float NdotH, float a)
    {
        const float a2 = a * a;
        const float d  = (NdotH * a2 - NdotH) * NdotH + 1.0f;
        return a2 / (Math::Pi * std::fmax(d * d, 1.0e-7f));
    }

    // Visibility G/(4 NdotV NdotL). Do NOT divide by 4 NdotV NdotL again.
    inline float vSmithGgxCorrelated(float NdotV, float NdotL, float a)
    {
        const float a2   = a * a;
        const float ggxV = NdotL * std::sqrt(NdotV * NdotV * (1.0f - a2) + a2);
        const float ggxL = NdotV * std::sqrt(NdotL * NdotL * (1.0f - a2) + a2);
        return 0.5f / std::fmax(ggxV + ggxL, 1.0e-5f);
    }

    inline Math::Vector3f fSchlick(const Math::Vector3f& F0, float VdotH)
    {
        const float f = std::pow(pbrSaturate(1.0f - VdotH), 5.0f);
        return F0 + (Math::Vector3f(1.0f, 1.0f, 1.0f) - F0) * f;
    }

    inline Math::Vector3f pbrEvaluate(Math::Vector3f n, Math::Vector3f v, Math::Vector3f l, const Math::Vector3f& albedo, float roughness, float metallic, const Math::Vector3f& lightColor)
    {
        n.Normalize();
        v.Normalize();
        l.Normalize();

        const float NdotL = pbrSaturate(n.Dot(l));
        if (NdotL <= 0.0f)
            return Math::Vector3f(0.0f, 0.0f, 0.0f);

        const float NdotV = std::fmax(pbrSaturate(n.Dot(v)), 1.0e-4f);
        Math::Vector3f h  = v + l;
        h.Normalize();
        const float NdotH = pbrSaturate(n.Dot(h));
        const float VdotH = pbrSaturate(v.Dot(h));

        const float            a          = roughness * roughness;
        const Math::Vector3f   F0         = Math::Vector3f(0.04f, 0.04f, 0.04f) + (albedo - Math::Vector3f(0.04f, 0.04f, 0.04f)) * metallic;
        const Math::Vector3f   diffuseCol = albedo * (1.0f - metallic);
        const float            D          = ndfGgx(NdotH, a);
        const float            Vvis       = vSmithGgxCorrelated(NdotV, NdotL, a);
        const Math::Vector3f   F          = fSchlick(F0, VdotH);
        const Math::Vector3f   Fr         = F * (D * Vvis);
        const Math::Vector3f   Fd         = diffuseCol;
        const Math::Vector3f   one(1.0f, 1.0f, 1.0f);
        return ((Fd * (one - F) + Fr) * NdotL) * lightColor;
    }

    inline Math::Vector3f pbrDirectional(const Math::Vector3f& n, const Math::Vector3f& v, const Math::Vector3f& albedo, float roughness, float metallic, const Math::Vector3f& lightDir,
                                         const Math::Vector3f& lightColor)
    {
        return pbrEvaluate(n, v, lightDir, albedo, roughness, metallic, lightColor);
    }

    inline float windowedDistanceAttenuation(float d2, float invRange2)
    {
        float s = pbrSaturate(1.0f - d2 * invRange2);
        s *= s;
        return s / std::fmax(d2, 1.0e-4f);
    }

    inline float spotAngleAttenuation(float cosTheta, float innerCos, float outerCos)
    {
        const float inv = 1.0f / std::fmax(innerCos - outerCos, 1.0e-4f);
        const float t   = pbrSaturate((cosTheta - outerCos) * inv);
        return t * t;
    }

    // l = normalize(lightPos - worldPos); cosTheta = dot(-l, spotDirTowardBase)
    inline float spotCosTheta(const Math::Vector3f& lightPos, const Math::Vector3f& worldPos, const Math::Vector3f& spotDirTowardBase)
    {
        Math::Vector3f l = lightPos - worldPos;
        l.Normalize();
        return (-l).Dot(spotDirTowardBase);
    }

    // L is surface-to-light (lightPos - worldPos). sourceRadius==0 keeps L unchanged.
    inline Math::Vector3f applySourceRadius(const Math::Vector3f& L, Math::Vector3f n, Math::Vector3f v, float sourceRadius)
    {
        if (sourceRadius <= 0.0f)
            return L;
        n.Normalize();
        v.Normalize();
        const Math::Vector3f incident    = v * -1.0f;
        const Math::Vector3f r           = incident - n * (2.0f * n.Dot(incident));
        const Math::Vector3f centerToRay = L - r * L.Dot(r);
        const float          len         = std::fmax(centerToRay.Magnitude(), 1.0e-4f);
        return L - centerToRay * pbrSaturate(sourceRadius / len);
    }

} // namespace Dark
