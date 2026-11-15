#include "Editor/EditorInternals.h"

#include "Collision/StaticCollision.h"
#include "Particles/ParticleScene.h"
#include "Math/AABox3f.h"
#include "Math/MathHelper.h"
#include "Math/Sphere3f.h"
#include "Render/Texture2D.h"
#include "Render/Renderer.h"

#include <cmath>
#include <vector>

using namespace Dark;
using namespace Math;

namespace Dark::EditorDetail 
{

    const float kPalette[][4] = {
        { 1.00f, 1.00f, 1.00f, 1.0f },
        { 0.95f, 0.35f, 0.30f, 1.0f },
        { 0.35f, 0.80f, 0.40f, 1.0f },
        { 0.30f, 0.55f, 0.95f, 1.0f },
        { 0.95f, 0.80f, 0.25f, 1.0f },
        { 0.75f, 0.40f, 0.90f, 1.0f },
        { 0.25f, 0.85f, 0.85f, 1.0f },
        { 0.95f, 0.55f, 0.20f, 1.0f },
    };
    const int kPaletteCount = static_cast<int>(sizeof(kPalette) / sizeof(kPalette[0]));

    void copyColor(float dst[4], const float src[4])
    {
        dst[0] = src[0];
        dst[1] = src[1];
        dst[2] = src[2];
        dst[3] = src[3];
    }

    bool createChecker(
        Renderer& renderer, Texture2D& out, uint8_t r0, uint8_t g0, uint8_t b0,
        uint8_t r1, uint8_t g1, uint8_t b1, uint32_t size, uint32_t cell)
    {
        std::vector<uint8_t> px(static_cast<size_t>(size) * size * 4u);
        for (uint32_t y = 0; y < size; ++y)
        {
            for (uint32_t x = 0; x < size; ++x)
            {
                const bool   alt = ((x / cell) + (y / cell)) & 1u;
                const size_t i   = (static_cast<size_t>(y) * size + x) * 4u;
                px[i + 0]        = alt ? r1 : r0;
                px[i + 1]        = alt ? g1 : g0;
                px[i + 2]        = alt ? b1 : b0;
                px[i + 3]        = 255;
            }
        }
        return out.createFromRGBA(renderer, px.data(), size, size, size * 4u, Dark::Color::TextureUsage::Hud);
    }

    uint32_t packRgba8(const float c[4])
    {
        auto u8 = [](float v) -> uint32_t {
            if (v < 0.0f)
                v = 0.0f;
            if (v > 1.0f)
                v = 1.0f;
            return static_cast<uint32_t>(v * 255.0f + 0.5f);
        };
        return (u8(c[0]) << 24) | (u8(c[1]) << 16) | (u8(c[2]) << 8) | u8(c[3]);
    }

    void unpackRgba8(uint32_t rgba, float out[4])
    {
        out[0] = static_cast<float>((rgba >> 24) & 0xFFu) / 255.0f;
        out[1] = static_cast<float>((rgba >> 16) & 0xFFu) / 255.0f;
        out[2] = static_cast<float>((rgba >> 8) & 0xFFu) / 255.0f;
        out[3] = static_cast<float>(rgba & 0xFFu) / 255.0f;
    }

    void defaultLightColor(float out[4])
    {
        out[0] = 1.00f;
        out[1] = 0.92f;
        out[2] = 0.75f;
        out[3] = 1.00f;
    }

    Entity glowMeshOf(World& world, Entity e)
    {
        if (const auto* light = world.get<LocalLightComponent>(e))
            return light->emissiveMesh;
        return {};
    }

    Entity lightOwningGlowMesh(World& world, Entity mesh)
    {
        Entity found{};
        if (!mesh.valid())
            return found;
        world.each<LocalLightComponent>([&](Entity e, LocalLightComponent& light) {
            if (light.emissiveMesh.valid() && light.emissiveMesh.id() == mesh.id())
                found = e;
        });
        return found;
    }

    bool keepsPlacedHeight(World& world, const EditorObjectComponent* so, Entity e)
    {
        if (so && (editorPointOrSpot(so->source) || editorGlobalLight(so->source) || sceneHasComponent(so->source, "CloudVolume")))
            return true;
        return lightOwningGlowMesh(world, e).valid();
    }

    void syncGlowPairPosition(World& world, Entity e)
    {
        const auto* xf = world.get<TransformComponent>(e);
        if (!xf)
            return;
        Entity other = glowMeshOf(world, e);
        if (!other.valid())
            other = lightOwningGlowMesh(world, e);
        if (!other.valid() || other.id() == e.id())
            return;
        if (auto* ox = world.get<TransformComponent>(other))
            ox->position = xf->position;
    }

    void eulerXYZFromQuat(const Quaternion& q, float& pitch, float& yaw, float& roll)
    {
        const float sinY = Clamp(2.0f * (q.w * q.y - q.z * q.x), -1.0f, 1.0f);
        yaw              = asinf(sinY);
        pitch            = atan2f(2.0f * (q.w * q.x + q.y * q.z), 1.0f - 2.0f * (q.x * q.x + q.y * q.y));
        roll             = atan2f(2.0f * (q.w * q.z + q.x * q.y), 1.0f - 2.0f * (q.y * q.y + q.z * q.z));
    }

    Matrix4f makePointLightGizmoWorld(const Vector3f& pos, float range)
    {
        return Matrix4f::ScaleMatrix(Max(range, 0.01f)) * Matrix4f::TranslationMatrix(pos.x, pos.y, pos.z);
    }

    Matrix4f makeSpotLightGizmoWorld(const TransformComponent& xf, float range, float outerConeDeg)
    {
        float outerRad = DegreesToRadians(Max(outerConeDeg, 0.1f));
        if (outerRad >= HalfPi - 0.01f)
            outerRad = HalfPi - 0.01f;
        const float    xy = tanf(outerRad) * Max(range, 0.01f);
        const Matrix4f S  = Matrix4f::ScaleMatrixXYZ(xy, xy, Max(range, 0.01f));
        const Matrix4f R  = xf.rotation.ToMatrix4();
        const Matrix4f T  = Matrix4f::TranslationMatrix(xf.position.x, xf.position.y, xf.position.z);
        return S * R * T;
    }

    Quaternion defaultSpotRotation()
    {
        return Quaternion::FromLookRotation(Vector3f(0.0f, -1.0f, 0.0f), Vector3f(0.0f, 0.0f, 1.0f));
    }

    Quaternion defaultDirectionalRotation()
    {
        Vector3f dir(0.35f, 0.85f, -0.35f);
        dir.Normalize();
        return Quaternion::FromLookRotation(dir, Vector3f(0.0f, 1.0f, 0.0f));
    }

    Vector3f directionalLightDir(const TransformComponent& xf)
    {
        Vector3f dir = xf.rotation.Rotate(Vector3f(0.0f, 0.0f, 1.0f));
        if (dir.MagnitudeSqrd() <= 1.0e-8f)
            dir = Vector3f(0.35f, 0.85f, -0.35f);
        dir.Normalize();
        return dir;
    }

    const char* netRoleLabel(NetRole role)
    {
        switch (role)
        {
        case NetRole::Joining:
            return "Joining\u2026";
        case NetRole::Host:
            return "Host";
        case NetRole::Client:
            return "Client";
        case NetRole::Idle:
        default:
            return "Idle";
        }
    }

    void descFromSceneData(const SceneObjectData& d, ParticleEmitterDesc& out)
    {
        particleDescFromSceneData(d, out);
    }

    void sceneDataFromDesc(const ParticleEmitterDesc& src, SceneObjectData& d)
    {
        sceneDataFromParticleDesc(src, d);
    }
} // namespace Dark::EditorDetail
