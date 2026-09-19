#include "Terrain/HeightBlend.h"
#include "Math/MathHelper.h"
#include "Math/Vector2f.h"
#include "Render/Octahedral.h"

namespace Dark
{
namespace Terrain
{

using namespace Math;

namespace
{

void renormalizeSplat(const float* splat, float outW[kMaxTerrainLayers])
{
    float sum = 0.0f;
    for (int i = 0; i < kMaxTerrainLayers; ++i)
    {
        const float w = splat ? splat[i] : 0.0f;
        outW[i] = w > 0.0f ? w : 0.0f;
        sum += outW[i];
    }
    if (sum > 1.0e-5f)
    {
        const float inv = 1.0f / sum;
        for (int i = 0; i < kMaxTerrainLayers; ++i)
            outW[i] *= inv;
    }
}

} // namespace

bool heightBlendWeights(const float height[kMaxTerrainLayers], const float splat[kMaxTerrainLayers], float k, float t, float outW[kMaxTerrainLayers])
{
    if (!outW)
        return false;

    float w[kMaxTerrainLayers];
    renormalizeSplat(splat, w);

    // Raw formula at k=0 with dummy H=0.5 is a 4-way mix that erases splat.
    if (k <= 0.0f)
    {
        for (int i = 0; i < kMaxTerrainLayers; ++i)
            outW[i] = w[i];
        return true;
    }

    if (t < 1.0e-3f)
        t = 1.0e-3f;

    float h[kMaxTerrainLayers];
    float hMax = -Infinity;
    for (int i = 0; i < kMaxTerrainLayers; ++i)
    {
        const float Hi = height ? height[i] : 0.0f;
        h[i] = Hi + k * w[i];
        if (h[i] > hMax)
            hMax = h[i];
    }

    float hat[kMaxTerrainLayers];
    float hatSum = 0.0f;
    const float threshold = hMax - t;
    for (int i = 0; i < kMaxTerrainLayers; ++i)
    {
        hat[i] = Clamp((h[i] - threshold) / t, 0.0f, 1.0f);
        hatSum += hat[i];
    }

    if (hatSum <= 1.0e-5f)
    {
        for (int i = 0; i < kMaxTerrainLayers; ++i)
            outW[i] = w[i];
        return true;
    }

    const float inv = 1.0f / hatSum;
    for (int i = 0; i < kMaxTerrainLayers; ++i)
        outW[i] = hat[i] * inv;
    return true;
}

Vector3f whiteoutBlend(const Vector3f n[kMaxTerrainLayers], const float w[kMaxTerrainLayers])
{
    Vector3f acc(0.0f, 0.0f, 1.0f);
    if (!n || !w)
        return acc;

    for (int i = 0; i < kMaxTerrainLayers; ++i)
    {
        acc.x += n[i].x * w[i];
        acc.y += n[i].y * w[i];
        acc.z *= Lerp(1.0f, n[i].z, w[i]);
    }
    acc.Normalize();
    return acc;
}

Vector3f lerpRgbNormals(const Vector3f n[kMaxTerrainLayers], const float w[kMaxTerrainLayers])
{
    if (!n || !w)
        return Vector3f(0.0f, 0.0f, 1.0f);

    Vector3f acc(0.0f, 0.0f, 0.0f);
    for (int i = 0; i < kMaxTerrainLayers; ++i)
    {
        acc.x += n[i].x * w[i];
        acc.y += n[i].y * w[i];
        acc.z += n[i].z * w[i];
    }
    acc.Normalize();
    return acc;
}

Vector3f lerpRgbNormal(const Vector3f n[kMaxTerrainLayers], const float w[kMaxTerrainLayers])
{
    return lerpRgbNormals(n, w);
}

Vector3f whiteoutNormal(const Vector3f& geomN, const Vector3f& tangentN)
{
    Vector3f nG = geomN;
    nG.Normalize();

    // cross(nG, +Z) is +X on nG=+Y; cross(+Z, nG) would flip U.
    Vector3f tW = nG.Cross(Vector3f(0.0f, 0.0f, 1.0f));
    if (tW.MagnitudeSqrd() < 1.0e-6f)
        tW = Vector3f(1.0f, 0.0f, 0.0f);
    tW.Normalize();
    const Vector3f bW = tW.Cross(nG);

    Vector3f nT = tangentN;
    nT.Normalize();
    Vector3f nWorld = tW * nT.x + bW * nT.y + nG * nT.z;
    nWorld.Normalize();
    return nWorld;
}

Vector4f packTerrainAttrib(const Vector3f& nWorld, float roughness, float metallic)
{
    const Vector2f oct = encodeOct(nWorld);
    return Vector4f(oct.x, oct.y, roughness, metallic);
}

} // namespace Terrain
} // namespace Dark
