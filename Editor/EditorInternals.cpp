#include "Editor/EditorInternals.h"

#include "Core/ContentRoots.h"
#include "Collision/StaticCollision.h"
#include "Math/AABox3f.h"
#include "Math/MathHelper.h"
#include "Math/Sphere3f.h"
#include "Render/Texture2D.h"
#include "Render/Renderer.h"
#include "Assets/AssetManager.h"

#include <cmath>
#include <cstring>
#include <filesystem>
#include <vector>

using namespace Dark;
using namespace Dark::EditorDetail;
using namespace Math;

namespace Dark {
namespace EditorDetail {

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

void mountContentRoots(AssetManager& assets)
{
    namespace fs = std::filesystem;
    for (const fs::path& c : contentRootCandidates())
    {
        std::error_code ec;
        if (!c.empty() && fs::exists(c, ec) && !ec && fs::is_directory(c, ec) && !ec)
            assets.mountDirectory(c);
    }
}

void copyMatrix(float dst[16], const Matrix4f& m)
{
    std::memcpy(dst, m.m_afEntry, sizeof(float) * 16);
}

Matrix4f makeWorldMatrix(const TransformComponent& xf)
{
    const Matrix4f S = Matrix4f::ScaleMatrixXYZ(xf.scale.x, xf.scale.y, xf.scale.z);
    const Matrix4f R = xf.rotation.ToMatrix4();
    const Matrix4f T = Matrix4f::TranslationMatrix(xf.position.x, xf.position.y, xf.position.z);
    return S * R * T;
}

void copyColor(float dst[4], const float src[4])
{
    dst[0] = src[0];
    dst[1] = src[1];
    dst[2] = src[2];
    dst[3] = src[3];
}

bool createChecker(
    Renderer& renderer,
    Texture2D& out,
    uint8_t r0, uint8_t g0, uint8_t b0,
    uint8_t r1, uint8_t g1, uint8_t b1,
    uint32_t size = 32,
    uint32_t cell = 8)
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
    return out.createFromRGBA(renderer, px.data(), size, size, size * 4u);
}

Vector3f defaultScale2D(SceneObjectType type)
{
    switch (type)
    {
    case SceneObjectType::Platform: return Vector3f(4.0f, 0.6f, 1.0f);
    case SceneObjectType::Coin:     return Vector3f(0.5f, 0.5f, 1.0f);
    case SceneObjectType::Spawn:    return Vector3f(0.8f, 1.4f, 1.0f);
    default:                        return Vector3f(1.0f, 1.0f, 1.0f);
    }
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

bool isReplicatedProp(SceneObjectType type)
{
    return type == SceneObjectType::Cube || type == SceneObjectType::Sphere
        || type == SceneObjectType::Platform || type == SceneObjectType::Coin;
}

bool isLocalLightType(SceneObjectType type)
{
    return type == SceneObjectType::PointLight || type == SceneObjectType::SpotLight;
}

void defaultLightColor(float out[4])
{
    out[0] = 1.00f;
    out[1] = 0.92f;
    out[2] = 0.75f;
    out[3] = 1.00f;
}

void fillDefaultLocalLight(LocalLightComponent& light, SceneObjectType type)
{
    light              = LocalLightComponent{};
    light.type         = (type == SceneObjectType::SpotLight) ? LocalLightType::Spot : LocalLightType::Point;
    light.color        = Vector3f(1.0f, 0.92f, 0.75f);
    light.innerConeDeg = 12.0f;
    light.outerConeDeg = 25.0f;
    light.sourceRadius = 0.05f;
    light.enabled      = true;
    if (light.type == LocalLightType::Spot)
    {
        light.intensity = 800.0f;
        light.range     = 16.0f;
    }
    else
    {
        light.intensity = 600.0f;
        light.range     = 8.0f;
    }
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
    if (so && isLocalLightType(so->type))
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

NetPrefab prefabFromType(SceneObjectType type)
{
    switch (type)
    {
    case SceneObjectType::Sphere:
        return NetPrefab::Sphere;
    case SceneObjectType::Platform:
        return NetPrefab::Platform;
    case SceneObjectType::Coin:
        return NetPrefab::Coin;
    default:
        return NetPrefab::Cube;
    }
}

SceneObjectType typeFromPrefab(NetPrefab prefab)
{
    switch (prefab)
    {
    case NetPrefab::Sphere:
        return SceneObjectType::Sphere;
    case NetPrefab::Platform:
        return SceneObjectType::Platform;
    case NetPrefab::Coin:
        return SceneObjectType::Coin;
    case NetPrefab::Player2D:
        return SceneObjectType::Spawn;
    case NetPrefab::Cube:
    case NetPrefab::PlayerPawn:
    default:
        return SceneObjectType::Cube;
    }
}

const char* netRoleLabel(NetRole role)
{
    switch (role)
    {
    case NetRole::Joining:
        return "Joining…";
    case NetRole::Host:
        return "Host";
    case NetRole::Client:
        return "Client";
    case NetRole::Idle:
    default:
        return "Idle";
    }
}

void defaultColor2D(SceneObjectType type, float out[4])
{
    switch (type)
    {
    case SceneObjectType::Platform:
        out[0] = 0.72f; out[1] = 0.52f; out[2] = 0.32f; out[3] = 1.0f;
        break;
    case SceneObjectType::Coin:
        out[0] = 0.95f; out[1] = 0.80f; out[2] = 0.25f; out[3] = 1.0f;
        break;
    case SceneObjectType::Spawn:
        out[0] = 0.20f; out[1] = 0.80f; out[2] = 0.70f; out[3] = 1.0f;
        break;
    default:
        out[0] = 1.0f; out[1] = 1.0f; out[2] = 1.0f; out[3] = 1.0f;
        break;
    }
}

void descFromSceneData(const SceneObjectData& d, ParticleEmitterDesc& out)
{
    out = ParticleEmitterDesc{};
    out.name            = d.particleName;
    out.maxParticles    = d.maxParticles;
    out.emissionRate    = d.emissionRate;
    out.duration        = d.duration;
    out.looping         = d.looping;
    out.lifetime        = { d.lifetimeMin, d.lifetimeMax };
    out.startSpeed      = { d.startSpeedMin, d.startSpeedMax };
    out.startSize       = { d.startSizeMin, d.startSizeMax };
    out.endSize         = { d.endSizeMin, d.endSizeMax };
    copyColor(out.startColor, d.startColor);
    copyColor(out.endColor, d.endColor);
    out.gravity         = d.gravity;
    out.direction       = d.direction;
    out.spreadDegrees   = d.spreadDegrees;
    out.shape           = static_cast<ParticleEmitterDesc::Shape>(d.shape);
    out.shapeSize       = d.shapeSize;
    out.additiveBlend   = d.additiveBlend;
    out.simulationSpeed = d.simulationSpeed;
    out.renderMode      = static_cast<ParticleEmitterDesc::RenderMode>(d.renderMode);
    out.ribbonCount     = d.ribbonCount;
    out.ribbonUvScale   = d.ribbonUvScale;
}

void sceneDataFromDesc(const ParticleEmitterDesc& src, SceneObjectData& d)
{
    d.hasParticle     = true;
    d.particleName    = src.name;
    d.maxParticles    = src.maxParticles;
    d.emissionRate    = src.emissionRate;
    d.duration        = src.duration;
    d.looping         = src.looping;
    d.lifetimeMin     = src.lifetime.min;
    d.lifetimeMax     = src.lifetime.max;
    d.startSpeedMin   = src.startSpeed.min;
    d.startSpeedMax   = src.startSpeed.max;
    d.startSizeMin    = src.startSize.min;
    d.startSizeMax    = src.startSize.max;
    d.endSizeMin      = src.endSize.min;
    d.endSizeMax      = src.endSize.max;
    copyColor(d.startColor, src.startColor);
    copyColor(d.endColor, src.endColor);
    d.gravity         = src.gravity;
    d.direction       = src.direction;
    d.spreadDegrees   = src.spreadDegrees;
    d.shape           = static_cast<int>(src.shape);
    d.shapeSize       = src.shapeSize;
    d.additiveBlend   = src.additiveBlend;
    d.simulationSpeed = src.simulationSpeed;
    d.renderMode      = static_cast<int>(src.renderMode);
    d.ribbonCount     = src.ribbonCount;
    d.ribbonUvScale   = src.ribbonUvScale;
}

} // namespace EditorDetail
} // namespace Dark
