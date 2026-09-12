#include "EditorApp.h"

#include "Editor/EditorInternals.h"
#include "Editor/EditorObject.h"
#include "Scene/SceneFile.h"
#include "ECS/Components.h"
#include "Network/Replication.h"
#include "Core/ContentRoots.h"
#include "Core/Log.h"
#include "Core/UiPalette.h"
#include "Ui/Icons.h"
#include "Ui/ImGuiTheme.h"
#include "Input/InputCodes.h"
#include "Collision/StaticCollision.h"
#include "Math/AABox3f.h"
#include "Math/AABox2f.h"
#include "Math/MathHelper.h"
#include "Math/Matrix4f.h"
#include "Math/Quaternion.h"
#include "Math/Sphere3f.h"
#include "Math/Vector2f.h"
#include "Math/Vector3f.h"
#include "Math/Ray3f.h"
#include "Render/LineMesh.h"
#include "Render/MeshGen.h"
#include "Render/TaaJitter.h"
#include "Render/ModelDraw.h"
#include "Assets/Model.h"
#include "Animation/AnimGraphTick.h"
#include "Animation/AnimGraphComponent.h"

#include <imgui.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace Dark;
using namespace Dark::EditorDetail;
using namespace Math;

Entity EditorApp::pickObject(const Ray3f& ray)
{
    Entity best{};
    float bestT = 1e30f;
    world().each<EditorObjectComponent>([&](Entity e, EditorObjectComponent& so) {
        const auto* xf = world().get<TransformComponent>(e);
        if (!xf)
            return;
        Collision::RayHit3D hit{};
        if (isLocalLightType(so.type))
        {
            float range = 8.0f;
            if (const auto* light = world().get<LocalLightComponent>(e))
                range = light->range;
            const float r = Max(0.35f, range * 0.05f);
            hit           = Collision::Intersect(ray, Sphere3f(xf->position, r));
        }
        else
        {
            Vector3f half(0.5f * xf->scale.x, 0.5f * xf->scale.y, 0.5f * xf->scale.z);
            if (so.type == SceneObjectType::ParticleEmitter)
                half = Vector3f(0.25f, 0.25f, 0.25f);
            const AABox3f box = AABox3f::FromCenterExtents(xf->position, half);
            hit               = Collision::Intersect(ray, box);
        }
        if (hit.hit && hit.t >= 0.0f && hit.t < bestT)
        {
            bestT = hit.t;
            best  = e;
        }
    });
    return best;
}

Entity EditorApp::spawnObject(
    SceneObjectType type,
    const Vector3f& pos,
    const Vector3f& scale,
    const Quaternion& rot,
    const float color[4],
    const ParticleEmitterDesc* particleDesc,
    const SceneObjectData* authored,
    bool registerNet)
{
    if (netClientLocked())
        return {};

    Entity e = world().createEntity();
    world().emplace<TagComponent>(e, toString(type));

    TransformComponent xf{};
    xf.position = pos;
    xf.scale    = scale;
    xf.rotation = rot;
    const bool lightType = isLocalLightType(type);
    if (m_sceneMode == SceneMode::Scene3D && isScene3DType(type) && type != SceneObjectType::ParticleEmitter && !lightType
        && xf.position.y < 0.5f * xf.scale.y)
        xf.position.y = 0.5f * xf.scale.y;
    world().emplace<TransformComponent>(e, xf);

    if (!lightType)
    {
        auto& mc       = world().emplace<MeshComponent>(e);
        mc.matAssetID  = m_propMaterial ? m_propMaterial->id : NULL_ASSET;
        mc.meshAssetID = NULL_ASSET;
        if (authored)
            mc.emissive = authored->emissive;
    }
    else
    {
        LocalLightComponent light{};
        fillDefaultLocalLight(light, type);
        light.color = Vector3f(color[0], color[1], color[2]);
        if (authored && authored->hasLight)
        {
            light.intensity    = authored->lightIntensity;
            light.range        = authored->lightRange;
            light.innerConeDeg = authored->lightInnerDeg;
            light.outerConeDeg = authored->lightOuterDeg;
            light.sourceRadius = authored->lightSourceRadius;
            light.enabled      = authored->lightEnabled;
        }
        world().emplace<LocalLightComponent>(e, light);
    }

    EditorObjectComponent so{};
    so.type         = type;
    copyColor(so.color, color);
    so.emitterIndex = -1;

    if (type == SceneObjectType::ParticleEmitter)
    {
        auto emitter = std::make_unique<ParticleEmitter>();
        ParticleEmitterDesc desc = particleDesc ? *particleDesc : makeDefaultParticleDesc();
        emitter->setDesc(desc);
        emitter->setTransform(xf.position, xf.rotation);
        emitter->play();
        so.emitterIndex = static_cast<int>(m_emitters.size());
        m_emitters.push_back(std::move(emitter));
        if (auto* t = world().get<TransformComponent>(e))
            t->scale = Vector3f(0.35f, 0.35f, 0.35f);
    }

    world().emplace<EditorObjectComponent>(e, so);
    m_selected = e;
    if (registerNet && isReplicatedProp(type))
        network().registerEntity(world(), e, prefabFromType(type), ClientId::Host, packRgba8(so.color));
    DE_LOG_INFO("Editor: spawn {} #{} (emitters={})", toString(type), e.id(), m_emitters.size());
    return e;
}

Entity EditorApp::placeAtCursor(SceneObjectType type)
{
    if (m_sceneMode == SceneMode::Scene2D)
    {
        if (!isScene2DType(type))
            type = SceneObjectType::Platform;
        Vector2f p{};
        worldFromMouse2D(p);
        if (m_gridSnap > 0.0f)
        {
            p.x = snap(p.x, m_gridSnap);
            p.y = snap(p.y, m_gridSnap);
        }
        float col[4]{};
        defaultColor2D(type, col);
        const Entity e = spawnObject(type, Vector3f(p.x, p.y, 0.0f), defaultScale2D(type), Quaternion::IDENTITY, col, nullptr);
        if (e.valid())
            audio().play3D(m_sfxPlace, Vector3f(p.x, p.y, 0.0f), 0.65f);
        return e;
    }

    Vector3f hit{};
    bool ok = groundHitFromMouse(hit);
    if (!ok)
    {
        Ray3f ray(m_camera.GetPosition(), m_camera.GetLook());
        ok = groundHitFromRay(ray, hit);
    }
    if (!ok)
    {
        DE_LOG_WARN("Editor: place failed (no ground hit)");
        return {};
    }
    if (m_gridSnap > 0.0f)
    {
        hit.x = snap(hit.x, m_gridSnap);
        hit.z = snap(hit.z, m_gridSnap);
    }
    float lightCol[4]{};
    defaultLightColor(lightCol);
    const float* col = isLocalLightType(type) ? lightCol : kPalette[m_colorIndex % kPaletteCount];
    Vector3f scale(1, 1, 1);
    if (isLocalLightType(type))
        hit.y += 1.5f;
    else if (type == SceneObjectType::ParticleEmitter)
        hit.y = 0.5f;
    else
        hit.y = 0.5f * scale.y;
    const Entity e = spawnObject(type, hit, scale, Quaternion::IDENTITY, col, nullptr);
    if (e.valid())
        audio().play3D(m_sfxPlace, hit, 0.65f);
    return e;
}

Entity EditorApp::placeGlowProp()
{
    if (m_sceneMode == SceneMode::Scene2D)
        return {};

    Vector3f hit{};
    bool ok = groundHitFromMouse(hit);
    if (!ok)
    {
        Ray3f ray(m_camera.GetPosition(), m_camera.GetLook());
        ok = groundHitFromRay(ray, hit);
    }
    if (!ok)
    {
        DE_LOG_WARN("Editor: glow prop place failed (no ground hit)");
        return {};
    }
    if (m_gridSnap > 0.0f)
    {
        hit.x = snap(hit.x, m_gridSnap);
        hit.z = snap(hit.z, m_gridSnap);
    }
    hit.y += 1.5f;

    float col[4]{};
    defaultLightColor(col);
    SceneObjectData meshAuthored{};
    meshAuthored.emissive = 1.0f;
    const Entity meshE    = spawnObject(SceneObjectType::Sphere, hit, Vector3f(1, 1, 1), Quaternion::IDENTITY, col, nullptr, &meshAuthored, false);
    const Entity lightE   = spawnObject(SceneObjectType::PointLight, hit, Vector3f(1, 1, 1), Quaternion::IDENTITY, col, nullptr);
    if (lightE.valid())
    {
        if (auto* light = world().get<LocalLightComponent>(lightE))
            light->emissiveMesh = meshE;
        audio().play3D(m_sfxPlace, hit, 0.65f);
    }
    return lightE.valid() ? lightE : meshE;
}

void EditorApp::deleteSelected()
{
    if (netClientLocked() || !m_selected.valid())
        return;
    audio().play2D(m_sfxDelete, 0.55f);

    Entity extra{};
    if (auto* light = world().get<LocalLightComponent>(m_selected))
        extra = light->emissiveMesh;
    else
        extra = lightOwningGlowMesh(world(), m_selected);

    auto eraseOne = [&](Entity e) {
        if (!e.valid() || !world().alive(e))
            return;
        if (EditorObjectComponent* so = findObject(e))
        {
            if (so->type == SceneObjectType::ParticleEmitter && so->emitterIndex >= 0
                && so->emitterIndex < static_cast<int>(m_emitters.size()))
                m_emitters[static_cast<size_t>(so->emitterIndex)].reset();
        }
        if (world().has<NetworkedComponent>(e))
        {
            network().unregisterEntity(world(), e);
            return;
        }
        world().destroyEntity(e);
    };

    const Entity primary = m_selected;
    eraseOne(primary);
    if (extra.valid() && extra.id() != primary.id())
        eraseOne(extra);
    m_selected = {};
    m_dragging = false;
    DE_LOG_INFO("Editor: deleted ({} remaining)", editorObjectCount());
}

void EditorApp::selectNext(int delta)
{
    std::vector<Entity> ents;
    collectEditorEntities(ents);
    if (ents.empty())
    {
        m_selected = {};
        return;
    }
    int idx = 0;
    if (m_selected.valid())
    {
        for (size_t i = 0; i < ents.size(); ++i)
            if (ents[i].id() == m_selected.id())
            {
                idx = static_cast<int>(i);
                break;
            }
        idx += delta;
    }
    const int n = static_cast<int>(ents.size());
    idx = ((idx % n) + n) % n;
    m_selected = ents[static_cast<size_t>(idx)];
}

void EditorApp::cyclePlaceType(int delta)
{
    const SceneObjectType types3D[] = {
        SceneObjectType::Cube, SceneObjectType::Sphere, SceneObjectType::ParticleEmitter,
        SceneObjectType::PointLight, SceneObjectType::SpotLight
    };
    const SceneObjectType types2D[] = {
        SceneObjectType::Platform, SceneObjectType::Coin, SceneObjectType::Spawn
    };
    const SceneObjectType* types = (m_sceneMode == SceneMode::Scene2D) ? types2D : types3D;
    const int n = (m_sceneMode == SceneMode::Scene2D)
        ? static_cast<int>(_countof(types2D))
        : static_cast<int>(_countof(types3D));
    int idx = 0;
    for (int i = 0; i < n; ++i)
        if (types[i] == m_placeType)
            idx = i;
    idx = ((idx + delta) % n + n) % n;
    m_placeType = types[idx];
    DE_LOG_INFO("Editor: place type = {}", toString(m_placeType));
}

void EditorApp::cycleSelectedColor()
{
    m_colorIndex = (m_colorIndex + 1) % kPaletteCount;
    const float* col = kPalette[m_colorIndex];
    if (EditorObjectComponent* so = findObject(m_selected))
    {
        copyColor(so->color, col);
        if (auto* light = world().get<LocalLightComponent>(m_selected))
            light->color = Vector3f(col[0], col[1], col[2]);
    }
}

void EditorApp::clearScene()
{
    if (netSceneLocked())
        return;
    std::vector<Entity> ents;
    collectEditorEntities(ents);
    for (Entity e : ents)
    {
        if (world().has<NetworkedComponent>(e))
            network().unregisterEntity(world(), e);
        else if (world().alive(e))
            world().destroyEntity(e);
    }
    m_emitters.clear();
    m_selected = {};
    m_dragging = false;
}
