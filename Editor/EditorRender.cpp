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

void EditorApp::onRender()
{
    if (!renderer().beginFrame())
    {
        requestQuit();
        return;
    }
    if (m_skinRing.isValid())
        m_skinRing.beginFrame(renderer().frameIndex());
    auto* cmd = renderer().commandList();

    if (m_imgui.isReady())
        m_imgui.beginFrame();

    if (m_sceneMode == SceneMode::Scene2D)
        renderScene2D(cmd);
    else
        renderScene3D(cmd);

    if (m_imgui.isReady())
    {
        drawEditorUi();
        m_imgui.render(renderer());
    }

    renderer().endFrame();
}

void EditorApp::renderScene2D(ID3D12GraphicsCommandList* cmd)
{
    if (!cmd || !ensure2DResources())
        return;

    m_spritePipe.bind(cmd);

    auto drawObj = [&](Entity e, const EditorObjectComponent& so, const TransformComponent& xf) {
        const bool selected = m_selected.valid() && m_selected.id() == e.id();
        float cr = so.color[0], cg = so.color[1], cb = so.color[2];
        if (selected)
        {
            cr = cr * 0.55f + 1.0f * 0.45f;
            cg = cg * 0.55f + 0.85f * 0.45f;
            cb = cb * 0.55f + 0.20f * 0.45f;
        }
        const Vector2f pos(xf.position.x, xf.position.y);
        const Vector2f size(std::fabs(xf.scale.x), std::fabs(xf.scale.y));
        const Texture2D* tex = &m_texPlatform;
        float z = 2.0f;
        float uvx = size.x;
        float uvy = size.y;
        if (so.type == SceneObjectType::Coin)
        {
            tex = &m_texCoin;
            z   = 1.2f;
            uvx = 1.0f;
            uvy = 1.0f;
        }
        else if (so.type == SceneObjectType::Spawn)
        {
            tex = &m_texSpawn;
            z   = 1.0f;
            uvx = 1.0f;
            uvy = 1.0f;
        }
        drawSprite2D(cmd, *tex, pos, size, z, cr, cg, cb, uvx, uvy);
    };

    world().each<EditorObjectComponent>([&](Entity e, EditorObjectComponent& so) {
        if (!isScene2DType(so.type))
            return;
        const auto* xf = world().get<TransformComponent>(e);
        if (xf)
            drawObj(e, so, *xf);
    });

    if (m_showGrid && m_grid2D.valid())
    {
        m_linePipeline.bind(cmd);
        LineFrameConstants lc{};
        copyMatrix(lc.worldViewProj, m_camera2D.GetViewProj());
        lc.color[0] = 0.20f;
        lc.color[1] = 0.35f;
        lc.color[2] = 0.50f;
        lc.color[3] = 1.0f;
        m_linePipeline.setConstants(cmd, lc);
        m_grid2D.draw(cmd);
    }

    if (m_selected.valid() && m_boxOutline2D.valid())
    {
        if (const EditorObjectComponent* so = findObject(m_selected))
        {
            if (const auto* xf = world().get<TransformComponent>(m_selected))
            {
                const AABox2f  box   = objectBounds2D(so->type, xf->position, xf->scale);
                const Vector2f c = box.Center();
                const Vector2f s = box.Size();
                const Matrix4f world = Matrix4f::ScaleMatrixXYZ(s.x, s.y, 1.0f)
                    * Matrix4f::TranslationMatrix(c.x, c.y, 0.4f);
                m_linePipeline.bind(cmd);
                LineFrameConstants lc{};
                copyMatrix(lc.worldViewProj, world * m_camera2D.GetViewProj());
                lc.color[0] = 1.0f;
                lc.color[1] = 0.85f;
                lc.color[2] = 0.15f;
                lc.color[3] = 1.0f;
                m_linePipeline.setConstants(cmd, lc);
                m_boxOutline2D.draw(cmd);
            }
        }
    }

    renderer().stats().drawCalls = editorObjectCount() + 2;
}
