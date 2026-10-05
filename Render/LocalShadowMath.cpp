#include "Render/LocalShadowMath.h"

#include "Core/Log.h"
#include "Math/MathDefines.h"
#include "Math/MathHelper.h"

#include <cmath>

namespace Dark
{

using namespace Math;

namespace
{

struct FaceAxis
{
    Vector3f axis;
    Vector3f up;
};

// D3D cubemap order. +Y / -Y ups are the cubemap convention.
const FaceAxis kPointFaces[kLocalShadowPointFaces] = {
    { Vector3f(1.0f, 0.0f, 0.0f), Vector3f(0.0f, 1.0f, 0.0f) },
    { Vector3f(-1.0f, 0.0f, 0.0f), Vector3f(0.0f, 1.0f, 0.0f) },
    { Vector3f(0.0f, 1.0f, 0.0f), Vector3f(0.0f, 0.0f, -1.0f) },
    { Vector3f(0.0f, -1.0f, 0.0f), Vector3f(0.0f, 0.0f, 1.0f) },
    { Vector3f(0.0f, 0.0f, 1.0f), Vector3f(0.0f, 1.0f, 0.0f) },
    { Vector3f(0.0f, 0.0f, -1.0f), Vector3f(0.0f, 1.0f, 0.0f) },
};

Vector3f finiteDir(const Vector3f& dir)
{
    if (!std::isfinite(dir.x) || !std::isfinite(dir.y) || !std::isfinite(dir.z) || dir.MagnitudeSqrd() <= 1.0e-8f)
        return Vector3f(Vector3f::Z_AXIS);
    Vector3f n = dir;
    n.Normalize();
    return n;
}

} // namespace

Matrix4f buildSpotShadowViewProj(const Vector3f& pos, const Vector3f& dirTowardBase, float range, float outerConeDeg, float zn)
{
    const Vector3f dir = finiteDir(dirTowardBase);
    // Same up test as makeSpotVolumeWorld. A near-vertical axis cannot use world Y.
    const Vector3f up = (std::fabs(dir.y) > 0.9f) ? Vector3f(Vector3f::X_AXIS) : Vector3f(Vector3f::Y_AXIS);
    float          outer = DegreesToRadians(outerConeDeg);
    const float    kMaxOuter = HalfPi - 0.01f;
    if (outer > kMaxOuter)
        outer = kMaxOuter;
    if (outer < 0.0f)
        outer = 0.0f;
    float zf = range;
    if (!(zf > zn))
        zf = zn;
    const Matrix4f view = Matrix4f::LookAtLHMatrix(pos, pos + dir, up);
    const Matrix4f proj = Matrix4f::PerspectiveFovLHReverseMatrix(outer * 2.0f, 1.0f, zn, zf);
    return view * proj;
}

Matrix4f buildPointFaceViewProj(const Vector3f& pos, int face, float range, float zn)
{
    if (face < 0 || face >= kLocalShadowPointFaces)
    {
        DE_ASSERT(false);
        face = 0;
    }
    float zf = range;
    if (!(zf > zn))
        zf = zn;
    const FaceAxis& axis = kPointFaces[face];
    const Matrix4f  view = Matrix4f::LookAtLHMatrix(pos, pos + axis.axis, axis.up);
    const Matrix4f  proj = Matrix4f::PerspectiveFovLHReverseMatrix(HalfPi, 1.0f, zn, zf);
    return view * proj;
}

int selectPointShadowFace(const Vector3f& fromLight)
{
    const float ax = std::fabs(fromLight.x);
    const float ay = std::fabs(fromLight.y);
    const float az = std::fabs(fromLight.z);
    if (ax >= ay && ax >= az)
        return fromLight.x >= 0.0f ? 0 : 1;
    if (ay >= az)
        return fromLight.y >= 0.0f ? 2 : 3;
    return fromLight.z >= 0.0f ? 4 : 5;
}

float localShadowNdcBias(float depthBiasM, float zn, float zf, float viewZ, float mapSize)
{
    const float z   = Max(viewZ, zn);
    const float m43 = zn * zf / Max(zf - zn, 1.0e-4f);
    const float dz  = depthBiasM * m43 / Max(z * z, 1.0e-4f);
    const float cap = 4.0f / Max(mapSize, 1.0f);
    return Min(dz, cap);
}

} // namespace Dark
