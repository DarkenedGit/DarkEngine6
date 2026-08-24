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
    // Match the basis the main view uses (Walk/Pitch leave it dirty until this).
    camera.UpdateViewMatrix();
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

    // Sit the light camera far enough that every frustum corner and scene
    // caster stays in front of it. A fixed 400m eye with a 256m terrain +
    // large margin pushed minZ near (or behind) the eye and flattened a 1m
    // cube into less than one NDC bias unit.
    float pullBack = 16.0f;
    auto considerTowardLight = [&](const Vector3f& p)
    {
        pullBack = Max(pullBack, (p - center).Dot(lightDir) + 8.0f);
    };
    for (int i = 0; i < 8; ++i)
        considerTowardLight(corners[i]);
    if (sceneBounds.IsValid())
    {
        Vector3f sc[8];
        sceneBounds.GetCorners(sc);
        for (int i = 0; i < 8; ++i)
            considerTowardLight(sc[i]);
    }
    pullBack += Max(casterMargin, 0.0f);

    const Vector3f eye  = center + lightDir * pullBack;
    const Matrix4f view = Matrix4f::LookAtLHMatrix(eye, center, up);

    // World-space sphere of the slice. Light-space AABBs elongate at a
    // grazing sun until they swallow the whole terrain, so the F8 preview
    // (and the crop window) look world-locked as the camera moves.
    float radius = 1.0f;
    for (int i = 0; i < 8; ++i)
    {
        const Vector3f d = corners[i] - center;
        radius = Max(radius, sqrtf(d.MagnitudeSqrd()));
    }

    float minZ =  1.0e20f;
    float maxZ = -1.0e20f;
    for (int i = 0; i < 8; ++i)
    {
        const Vector4f lp = view * Vector4f(corners[i].x, corners[i].y, corners[i].z, 1.0f);
        minZ = Min(minZ, lp.z);
        maxZ = Max(maxZ, lp.z);
    }

    // Pull the near plane back for casters closer to the light than the
    // slice. Expanding maxZ to the whole scene AABB only burns depth
    // precision — those points cannot shadow the slice.
    if (sceneBounds.IsValid())
    {
        Vector3f sc[8];
        sceneBounds.GetCorners(sc);
        for (int i = 0; i < 8; ++i)
        {
            const Vector4f lp = view * Vector4f(sc[i].x, sc[i].y, sc[i].z, 1.0f);
            minZ = Min(minZ, lp.z);
        }
    }
    minZ = Max(minZ - 1.0f, 0.05f);

    const Vector4f lightCenter = view * Vector4f(center.x, center.y, center.z, 1.0f);
    float          cx          = lightCenter.x;
    float          cy          = lightCenter.y;

    const float map = (mapSize > 0) ? static_cast<float>(mapSize) : 1024.0f;
    const float texel = (2.0f * radius) / map;
    cx = floorf(cx / texel) * texel;
    cy = floorf(cy / texel) * texel;
    radius += texel;

    maxZ += 1.0f;
    if (maxZ <= minZ)
        maxZ = minZ + 1.0f;

    const Matrix4f proj = Matrix4f::OrthographicOffCenterLHMatrix(
        cx - radius, cx + radius, cy - radius, cy + radius, minZ, maxZ);
    out.viewProj = view * proj;
    out.zRange   = maxZ - minZ;
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

    out.params[0] = Max(depthBias, 0.0f);
    out.params[1] = Clamp(strength, 0.0f, 1.0f);
    out.params[2] = static_cast<float>(cascadeCount);
    out.params[3] = sliceOffset;

    out.cameraLook[0] = cameraLook.x;
    out.cameraLook[1] = cameraLook.y;
    out.cameraLook[2] = cameraLook.z;

    for (int i = 0; i < cascadeCount; ++i)
        out.cascadeInvZ[i] = 1.0f / Max(cascades[i].zRange, 1.0f);
    for (int i = cascadeCount; i < kMaxShadowCascades; ++i)
        out.cascadeInvZ[i] = out.cascadeInvZ[Max(cascadeCount - 1, 0)];
    out.cascadeInvZ[3] = 0.0f;
}

} // namespace Dark
