#include <gtest/gtest.h>

#include "Math/MathDefines.h"
#include "Math/Matrix4f.h"
#include "Math/Vector3f.h"
#include "Math/Vector4f.h"
#include "Particles/ParticleDepthCollision.h"
#include "Particles/ParticleTypes.h"
#include "Render/Camera3D.h"

#include <cmath>
#include <cstdint>

using Dark::Camera3D;
using Dark::ParticleBirthSampleResult;
using Dark::ParticleDepthHitResult;
using Dark::ParticleEmitterDesc;
using Dark::ParticleProjectUvResult;
using Dark::ParticleReflectResult;
using Dark::particleBirthSample;
using Dark::particleDepthHit;
using Dark::particleDepthNormal;
using Dark::particleProjectUv;
using Dark::particleReconstructWorld;
using Dark::particleReflectVelocity;
using Dark::particleResolveViewZ;
using Dark::particleSurfaceViewZ;
using Dark::Math::DegToRad;
using Dark::Math::Matrix4f;
using Dark::Math::Pi;
using Dark::Math::Vector3f;
using Dark::Math::Vector4f;

namespace
{

    float lengthOf(const Vector3f& v)
    {
        return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    }

    Vector3f manualReconstruct(float ndcX, float ndcY, float depth, const Matrix4f& invViewProj)
    {
        const float    clamped = depth > 1.0e-7f ? depth : 1.0e-7f;
        const Vector4f w       = invViewProj * Vector4f(ndcX, ndcY, clamped, 1.0f);
        const float    denom   = w.w > 1.0e-6f ? w.w : 1.0e-6f;
        const float    invW    = 1.0f / denom;
        return Vector3f(w.x * invW, w.y * invW, w.z * invW);
    }

    void expectNearVec(const Vector3f& a, const Vector3f& b, float eps)
    {
        EXPECT_NEAR(a.x, b.x, eps);
        EXPECT_NEAR(a.y, b.y, eps);
        EXPECT_NEAR(a.z, b.z, eps);
    }

} // namespace

TEST(ParticleDepthCollision, SkyDepth_ZeroAndNegativeZero_Miss)
{
    const float negZero = std::copysign(0.0f, -1.0f);
    EXPECT_TRUE(std::signbit(negZero));
    EXPECT_FALSE(particleSurfaceViewZ(0.0f, 0.05f).valid);
    EXPECT_FALSE(particleSurfaceViewZ(negZero, 0.05f).valid);
}

TEST(ParticleDepthCollision, SurfaceViewZ_TinyDepth_NotSky)
{
    const auto surface = particleSurfaceViewZ(1.0e-8f, 0.05f);
    EXPECT_TRUE(surface.valid);
    EXPECT_FLOAT_EQ(surface.viewZ, 0.05f / 1.0e-7f);

    const auto nearDepth = particleSurfaceViewZ(1.0f, 0.05f);
    EXPECT_TRUE(nearDepth.valid);
    EXPECT_FLOAT_EQ(nearDepth.viewZ, 0.05f);
}

TEST(ParticleDepthCollision, DepthHit_CloserThanBias_Miss)
{
    const ParticleDepthHitResult hit = particleDepthHit(4.8f, 5.0f, 0.1f, 0.04f, 0.35f, 0.0f, 1.0f);
    EXPECT_FALSE(hit.hit);
    EXPECT_LT(hit.delta, -0.04f);
}

TEST(ParticleDepthCollision, DepthHit_InsideShell_Hit)
{
    const ParticleDepthHitResult onBias = particleDepthHit(4.96f, 5.0f, 0.1f, 0.04f, 0.35f, 0.0f, 1.0f);
    EXPECT_TRUE(onBias.hit);
    EXPECT_NEAR(onBias.delta, -0.04f, 1.0e-5f);
    EXPECT_FLOAT_EQ(onBias.shell, 0.35f);

    const ParticleDepthHitResult padded = particleDepthHit(5.45f, 5.0f, 0.1f, 0.04f, 0.35f, 0.5f, 1.0f);
    EXPECT_TRUE(padded.hit);
    EXPECT_FLOAT_EQ(padded.shell, 0.5f);
    EXPECT_NEAR(padded.delta, 0.45f, 1.0e-5f);

    const ParticleDepthHitResult capped = particleDepthHit(5.70f, 5.0f, 0.1f, 0.04f, 0.35f, 2.0f, 1.0f);
    EXPECT_TRUE(capped.hit);
    EXPECT_FLOAT_EQ(capped.shell, 0.75f);
    EXPECT_NEAR(capped.delta, 0.70f, 1.0e-5f);
}

TEST(ParticleDepthCollision, DepthHit_BeyondShell_Miss)
{
    const ParticleDepthHitResult slow = particleDepthHit(5.36f, 5.0f, 0.1f, 0.04f, 0.35f, 0.0f, 1.0f);
    EXPECT_FALSE(slow.hit);
    EXPECT_GT(slow.delta, slow.shell);
    EXPECT_FLOAT_EQ(slow.shell, 0.35f);

    const ParticleDepthHitResult pastCap = particleDepthHit(6.0f, 5.0f, 0.1f, 0.04f, 2.0f, 0.0f, 1.0f);
    EXPECT_FALSE(pastCap.hit);
    EXPECT_FLOAT_EQ(pastCap.shell, 0.75f);
    EXPECT_GT(pastCap.delta, 0.75f);
}

TEST(ParticleDepthCollision, Miss_ViewZAtNear_ClipBehind_UvOutside)
{
    const ParticleDepthHitResult atNear = particleDepthHit(0.1f, 0.1f, 0.1f, 0.04f, 0.35f, 0.0f, 1.0f);
    EXPECT_FALSE(atNear.hit);
    EXPECT_FLOAT_EQ(atNear.delta, 0.0f);

    const ParticleDepthHitResult insideNear = particleDepthHit(0.05f, 0.1f, 0.1f, 0.04f, 0.35f, 0.0f, 1.0f);
    EXPECT_FALSE(insideNear.hit);

    Camera3D cam;
    cam.SetLens(Pi * 0.5f, 1.0f, 0.1f, 100.0f);
    cam.LookAt(Vector3f(0.0f, 0.0f, 0.0f), Vector3f(0.0f, 0.0f, 1.0f), Vector3f(0.0f, 1.0f, 0.0f));
    const Matrix4f vp = cam.GetViewProj();

    const ParticleProjectUvResult behind = particleProjectUv(Vector3f(0.0f, 0.0f, -2.0f), vp);
    EXPECT_FALSE(behind.inside);
    EXPECT_LE(behind.clipW, 0.0f);

    const ParticleProjectUvResult offscreen = particleProjectUv(Vector3f(0.0f, 10.0f, 1.0f), vp);
    EXPECT_FALSE(offscreen.inside);
    EXPECT_GT(offscreen.clipW, 0.0f);
    EXPECT_TRUE(offscreen.v < 0.0f || offscreen.v >= 1.0f || offscreen.u < 0.0f || offscreen.u >= 1.0f);

    const Matrix4f                id;
    const ParticleProjectUvResult uEdge = particleProjectUv(Vector3f(1.0f, 0.0f, 0.0f), id);
    EXPECT_FALSE(uEdge.inside);
    EXPECT_FLOAT_EQ(uEdge.u, 1.0f);

    const ParticleProjectUvResult uZero = particleProjectUv(Vector3f(-1.0f, 0.0f, 0.0f), id);
    EXPECT_TRUE(uZero.inside);
    EXPECT_FLOAT_EQ(uZero.u, 0.0f);
}

TEST(ParticleDepthCollision, ResolveViewZ_SitsBiasInFront)
{
    EXPECT_FLOAT_EQ(particleResolveViewZ(5.0f, 0.04f), 4.96f);
}

TEST(ParticleDepthCollision, Reflect_OpposingNormal_Restitution045_Friction0)
{
    const ParticleReflectResult reflected = particleReflectVelocity(Vector3f(3.0f, -2.0f, 0.0f), Vector3f(0.0f, 4.0f, 0.0f), 0.45f, 0.0f);
    expectNearVec(reflected.velocity, Vector3f(3.0f, 0.9f, 0.0f), 1.0e-5f);
    EXPECT_NEAR(reflected.velocity.Dot(Vector3f(0.0f, 1.0f, 0.0f)), 0.45f * 2.0f, 1.0e-5f);
}

TEST(ParticleDepthCollision, Reflect_FlippedNormal_SameResult)
{
    const Vector3f              velocity(3.0f, -2.0f, 0.0f);
    const ParticleReflectResult opposing = particleReflectVelocity(velocity, Vector3f(0.0f, 4.0f, 0.0f), 0.45f, 0.0f);
    const ParticleReflectResult flipped  = particleReflectVelocity(velocity, Vector3f(0.0f, -3.0f, 0.0f), 0.45f, 0.0f);
    expectNearVec(flipped.velocity, opposing.velocity, 1.0e-5f);
}

TEST(ParticleDepthCollision, Reflect_Friction1_Restitution0_Zero)
{
    const ParticleReflectResult stopped = particleReflectVelocity(Vector3f(3.0f, -2.0f, 1.0f), Vector3f(0.0f, 1.0f, 0.0f), 0.0f, 1.0f);
    expectNearVec(stopped.velocity, Vector3f(0.0f, 0.0f, 0.0f), 1.0e-5f);

    const ParticleReflectResult saturated = particleReflectVelocity(Vector3f(3.0f, -2.0f, 1.0f), Vector3f(0.0f, 1.0f, 0.0f), -0.2f, 2.0f);
    expectNearVec(saturated.velocity, Vector3f(0.0f, 0.0f, 0.0f), 1.0e-5f);

    const ParticleReflectResult full = particleReflectVelocity(Vector3f(0.0f, -2.0f, 0.0f), Vector3f(0.0f, 1.0f, 0.0f), 2.0f, 0.0f);
    expectNearVec(full.velocity, Vector3f(0.0f, 2.0f, 0.0f), 1.0e-5f);
}

TEST(ParticleDepthCollision, DepthNormal_FlatGround_FacesCamera)
{
    const Vector3f center(0.0f, 0.0f, 0.0f);
    const Vector3f alongX(1.0f, 0.0f, 0.0f);
    const Vector3f alongZ(0.0f, 0.0f, 1.0f);
    const Vector3f normal = particleDepthNormal(center, alongX, alongZ, Vector3f(0.0f, 4.0f, 0.0f));
    expectNearVec(normal, Vector3f(0.0f, 1.0f, 0.0f), 1.0e-5f);

    const Vector3f below = particleDepthNormal(center, alongX, alongZ, Vector3f(0.0f, -4.0f, 0.0f));
    expectNearVec(below, Vector3f(0.0f, -1.0f, 0.0f), 1.0e-5f);
}

TEST(ParticleDepthCollision, ReconstructWorld_OneClampMatchesMultiply)
{
    Camera3D cam;
    cam.SetLens(Pi * 0.25f, 16.0f / 9.0f, 0.18f, 2000.0f);
    cam.LookAt(Vector3f(0.0f, 0.0f, 0.0f), Vector3f(0.0f, 0.0f, 1.0f), Vector3f(0.0f, 1.0f, 0.0f));

    auto roundTrip = [](const Camera3D& camera, const Vector3f& world) {
        const Matrix4f vp   = camera.GetViewProj();
        const Matrix4f inv  = vp.Inverse();
        const Vector4f clip = vp * Vector4f(world, 1.0f);
        EXPECT_GT(clip.w, 0.0f);
        if (!(clip.w > 0.0f))
            return;
        const float ndcX     = clip.x / clip.w;
        const float ndcY     = clip.y / clip.w;
        const float rawDepth = clip.z / clip.w;
        EXPECT_GT(rawDepth, 0.0f);

        const Vector3f rec    = particleReconstructWorld(ndcX, ndcY, rawDepth, inv);
        const Vector3f manual = manualReconstruct(ndcX, ndcY, rawDepth, inv);
        expectNearVec(rec, manual, 1.0e-5f);
        expectNearVec(rec, world, 1.0e-3f);

        const float    tiny       = 1.0e-8f;
        const Vector3f recTiny    = particleReconstructWorld(ndcX, ndcY, tiny, inv);
        const Vector3f manualTiny = manualReconstruct(ndcX, ndcY, tiny, inv);
        expectNearVec(recTiny, manualTiny, 1.0e-4f);
    };

    roundTrip(cam, Vector3f(0.4f, -0.25f, 8.0f));

    cam.SetSubpixelJitter(0.37f, -0.21f, 1280, 720);
    roundTrip(cam, Vector3f(-0.3f, 0.15f, 4.0f));

    // Offset camera: at the origin, max(w, 1e-6) hides 1e-8 vs 1e-7. Depth 0 is not a success input.
    Camera3D offset;
    offset.SetLens(Pi * 0.25f, 16.0f / 9.0f, 0.18f, 2000.0f);
    offset.LookAt(Vector3f(4.0f, -2.0f, -6.0f), Vector3f(4.0f, -2.0f, 8.0f), Vector3f(0.0f, 1.0f, 0.0f));
    const Matrix4f invOffset = offset.GetViewProj().Inverse();
    const float    tiny      = 1.0e-8f;
    const Vector3f clamped   = particleReconstructWorld(0.25f, -0.4f, tiny, invOffset);
    const Vector3f manual    = manualReconstruct(0.25f, -0.4f, tiny, invOffset);
    expectNearVec(clamped, manual, 1.0e-3f);

    const Vector4f rawW   = invOffset * Vector4f(0.25f, -0.4f, tiny, 1.0f);
    const float    rawDen = rawW.w > 1.0e-6f ? rawW.w : 1.0e-6f;
    const float    rawInv = 1.0f / rawDen;
    const Vector3f rawPos(rawW.x * rawInv, rawW.y * rawInv, rawW.z * rawInv);
    EXPECT_GT(std::fabs(clamped.x - rawPos.x) + std::fabs(clamped.y - rawPos.y) + std::fabs(clamped.z - rawPos.z), 1.0e-3f);
}

TEST(ParticleDepthCollision, ProjectUv_YFlip_NdcPlusYIsUvTop)
{
    const Matrix4f                id;
    const ParticleProjectUvResult top = particleProjectUv(Vector3f(0.0f, 1.0f, 0.0f), id);
    EXPECT_TRUE(top.inside);
    EXPECT_FLOAT_EQ(top.ndcY, 1.0f);
    EXPECT_FLOAT_EQ(top.v, 0.0f);

    const ParticleProjectUvResult bottom = particleProjectUv(Vector3f(0.0f, -1.0f, 0.0f), id);
    EXPECT_FALSE(bottom.inside);
    EXPECT_FLOAT_EQ(bottom.ndcY, -1.0f);
    EXPECT_FLOAT_EQ(bottom.v, 1.0f);
}

TEST(ParticleDepthCollision, BirthSample_PointBoxSphereCone)
{
    ParticleEmitterDesc point;
    point.shape         = ParticleEmitterDesc::Shape::Point;
    point.direction     = Vector3f(0.0f, 1.0f, 0.0f);
    point.spreadDegrees = 0.0f;
    point.startSpeed    = { 2.0f, 2.0f };
    point.lifetime      = { 1.0f, 1.0f };
    point.startColor[0] = 1.0f;
    point.startColor[1] = 1.0f;
    point.startColor[2] = 1.0f;
    point.startColor[3] = 1.0f;
    point.endColor[0]   = 1.0f;
    point.endColor[1]   = 0.0f;
    point.endColor[2]   = 0.0f;
    point.endColor[3]   = 1.0f;

    const ParticleBirthSampleResult a = particleBirthSample(point, 1u);
    const ParticleBirthSampleResult b = particleBirthSample(point, 1u);
    expectNearVec(a.position, Vector3f(0.0f, 0.0f, 0.0f), 1.0e-6f);
    expectNearVec(a.direction, Vector3f(0.0f, 1.0f, 0.0f), 1.0e-5f);
    expectNearVec(a.velocity, a.direction * 2.0f, 1.0e-5f);
    EXPECT_FLOAT_EQ(a.maxLife, 1.0f);
    EXPECT_FLOAT_EQ(a.life, a.maxLife);
    EXPECT_EQ(a.color0, 0xFFFFFFFFu);
    EXPECT_EQ(a.color1, 0xFF0000FFu);
    EXPECT_EQ(a.seed, 1u);
    expectNearVec(a.position, b.position, 0.0f);
    expectNearVec(a.velocity, b.velocity, 0.0f);
    EXPECT_FLOAT_EQ(a.rotation, b.rotation);

    point.lifetime                       = { 0.0f, 0.01f };
    const ParticleBirthSampleResult floored = particleBirthSample(point, 7u);
    EXPECT_FLOAT_EQ(floored.maxLife, 0.05f);
    EXPECT_FLOAT_EQ(floored.life, floored.maxLife);

    const ParticleBirthSampleResult zeroHash = particleBirthSample(point, 0u);
    expectNearVec(zeroHash.position, Vector3f(0.0f, 0.0f, 0.0f), 1.0e-6f);
    EXPECT_NEAR(lengthOf(zeroHash.direction), 1.0f, 1.0e-5f);

    ParticleEmitterDesc box;
    box.shape         = ParticleEmitterDesc::Shape::Box;
    box.shapeSize     = Vector3f(1.5f, 0.25f, 2.0f);
    box.direction     = Vector3f(1.0f, 0.0f, 0.0f);
    box.spreadDegrees = 20.0f;
    box.startSpeed    = { 1.0f, 1.0f };
    const float boxCos = std::cos(20.0f * DegToRad);
    const Vector3f boxAxis(1.0f, 0.0f, 0.0f);
    bool           boxMoved = false;
    for (uint32_t seed = 1; seed <= 24; ++seed)
    {
        const ParticleBirthSampleResult sample = particleBirthSample(box, seed);
        EXPECT_LE(std::fabs(sample.position.x), box.shapeSize.x + 1.0e-4f);
        EXPECT_LE(std::fabs(sample.position.y), box.shapeSize.y + 1.0e-4f);
        EXPECT_LE(std::fabs(sample.position.z), box.shapeSize.z + 1.0e-4f);
        EXPECT_GE(sample.direction.Dot(boxAxis), boxCos - 1.0e-4f);
        EXPECT_NEAR(lengthOf(sample.direction), 1.0f, 1.0e-4f);
        if (lengthOf(sample.position) > 1.0e-3f)
            boxMoved = true;
    }
    EXPECT_TRUE(boxMoved);
    const ParticleBirthSampleResult boxA = particleBirthSample(box, 3u);
    const ParticleBirthSampleResult boxB = particleBirthSample(box, 4u);
    EXPECT_GT(lengthOf(boxA.position - boxB.position) + lengthOf(boxA.direction - boxB.direction), 1.0e-4f);

    ParticleEmitterDesc sphere;
    sphere.shape         = ParticleEmitterDesc::Shape::Sphere;
    sphere.shapeSize     = Vector3f(1.5f, 0.0f, 0.0f);
    sphere.direction     = Vector3f(0.0f, 0.2f, 0.8f);
    sphere.spreadDegrees = 35.0f;
    sphere.startSpeed    = { 3.0f, 3.0f };
    const Vector3f sphereAxis = Vector3f(0.0f, 0.2f, 0.8f) / lengthOf(Vector3f(0.0f, 0.2f, 0.8f));
    const float sphereCos = std::cos(35.0f * DegToRad);
    for (uint32_t seed = 1; seed <= 32; ++seed)
    {
        const ParticleBirthSampleResult sample = particleBirthSample(sphere, seed);
        EXPECT_LE(lengthOf(sample.position), sphere.shapeSize.x + 1.0e-4f);
        EXPECT_GE(sample.direction.Dot(sphereAxis), sphereCos - 1.0e-4f);
        EXPECT_NEAR(lengthOf(sample.direction), 1.0f, 1.0e-4f);
        expectNearVec(sample.velocity, sample.direction * 3.0f, 1.0e-4f);
    }
}

TEST(ParticleDepthCollision, IsOrthographic_WrapperDoesNotChangeProjection)
{
    Camera3D cam;
    cam.SetLens(Pi * 0.5f, 1.0f, 0.1f, 100.0f);
    EXPECT_FALSE(cam.IsOrthographic());
    const Matrix4f perspective = cam.GetProj();

    cam.SetOrthographic(20.0f, 10.0f, 0.1f, 100.0f);
    EXPECT_TRUE(cam.IsOrthographic());
    const Matrix4f ortho = cam.GetProj();
    EXPECT_NE(ortho, perspective);

    cam.SetLens(Pi * 0.5f, 1.0f, 0.1f, 100.0f);
    EXPECT_FALSE(cam.IsOrthographic());
    EXPECT_EQ(cam.GetProj(), perspective);
}
