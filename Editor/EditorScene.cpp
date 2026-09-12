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

Entity EditorApp::pickObject2D(const Vector2f& worldPos)
{
    Entity best{};
    float  bestArea = 1.0e30f;
    world().each<EditorObjectComponent>([&](Entity e, EditorObjectComponent& so) {
        if (!isScene2DType(so.type))
            return;
        const auto* xf = world().get<TransformComponent>(e);
        if (!xf)
            return;
        const AABox2f box = objectBounds2D(so.type, xf->position, xf->scale);
        if (!box.Contains(worldPos))
            return;
        const float area = box.Area();
        if (area < bestArea)
        {
            bestArea = area;
            best     = e;
        }
    });
    return best;
}

EditorObjectComponent* EditorApp::findObject(Entity e)
{
    if (!e.valid() || !world().alive(e))
        return nullptr;
    return world().get<EditorObjectComponent>(e);
}

uint32_t EditorApp::editorObjectCount()
{
    uint32_t n = 0;
    world().each<EditorObjectComponent>([&](Entity, EditorObjectComponent&) { ++n; });
    return n;
}

void EditorApp::collectEditorEntities(std::vector<Entity>& out)
{
    out.clear();
    world().each<EditorObjectComponent>([&](Entity e, EditorObjectComponent&) { out.push_back(e); });
}

ParticleEmitter* EditorApp::selectedEmitter()
{
    EditorObjectComponent* so = findObject(m_selected);
    if (!so || so->type != SceneObjectType::ParticleEmitter)
        return nullptr;
    if (so->emitterIndex < 0 || so->emitterIndex >= static_cast<int>(m_emitters.size()))
        return nullptr;
    return m_emitters[static_cast<size_t>(so->emitterIndex)].get();
}

ParticleEmitterDesc EditorApp::makeDefaultParticleDesc() const
{
    ParticleEmitterDesc d{};
    d.name = "Emitter";
    return d;
}
