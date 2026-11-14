#include "EditorApp.h"

#include "Assets/MeshData.h"
#include "Editor/EditorInternals.h"
#include "Input/InputCodes.h"
#include "Render/LineMesh.h"
#include "Render/LinePipeline.h"
#include "Terrain/FoliageSpawn.h"
#include "Terrain/TrailPath.h"

#include <imgui.h>

#include <cstdio>
#include <vector>

using namespace Dark;
using namespace Dark::EditorDetail;
using namespace Math;
using namespace Terrain;

namespace
{
    constexpr float kTrailPickMetres = 1.25f;
    constexpr float kTrailLiftMetres = 0.2f;
    constexpr int   kTrailMeshVerts  = 8192;

    void pushLine(LineMeshData& data, const Vector3f& a, const Vector3f& b, int& budget)
    {
        if (budget < 2)
            return;
        const uint32_t index = static_cast<uint32_t>(data.positions.size());
        data.positions.push_back(a);
        data.positions.push_back(b);
        data.indices.push_back(index);
        data.indices.push_back(index + 1u);
        budget -= 2;
    }

    void pushCross(LineMeshData& data, const Vector3f& p, float arm, int& budget)
    {
        pushLine(data, Vector3f(p.x - arm, p.y, p.z), Vector3f(p.x + arm, p.y, p.z), budget);
        pushLine(data, Vector3f(p.x, p.y, p.z - arm), Vector3f(p.x, p.y, p.z + arm), budget);
    }

    bool createLines(Renderer& renderer, const LineMeshData& data, LineMesh& out)
    {
        if (data.positions.empty() || data.indices.empty())
        {
            out = {};
            return true;
        }
        out = LineMesh::Create(renderer, data);
        return out.valid();
    }
}

bool EditorApp::acquireFoliageThread()
{
    if (m_foliageDone.load())
        pollFoliageSpawn();
    if (m_foliageRunning.load())
        return false;
    if (m_foliageThread.joinable())
    {
        // Assigning a joinable std::thread calls std::terminate.
        if (!m_foliageDone.load())
            return false;
        m_foliageThread.join();
    }
    return true;
}

void EditorApp::startCoverRestamp()
{
    if (!acquireFoliageThread())
        return;

    FoliageSpawnIn in;
    if (!fillFoliageSpawnIn(in))
        return;

    m_foliageTrailGeneration = m_trailGeneration;
    std::vector<FoliageRecord> records = m_foliage;
    m_foliageCoverOnly = true;
    m_foliageApplyTrails = false;
    m_foliageSplat = false;
    m_foliageCancel.store(false);
    m_foliageDone.store(false);
    m_foliageOk = false;
    m_foliageProgress.store(0.0f);
    std::vector<FoliageRecord>().swap(m_foliagePending);
    m_foliagePendingCover.reset();
    setFoliagePhase("trails");
    m_foliageRunning.store(true);

    m_foliageThread = std::thread([this, in, records]() {
        FoliageCover cover;
        const bool ok = restampFoliageCover(in, records, cover);
        if (ok)
            m_foliagePendingCover = std::move(cover);
        m_foliageOk = ok;
        m_foliageDone.store(true);
    });
}

void EditorApp::startTrailGenerate()
{
    if (!acquireFoliageThread())
        return;

    FoliageSpawnIn in;
    if (!fillFoliageSpawnIn(in))
        return;

    TrailSet trailSet = m_trails;
    std::vector<FoliageRecord> records = m_foliage;
    m_foliageTrailGeneration = m_trailGeneration;
    m_foliageCoverOnly = true;
    m_foliageApplyTrails = true;
    m_foliageSplat = false;
    m_foliageCancel.store(false);
    m_foliageDone.store(false);
    m_foliageOk = false;
    m_foliageProgress.store(0.0f);
    std::vector<FoliageRecord>().swap(m_foliagePending);
    std::vector<TrailPath>().swap(m_foliagePendingTrails);
    m_foliagePendingSplat = SplatMap{};
    m_foliagePendingCover.reset();
    setFoliagePhase("trails");
    m_foliageRunning.store(true);

    m_foliageThread = std::thread([this, in, trailSet, records]() mutable {
        TrailGenerateIn gen;
        gen.height       = in.height;
        gen.splat        = in.splat;
        gen.water        = &in.water;
        gen.seaLevel     = in.seaLevel;
        gen.seed         = in.density.seed;
        gen.maxAutoPaths = trailSet.maxAutoPaths;
        gen.humanWidth   = trailSet.humanWidth;
        gen.animalWidth  = trailSet.animalWidth;
        gen.onProgress   = in.onProgress;
        gen.user         = in.user;
        FoliageCover cover;
        bool ok = generateGameTrails(gen, trailSet, nullptr);
        if (ok)
        {
            in.trails = trailSet.paths;
            ok = restampFoliageCover(in, records, cover);
        }
        if (ok)
        {
            m_foliagePendingTrails = std::move(trailSet.paths);
            m_foliagePendingCover = std::move(cover);
        }
        m_foliageOk = ok;
        m_foliageDone.store(true);
    });
}

void EditorApp::clearGeneratedTrails()
{
    if (!acquireFoliageThread())
        return;

    std::vector<TrailPath> kept;
    kept.reserve(m_trails.paths.size());
    bool removed = false;
    for (const TrailPath& path : m_trails.paths)
    {
        if (path.authored)
            kept.push_back(path);
        else
            removed = true;
    }
    if (!removed)
        return;
    m_trails.paths.swap(kept);
    if (m_trailSelected >= static_cast<int>(m_trails.paths.size()))
    {
        m_trailSelected = -1;
        m_trailPoint = -1;
    }
    noteTrailEdit();
    m_foliageStale = true;
    m_trailMeshDirty = true;
    startCoverRestamp();
}

void EditorApp::startTrailSplat()
{
    if (!acquireFoliageThread())
        return;

    HeightMap* height = m_terrain.editableWorking();
    SplatMap* splat = m_terrain.editableWorkingSplat();
    if (height == nullptr || splat == nullptr || !height->valid() || !splat->valid())
        return;

    SplatMap copy = *splat;
    std::vector<TrailPath> paths = m_trails.paths;
    m_foliageTrailGeneration = m_trailGeneration;
    m_foliageCoverOnly = false;
    m_foliageApplyTrails = false;
    m_foliageSplat = true;
    m_foliageCancel.store(false);
    m_foliageDone.store(false);
    m_foliageOk = false;
    m_foliageProgress.store(0.0f);
    std::vector<FoliageRecord>().swap(m_foliagePending);
    std::vector<TrailPath>().swap(m_foliagePendingTrails);
    m_foliagePendingSplat = SplatMap{};
    m_foliagePendingCover.reset();
    setFoliagePhase("splat");
    m_foliageRunning.store(true);

    m_foliageThread = std::thread([this, height, paths, copy = std::move(copy)]() mutable {
        const bool ok = stampTrailDirt(*height, copy, paths, &EditorApp::onFoliageProgress, this);
        if (ok)
            m_foliagePendingSplat = std::move(copy);
        m_foliageOk = ok;
        m_foliageDone.store(true);
    });
}

void EditorApp::noteTrailEdit()
{
    ++m_trailGeneration;
}

void EditorApp::clearTrailAuthoring()
{
    m_trails = {};
    noteTrailEdit();
    m_trailDraw = false;
    m_trailSelected = -1;
    m_trailPoint = -1;
    m_trailDragging = false;
    m_trailMoved = false;
    m_trailMeshDirty = true;
}

void EditorApp::finishTrailDraw()
{
    if (m_foliageRunning.load())
        return;
    bool dropped = false;
    if (m_trailSelected >= 0 && m_trailSelected < static_cast<int>(m_trails.paths.size()))
    {
        TrailPath& path = m_trails.paths[static_cast<size_t>(m_trailSelected)];
        if (path.points.size() < 2)
        {
            m_trails.paths.erase(m_trails.paths.begin() + m_trailSelected);
            dropped = true;
        }
        else
            path.authored = true;
    }
    // Enter is handled before mouse-up, which is what used to stamp this stroke.
    if (!dropped && m_trailGeneration != m_foliageTrailGeneration)
    {
        m_foliageStale = true;
        m_trailMeshDirty = true;
        startCoverRestamp();
    }
    m_trailSelected = -1;
    m_trailPoint = -1;
    m_trailDraw = false;
    m_trailDragging = false;
    m_trailMoved = false;
    m_trailMeshDirty = true;
    if (!dropped)
        return;
    noteTrailEdit();
    m_foliageStale = true;
    startCoverRestamp();
}

void EditorApp::deleteTrailPoint()
{
    if (m_foliageRunning.load())
        return;
    if (m_trailSelected < 0 || m_trailSelected >= static_cast<int>(m_trails.paths.size()))
        return;
    TrailPath& path = m_trails.paths[static_cast<size_t>(m_trailSelected)];
    if (m_trailPoint < 0 || m_trailPoint >= static_cast<int>(path.points.size()))
        return;
    path.points.erase(path.points.begin() + m_trailPoint);
    if (path.points.empty())
    {
        m_trails.paths.erase(m_trails.paths.begin() + m_trailSelected);
        m_trailSelected = -1;
        m_trailPoint = -1;
        m_trailDraw = false;
    }
    else if (m_trailPoint >= static_cast<int>(path.points.size()))
        m_trailPoint = static_cast<int>(path.points.size()) - 1;
    m_trailDragging = false;
    m_trailMoved = false;
    noteTrailEdit();
    m_foliageStale = true;
    m_trailMeshDirty = true;
    startCoverRestamp();
}

bool EditorApp::handleTrailKeys()
{
    if (m_playMode || m_sceneMode != SceneMode::Scene3D || !m_trailDraw)
        return false;
    // The job already copied the trails. Swallow the keys so object delete does not run either.
    if (m_foliageRunning.load())
        return true;

    bool used = false;
    if (input().keyPressed(Key::Enter))
    {
        finishTrailDraw();
        used = true;
    }
    if (input().actionPressed("delete") || input().keyPressed(Key::Delete))
    {
        deleteTrailPoint();
        used = true;
    }
    if (input().actionPressed("place"))
        used = true;
    return used;
}

void EditorApp::handleTrailPointer(bool uiMouse)
{
    if (!m_trailDraw || m_playMode || m_sceneMode != SceneMode::Scene3D)
        return;
    if (m_foliageRunning.load())
    {
        m_trailDragging = false;
        return;
    }

    if (input().mouseReleased(MouseButton::Left))
    {
        const bool edit = m_trailDragging && m_trailMoved;
        m_trailDragging = false;
        m_trailMoved = false;
        if (edit)
        {
            m_foliageStale = true;
            m_trailMeshDirty = true;
            startCoverRestamp();
        }
    }
    if (uiMouse)
        return;

    if (input().mousePressed(MouseButton::Left))
    {
        Vector3f hit;
        if (!groundHitFromMouse(hit))
            return;
        if (m_trailSelected < 0 || m_trailSelected >= static_cast<int>(m_trails.paths.size()))
        {
            TrailPath created;
            created.kind = TrailKind::Human;
            created.authored = true;
            created.width = clampTrailWidth(m_trails.humanWidth, kTrailHumanWidth);
            m_trails.paths.push_back(std::move(created));
            noteTrailEdit();
            m_trailSelected = static_cast<int>(m_trails.paths.size()) - 1;
            m_trailPoint = -1;
        }
        TrailPath& path = m_trails.paths[static_cast<size_t>(m_trailSelected)];
        int nearest = -1;
        float best = kTrailPickMetres * kTrailPickMetres;
        for (int i = 0; i < static_cast<int>(path.points.size()); ++i)
        {
            const float dx = path.points[static_cast<size_t>(i)].x - hit.x;
            const float dz = path.points[static_cast<size_t>(i)].z - hit.z;
            const float d2 = dx * dx + dz * dz;
            if (d2 <= best)
            {
                best = d2;
                nearest = i;
            }
        }
        if (nearest >= 0)
        {
            m_trailPoint = nearest;
            m_trailDragging = true;
            m_trailMoved = false;
        }
        else
        {
            path.points.push_back(TrailPoint{ hit.x, hit.z });
            noteTrailEdit();
            m_trailPoint = static_cast<int>(path.points.size()) - 1;
            m_trailDragging = true;
            m_trailMoved = true;
            m_trailMeshDirty = true;
        }
        m_dragging = false;
        m_gizmoDragAxis = TranslateGizmoAxis::None;
    }
    else if (m_trailDragging && input().mouseDown(MouseButton::Left))
    {
        Vector3f hit;
        if (!groundHitFromMouse(hit))
            return;
        if (m_trailSelected < 0 || m_trailSelected >= static_cast<int>(m_trails.paths.size()))
            return;
        TrailPath& path = m_trails.paths[static_cast<size_t>(m_trailSelected)];
        if (m_trailPoint < 0 || m_trailPoint >= static_cast<int>(path.points.size()))
            return;
        path.points[static_cast<size_t>(m_trailPoint)].x = hit.x;
        path.points[static_cast<size_t>(m_trailPoint)].z = hit.z;
        noteTrailEdit();
        m_trailMoved = true;
        m_trailMeshDirty = true;
    }
}

void EditorApp::drawTrailHeader()
{
    if (!ImGui::CollapsingHeader("Trails"))
        return;

    if (ImGui::Checkbox("Show paths", &m_showTrails))
        m_trailMeshDirty = true;

    if (ImGui::Button("Draw path"))
    {
        m_dragging = false;
        m_gizmoDragAxis = TranslateGizmoAxis::None;
        if (m_trailSelected < 0 || m_trailSelected >= static_cast<int>(m_trails.paths.size()))
        {
            TrailPath created;
            created.kind = TrailKind::Human;
            created.authored = true;
            created.width = clampTrailWidth(m_trails.humanWidth, kTrailHumanWidth);
            m_trails.paths.push_back(std::move(created));
            noteTrailEdit();
            m_trailSelected = static_cast<int>(m_trails.paths.size()) - 1;
            m_trailPoint = -1;
        }
        m_trailDraw = true;
        m_trailMeshDirty = true;
    }
    if (m_trailDraw)
    {
        ImGui::SameLine();
        if (ImGui::Button("Stop drawing"))
            finishTrailDraw();
    }
    if (ImGui::Button("Generate game trails"))
        startTrailGenerate();
    if (ImGui::Button("Clear generated"))
        clearGeneratedTrails();
    if (ImGui::Button("Re-stamp cover"))
        startCoverRestamp();
    if (ImGui::Button("Stamp trails into splat"))
        startTrailSplat();
    ImGui::SetItemTooltip("Paints dirt along the trails. A grid scene writes it on the next save. World Engine imports still do not write the splat back; the paint lasts for this session only.");

    for (int i = 0; i < static_cast<int>(m_trails.paths.size()); ++i)
    {
        const TrailPath& path = m_trails.paths[static_cast<size_t>(i)];
        char label[96];
        std::snprintf(label, sizeof(label), "%s  %s  %d pts##trail%d",
            path.kind == TrailKind::Animal ? "Animal" : "Human",
            path.authored ? "authored" : "generated",
            static_cast<int>(path.points.size()),
            i);
        if (ImGui::Selectable(label, m_trailSelected == i))
        {
            m_trailSelected = i;
            m_trailPoint = path.points.empty() ? -1 : 0;
            m_trailMeshDirty = true;
        }
    }

    if (m_trailSelected < 0 || m_trailSelected >= static_cast<int>(m_trails.paths.size()))
        return;

    TrailPath& path = m_trails.paths[static_cast<size_t>(m_trailSelected)];
    int kind = path.kind == TrailKind::Animal ? 1 : 0;
    if (ImGui::Combo("Kind", &kind, "Human\0Animal\0"))
    {
        path.kind = kind == 1 ? TrailKind::Animal : TrailKind::Human;
        const float stored = path.kind == TrailKind::Animal ? m_trails.animalWidth : m_trails.humanWidth;
        const float def = path.kind == TrailKind::Animal ? kTrailAnimalWidth : kTrailHumanWidth;
        path.width = clampTrailWidth(stored, def);
        noteTrailEdit();
        m_foliageStale = true;
        m_trailMeshDirty = true;
        startCoverRestamp();
    }

    const float fallback = path.kind == TrailKind::Animal ? kTrailAnimalWidth : kTrailHumanWidth;
    if (ImGui::SliderFloat("Width", &path.width, 0.05f, kTrailWidthMax, "%.2f"))
    {
        path.width = clampTrailWidth(path.width, fallback);
        noteTrailEdit();
        m_foliageStale = true;
        m_trailMeshDirty = true;
    }
    if (ImGui::IsItemDeactivatedAfterEdit())
        startCoverRestamp();
    if (ImGui::Checkbox("Closed", &path.closed))
    {
        noteTrailEdit();
        m_foliageStale = true;
        m_trailMeshDirty = true;
        startCoverRestamp();
    }
    if (m_trailPoint >= 0 && ImGui::Button("Delete point"))
    {
        deleteTrailPoint();
        return;
    }
    if (ImGui::Button("Deselect"))
    {
        m_trailSelected = -1;
        m_trailPoint = -1;
        m_trailMeshDirty = true;
    }
}

void EditorApp::rebuildTrailMesh()
{
    if (!m_trailMeshDirty)
        return;

    LineMeshData center;
    LineMeshData ribbon;
    LineMeshData picks;
    const bool draw = m_showTrails && m_haveTerrain && m_terrain.valid() && !m_trails.paths.empty();
    if (draw)
    {
    int centerBudget = kTrailMeshVerts;
    int ribbonBudget = kTrailMeshVerts;
    int pickBudget = kTrailMeshVerts;

    auto lift = [&](float x, float z) {
        return Vector3f(x, m_terrain.heightAtWorld(x, z) + kTrailLiftMetres, z);
    };

    for (int pathIndex = 0; pathIndex < static_cast<int>(m_trails.paths.size()); ++pathIndex)
    {
        const TrailPath& path = m_trails.paths[static_cast<size_t>(pathIndex)];
        const bool selected = pathIndex == m_trailSelected;
        for (int pointIndex = 0; pointIndex < static_cast<int>(path.points.size()); ++pointIndex)
        {
            const TrailPoint& point = path.points[static_cast<size_t>(pointIndex)];
            const float arm = (selected && pointIndex == m_trailPoint) ? 0.6f : 0.35f;
            pushCross(picks, lift(point.x, point.z), arm, pickBudget);
        }
        if (path.points.size() < 2)
            continue;

        std::vector<Vector2f> controls;
        controls.reserve(path.points.size());
        for (const TrailPoint& point : path.points)
            controls.emplace_back(point.x, point.z);
        std::vector<Vector2f> samples;
        sampleTrailPolyline(controls, path.closed, samples);
        const float half = clampTrailWidth(path.width, path.kind == TrailKind::Animal ? kTrailAnimalWidth : kTrailHumanWidth) * 0.5f;
        for (size_t i = 1; i < samples.size(); ++i)
        {
            const Vector2f delta = samples[i] - samples[i - 1];
            const float mag = delta.Magnitude();
            if (mag < 1.0e-4f)
                continue;
            const Vector2f side = delta.Perpendicular() * (half / mag);
            const Vector3f a = lift(samples[i - 1].x, samples[i - 1].y);
            const Vector3f b = lift(samples[i].x, samples[i].y);
            pushLine(center, a, b, centerBudget);
            pushLine(ribbon, lift(samples[i - 1].x + side.x, samples[i - 1].y + side.y), lift(samples[i].x + side.x, samples[i].y + side.y), ribbonBudget);
            pushLine(ribbon, lift(samples[i - 1].x - side.x, samples[i - 1].y - side.y), lift(samples[i].x - side.x, samples[i].y - side.y), ribbonBudget);
        }
    }
    }

    LineMesh nextCenter;
    LineMesh nextRibbon;
    LineMesh nextPick;
    if (!createLines(renderer(), center, nextCenter) || !createLines(renderer(), ribbon, nextRibbon) || !createLines(renderer(), picks, nextPick))
    {
        if (nextCenter.valid())
            nextCenter.deferRelease(renderer());
        if (nextRibbon.valid())
            nextRibbon.deferRelease(renderer());
        if (nextPick.valid())
            nextPick.deferRelease(renderer());
        return;
    }

    auto install = [&](LineMesh& slot, LineMesh& next) {
        if (slot.valid())
            slot.deferRelease(renderer());
        slot = std::move(next);
    };
    install(m_trailCenter, nextCenter);
    install(m_trailRibbon, nextRibbon);
    install(m_trailPick, nextPick);
    m_trailMeshDirty = false;
}

void EditorApp::drawTrailOverlay(ID3D12GraphicsCommandList* cmd, const Matrix4f& viewProj)
{
    if (!cmd || !m_showTrails || !m_haveTerrain)
        return;
    if (!m_trailCenter.valid() && !m_trailRibbon.valid() && !m_trailPick.valid())
        return;
    LinePipeline& lines = m_linePipeline3D.isValid() ? m_linePipeline3D : m_linePipeline;
    if (!lines.isValid())
        return;

    lines.bind(cmd);
    auto draw = [&](LineMesh& mesh, float r, float g, float b) {
        if (!mesh.valid())
            return;
        LineFrameConstants lc{};
        copyMatrix(lc.worldViewProj, viewProj);
        lc.color[0] = r;
        lc.color[1] = g;
        lc.color[2] = b;
        lc.color[3] = 1.0f;
        lines.setConstants(cmd, lc);
        mesh.draw(cmd);
    };
    draw(m_trailRibbon, 0.45f, 0.32f, 0.18f);
    draw(m_trailCenter, 0.92f, 0.72f, 0.28f);
    draw(m_trailPick, 0.95f, 0.95f, 0.95f);
}
