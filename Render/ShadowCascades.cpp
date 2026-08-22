#include "Render/ShadowCascades.h"
#include "Render/Camera3D.h"
#include "Math/MathHelper.h"
#include "Math/Vector4f.h"

#include <cmath>
#include <cstring>

namespace Dark
{

using namespace Math;

void computePracticalSplits(float nearZ, float farZ, int cascadeCount, float lambda, float outSplits[kMaxShadowCascades])
{
    if (cascadeCount < 1)
        cascadeCount = 1;
    if (cascadeCount > kMaxShadowCascades)
        cascadeCount = kMaxShadowCascades;
    if (nearZ < 0.01f)
        nearZ = 0.01f;
    if (farZ <= nearZ)
        farZ = nearZ + 1.0f;
    lambda = Clamp(lambda, 0.0f, 1.0f);

    for (int i = 0; i < cascadeCount; ++i)
    {
        const float p   = static_cast<float>(i + 1) / static_cast<float>(cascadeCount);
        const float log = nearZ * powf(farZ / nearZ, p);
        const float uni = nearZ + (farZ - nearZ) * p;
        outSplits[i]    = Lerp(uni, log, lambda);
    }
    for (int i = cascadeCount; i < kMaxShadowCascades; ++i)
        outSplits[i] = farZ;
}

void extractFrustumCorners(const Camera3D& camera, float nearZ, float farZ, Vector3f outCorners[8])
{
    const Vector3f pos   = camera.GetPosition();
    const Vector3f look  = camera.GetLook();
    const Vector3f right = camera.GetRight();
    const Vector3f up    = camera.GetUp();
    const float    tanY  = tanf(0.5f * camera.GetFovY());
    const float    tanX  = tanY * camera.GetAspect();

    int n = 0;
    const float zs[2] = { nearZ, farZ };
    const float xs[2] = { -1.0f, 1.0f };
    const float ys[2] = { -1.0f, 1.0f };
    for (int iz = 0; iz < 2; ++iz)
    {
        const float z = zs[iz];
        for (int iy = 0; iy < 2; ++iy)
        {
            for (int ix = 0; ix < 2; ++ix)
            {
                outCorners[n++] = pos
                    + look * z
                    + right * (xs[ix] * z * tanX)
                    + up * (ys[iy] * z * tanY);
            }
        }
    }
}

bool buildCascadeMatrix(
    const Vector3f corners[8],
    const Vector3f& lightDirToward,
    const Aabb3f& sceneBounds,
    float casterMargin,
    uint32_t mapSize,
    CascadeData& out)
{
    Vector3f lightDir = lightDirToward;
    if (lightDir.MagnitudeSqrd() < 1.0e-8f)
        return false;
    lightDir.Normalize();

    Vector3f up(0.0f, 1.0f, 0.0f);
    if (fabsf(lightDir.Dot(up)) > 0.95f)
        up = Vector3f(0.0f, 0.0f, 1.0f);

    Vector3f center(0.0f, 0.0f, 0.0f);
    for (int i = 0; i < 8; ++i)
        center += corners[i];
    center *= (1.0f / 8.0f);

    const float eyeDist = 400.0f;
    const Vector3f eye  = center + lightDir * eyeDist;
    const Matrix4f view = Matrix4f::LookAtLHMatrix(eye, center, up);

    float minX =  1.0e20f, minY =  1.0e20f, minZ =  1.0e20f;
    float maxX = -1.0e20f, maxY = -1.0e20f, maxZ = -1.0e20f;
    for (int i = 0; i < 8; ++i)
    {
        const Vector4f lp = view * Vector4f(corners[i].x, corners[i].y, corners[i].z, 1.0f);
        minX = Min(minX, lp.x);
        minY = Min(minY, lp.y);
        minZ = Min(minZ, lp.z);
        maxX = Max(maxX, lp.x);
        maxY = Max(maxY, lp.y);
        maxZ = Max(maxZ, lp.z);
    }

    if (sceneBounds.IsValid())
    {
        Vector3f sc[8];
        sceneBounds.GetCorners(sc);
        for (int i = 0; i < 8; ++i)
        {
            const Vector4f lp = view * Vector4f(sc[i].x, sc[i].y, sc[i].z, 1.0f);
            minZ = Min(minZ, lp.z);
            maxZ = Max(maxZ, lp.z);
        }
    }
    minZ -= casterMargin;

    // Square the XY extent so camera yaw doesn't change resolution.
    const float halfW = 0.5f * (maxX - minX);
    const float halfH = 0.5f * (maxY - minY);
    float       radius = Max(halfW, halfH);
    if (radius < 1.0f)
        radius = 1.0f;
    float cx = 0.5f * (minX + maxX);
    float cy = 0.5f * (minY + maxY);

    const float map = (mapSize > 0) ? static_cast<float>(mapSize) : 1024.0f;
    const float texel = (2.0f * radius) / map;
    cx = floorf(cx / texel) * texel;
    cy = floorf(cy / texel) * texel;

    if (maxZ <= minZ)
        maxZ = minZ + 1.0f;

    const Matrix4f proj = Matrix4f::OrthographicOffCenterLHMatrix(
        cx - radius, cx + radius, cy - radius, cy + radius, minZ, maxZ);
    out.viewProj = view * proj;
    return true;
}

void packShadowConstants(
    ShadowConstants& out,
    const CascadeData cascades[],
    int cascadeCount,
    uint32_t mapSize,
    float depthBias,
    float strength,
    const Vector3f& cameraLook,
    float sliceOffset)
{
    std::memset(&out, 0, sizeof(out));
    if (cascadeCount < 1)
        cascadeCount = 1;
    if (cascadeCount > kMaxShadowCascades)
        cascadeCount = kMaxShadowCascades;

    for (int i = 0; i < cascadeCount; ++i)
        std::memcpy(out.cascadeViewProj[i], cascades[i].viewProj.m_afEntry, sizeof(float) * 16);

    out.cascadeSplits[0] = cascades[0].splitFar;
    out.cascadeSplits[1] = cascades[Min(1, cascadeCount - 1)].splitFar;
    out.cascadeSplits[2] = cascades[Min(2, cascadeCount - 1)].splitFar;
    out.cascadeSplits[3] = static_cast<float>(mapSize);

    out.params[0] = depthBias;
    out.params[1] = Clamp(strength, 0.0f, 1.0f);
    out.params[2] = static_cast<float>(cascadeCount);
    out.params[3] = sliceOffset;

    out.cameraLook[0] = cameraLook.x;
    out.cameraLook[1] = cameraLook.y;
    out.cameraLook[2] = cameraLook.z;
}

} // namespace Dark
