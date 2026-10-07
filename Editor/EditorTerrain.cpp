#include "EditorApp.h"

#include "Assets/Image.h"
#include "Core/Log.h"
#include "Editor/EditorFileDialog.h"
#include "Editor/EditorInternals.h"
#include "Math/MathHelper.h"
#include "Scene/SceneFile.h"
#include "Terrain/FoliageFile.h"
#include "Terrain/FoliageSpawn.h"
#include "Terrain/TerrainTileFile.h"
#include "Terrain/WorldEngineMap.h"
#include "Water/WaterWaves.h"
#include "Render/WaterPipeline.h"
#include "Terrain/HeightMap.h"
#include "Ui/Icons.h"
#include "ECS/Components.h"

#include <imgui.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <commdlg.h>
#include <shlobj.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace Dark;
using namespace Math;
using namespace Terrain;
using Microsoft::WRL::ComPtr;

namespace
{

const char* kLayerNames[Terrain::kMaxTerrainLayers] = { "Dirt", "Grass", "Rock", "Snow" };

bool pickFolder(HWND owner, std::filesystem::path& out)
{
    BROWSEINFOW bi{};
    bi.hwndOwner = owner;
    bi.lpszTitle = L"Select a World Engine terrain folder";
    bi.ulFlags   = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
    if (!pidl)
        return false;
    wchar_t path[MAX_PATH];
    path[0] = 0;
    const bool ok = SHGetPathFromIDListW(pidl, path) != FALSE;
    CoTaskMemFree(pidl);
    if (!ok || path[0] == 0)
        return false;
    out = std::filesystem::path(path);
    return true;
}

bool pickImagePath(HWND owner, std::filesystem::path& out)
{
    wchar_t file[MAX_PATH];
    file[0] = 0;

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = owner;
    ofn.lpstrFilter = L"Images (*.png;*.jpg;*.jpeg;*.bmp)\0*.png;*.jpg;*.jpeg;*.bmp\0PNG (*.png)\0*.png\0JPEG (*.jpg;*.jpeg)\0*.jpg;*.jpeg\0All files (*.*)\0*.*\0";
    ofn.lpstrFile   = file;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrDefExt = L"png";
    ofn.Flags       = OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_HIDEREADONLY | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    ofn.lpstrTitle  = L"Load Terrain Map";

    if (!GetOpenFileNameW(&ofn))
        return false;
    out = std::filesystem::path(file);
    return true;
}

AssetRef<Image> loadTerrainMapImage(AssetManager& assets, const std::filesystem::path& path)
{
    const std::string virt = assets.virtualPathFromAbsolute(path);
    AssetRef<Image>   img  = virt.empty() ? assets.loadImageFile(path) : assets.loadImage(virt);
    if (!img || !img->valid())
        return {};
    return img;
}

std::string terrainMapKey(AssetManager& assets, const std::filesystem::path& path)
{
    const std::string virt = assets.virtualPathFromAbsolute(path);
    return virt.empty() ? path.generic_string() : virt;
}

bool saveRgbaPng(const std::filesystem::path& path, uint32_t width, uint32_t height, const uint8_t* rgba)
{
    if (!rgba || width == 0 || height == 0)
        return false;

    std::error_code ec;
    if (path.has_parent_path())
    {
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec)
        {
            DE_LOG_ERROR("Editor: failed to create directory for '{}' ({})", path.string(), ec.message());
            return false;
        }
    }

    const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    (void)co;

    ComPtr<IWICImagingFactory> factory;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (FAILED(hr) || !factory)
    {
        DE_LOG_ERROR("Editor: WIC factory failed (HRESULT 0x{:08X})", static_cast<unsigned>(hr));
        return false;
    }

    ComPtr<IWICStream> stream;
    hr = factory->CreateStream(&stream);
    if (FAILED(hr) || !stream)
        return false;
    hr = stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE);
    if (FAILED(hr))
    {
        DE_LOG_ERROR("Editor: failed to open '{}' for splat write", path.string());
        return false;
    }

    ComPtr<IWICBitmapEncoder> encoder;
    hr = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
    if (FAILED(hr) || !encoder)
        return false;
    hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    if (FAILED(hr))
        return false;

    ComPtr<IWICBitmapFrameEncode> frame;
    hr = encoder->CreateNewFrame(&frame, nullptr);
    if (FAILED(hr) || !frame)
        return false;
    hr = frame->Initialize(nullptr);
    if (FAILED(hr))
        return false;
    hr = frame->SetSize(width, height);
    if (FAILED(hr))
        return false;
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppRGBA;
    hr = frame->SetPixelFormat(&format);
    if (FAILED(hr))
        return false;

    const UINT stride = width * 4u;
    const UINT bytes  = stride * height;
    hr = frame->WritePixels(height, stride, bytes, const_cast<BYTE*>(rgba));
    if (FAILED(hr))
    {
        DE_LOG_ERROR("Editor: splat PNG write failed (HRESULT 0x{:08X})", static_cast<unsigned>(hr));
        return false;
    }
    hr = frame->Commit();
    if (FAILED(hr))
        return false;
    hr = encoder->Commit();
    if (FAILED(hr))
        return false;
    return true;
}

std::filesystem::path sidecarPath(const std::filesystem::path& scenePath, const std::string& file)
{
    if (file.empty())
        return {};
    const std::filesystem::path p(file);
    if (p.is_absolute())
        return p;
    const std::filesystem::path parent = scenePath.has_parent_path() ? scenePath.parent_path() : std::filesystem::path{};
    return parent / p;
}

void defaultSidecarNames(const std::string& sceneName, std::string& heightFile, std::string& splatFile)
{
    const std::string stem = sceneName.empty() ? std::string("untitled") : sceneName;
    if (heightFile.empty())
        heightFile = stem + ".height.bin";
    if (splatFile.empty())
        splatFile = stem + ".splat.png";
}

void defaultGridSidecarNames(const std::string& sceneName, std::string& coarseFile, std::string& tileDir, std::string& heightFile, std::string& splatFile)
{
    const std::string stem = sceneName.empty() ? std::string("untitled") : sceneName;
    if (coarseFile.empty())
        coarseFile = stem + ".coarse.height.bin";
    if (tileDir.empty())
        tileDir = stem + ".tiles";
    defaultSidecarNames(stem, heightFile, splatFile);
}

constexpr int kGenTileCounts[] = { 1, 2, 4, 8 };

} // namespace

void EditorApp::removeEditorTerrain()
{
    Physics::destroyFoliageCollision(m_physics, m_foliageCollision);
    cancelFoliageSpawn();
    clearFoliageInstances("remove terrain");
    m_terrainSource.clear();
    if (!m_haveTerrain && !m_terrainMaterial.isValid())
    {
        m_terrain.clear();
        m_terrainMaterial    = {};
        m_splat              = {};
        m_terrainSurface     = {};
        m_haveTerrain        = false;
        m_terrainHeightDirty = false;
        m_terrainSplatDirty  = false;
        return;
    }

    renderer().waitForGpu();
    {
        const WaterParams kept = m_water.params();
        m_water = WaterWorld{};
        m_water.params() = defaultWaterParams(0.0f);
        m_water.params().amplitudeScale = kept.amplitudeScale;
        m_water.params().speedScale     = kept.speedScale;
        m_water.params().foam           = kept.foam;
        m_water.params().flowSpeed      = kept.flowSpeed;
        m_water.params().foamWidthScale = kept.foamWidthScale;
        m_water.params().detailAmount   = kept.detailAmount;
    }
    m_placedWaterForce = true;
    m_terrain.clear();
    m_terrainMaterial    = {};
    m_splat              = {};
    m_terrainSurface     = {};
    m_haveTerrain        = false;
    m_terrainHeightDirty = false;
    m_terrainSplatDirty  = false;
    for (int i = 0; i < Terrain::kMaxTerrainLayers; ++i)
    {
        m_terrainAlbedoPath[i].clear();
        m_terrainNormalPath[i].clear();
        m_terrainOrmPath[i].clear();
        m_terrainSurface.albedo[i].reset();
        m_terrainSurface.normal[i].reset();
        m_terrainSurface.orm[i].reset();
    }
}

bool EditorApp::terrainBrushesLocked() const
{
    return m_foliageRunning.load();
}

void EditorApp::bindTerrainHeightSrv()
{
    if (m_terrain.heightTexture().valid())
    {
        renderer().setHeightSrv(m_terrain.heightTexture().cpuHandle());
        m_scene.waterPipeline().setHeightSrv(renderer().device(), m_terrain.heightTexture().cpuHandle());
    }
}

bool EditorApp::rebuildEditorWater()
{
    const WaterParams kept = m_water.params();
    m_water = WaterWorld{};
    m_water.params() = defaultWaterParams(m_terrainSeaLevel);
    m_water.params().amplitudeScale = kept.amplitudeScale;
    m_water.params().speedScale     = kept.speedScale;
    m_water.params().foam           = kept.foam;
    m_water.params().flowSpeed      = kept.flowSpeed;
    m_water.params().foamWidthScale = kept.foamWidthScale;
    m_water.params().detailAmount   = kept.detailAmount;
    m_placedWaterForce = true;
    return true;
}

bool EditorApp::createEditorWaterSheet()
{
    if (!m_authoredWater.present || !m_haveTerrain || !m_terrain.valid() || !m_terrain.coarse().valid())
        return false;

    const AABox3f terrainBox = m_terrain.bounds();
    const float level = m_authoredWater.hasLevel
        ? m_authoredWater.level
        : Lerp(terrainBox.Min.y, terrainBox.Max.y, m_authoredWater.levelFraction);

    WaterParams params = m_water.params();
    params.waterLevel = level;

    WaterDesc desc;
    desc.chunkCells = m_authoredWater.chunkCells > 0 ? m_authoredWater.chunkCells : 16;
    desc.waterLevel = level;
    const int lodCount = m_authoredWater.lodDistanceCount < 1
        ? 1
        : (m_authoredWater.lodDistanceCount > Terrain::kMaxLodLevels ? Terrain::kMaxLodLevels : m_authoredWater.lodDistanceCount);
    desc.lodDistanceCount = lodCount;
    for (int i = 0; i < lodCount; ++i)
        desc.lodDistances[i] = m_authoredWater.lodDistances[i];
    desc.params = params;

    const float time = m_water.time();
    if (!m_water.create(m_terrain.coarse(), desc))
    {
        DE_LOG_ERROR(LogCategory::Render, "Editor: water sheet create failed");
        m_water = WaterWorld{};
        m_water.params() = params;
        m_water.setTime(time);
        m_placedWaterForce = true;
        return false;
    }
    m_water.setTime(time);
    m_water.updateLod(m_camera.GetPosition());
    if (!m_water.createGpu(renderer()))
    {
        DE_LOG_ERROR(LogCategory::Render, "Editor: water sheet GPU upload failed");
        m_water = WaterWorld{};
        m_water.params() = params;
        m_water.setTime(time);
        m_placedWaterForce = true;
        return false;
    }
    m_placedWaterForce = true;
    return true;
}

void EditorApp::syncPlacedWater(bool terrainChanged)
{
    m_placedWaterRetire.tick();
    const bool haveTerrain = m_haveTerrain && m_terrain.valid() && m_terrain.coarse().valid();
    const Terrain::HeightMap* heightMap = haveTerrain ? &m_terrain.coarse() : nullptr;
    const bool force = terrainChanged || m_placedWaterForce;
    m_placedWaterForce = false;

    std::vector<EntityID> live;
    world().each<EditorObjectComponent>([&](Entity e, EditorObjectComponent& so) {
        if (so.type != SceneObjectType::Water)
            return;
        const TransformComponent* xf = world().get<TransformComponent>(e);
        if (!xf)
            return;
        live.push_back(e.id());

        EditorWaterSlot* slot = nullptr;
        for (EditorWaterSlot& candidate : m_placedWater)
        {
            if (candidate.entityId == e.id())
            {
                slot = &candidate;
                break;
            }
        }
        if (!slot)
        {
            m_placedWater.push_back(EditorWaterSlot{});
            slot = &m_placedWater.back();
            slot->entityId = e.id();
        }

        WaterBodyDesc desc;
        desc.center  = xf->position;
        desc.extentX = xf->scale.x;
        desc.extentZ = xf->scale.z;
        const bool rebuild = force || !slot->body.matches(desc) || slot->body.bakedTerrain() != haveTerrain;
        if (rebuild)
        {
            slot->body.retireGpu(m_placedWaterRetire);
            WaterParams params = m_water.params();
            params.waterLevel = desc.center.y;
            if (!slot->body.build(heightMap, desc, params) || !slot->body.upload(renderer()))
                DE_LOG_ERROR(LogCategory::Render, "Editor: water body upload failed");
            else
            {
                slot->body.updateLod(m_camera.GetPosition());
                if (slot->body.needsRebuild())
                {
                    slot->body.rebuildDirtyCpuMeshes();
                    if (!slot->body.upload(renderer()))
                        DE_LOG_ERROR(LogCategory::Render, "Editor: water body upload failed");
                }
            }
        }
        else
        {
            slot->body.applySharedParams(m_water.params());
            slot->body.updateLod(m_camera.GetPosition());
            if (slot->body.needsRebuild())
            {
                slot->body.rebuildDirtyCpuMeshes();
                if (!slot->body.upload(renderer()))
                    DE_LOG_ERROR(LogCategory::Render, "Editor: water body upload failed");
            }
        }
    });

    for (size_t i = 0; i < m_placedWater.size();)
    {
        bool found = false;
        for (EntityID id : live)
        {
            if (id == m_placedWater[i].entityId)
            {
                found = true;
                break;
            }
        }
        if (found)
        {
            ++i;
            continue;
        }
        m_placedWater[i].body.retireGpu(m_placedWaterRetire);
        if (i + 1 != m_placedWater.size())
            m_placedWater[i] = std::move(m_placedWater.back());
        m_placedWater.pop_back();
    }

    const float lakeLevel = m_water.chunksX() > 0 ? m_water.params().waterLevel : m_terrainSeaLevel;
    std::vector<EntityID> liveStreams;
    world().each<EditorObjectComponent>([&](Entity e, EditorObjectComponent& so) {
        if (so.type != SceneObjectType::Stream)
            return;
        StreamComponent* stream = world().get<StreamComponent>(e);
        TransformComponent* xf = world().get<TransformComponent>(e);
        if (!stream || !xf || stream->points.size() < 2)
            return;
        liveStreams.push_back(e.id());

        if (stream->snapToPoints)
        {
            xf->position.x = stream->points[0].x;
            xf->position.z = stream->points[0].y;
            stream->snapToPoints = false;
        }
        else
        {
            const float dx = xf->position.x - stream->points[0].x;
            const float dz = xf->position.z - stream->points[0].y;
            if (dx * dx + dz * dz > 1.0e-6f)
            {
                for (Vector2f& point : stream->points)
                {
                    point.x += dx;
                    point.y += dz;
                }
            }
            xf->position.x = stream->points[0].x;
            xf->position.z = stream->points[0].y;
        }
        if (heightMap && heightMap->valid())
        {
            const Vector3f normal = heightMap->normalAtWorld(stream->points[0].x, stream->points[0].y);
            xf->position.y = heightMap->heightAtWorld(stream->points[0].x, stream->points[0].y) + normal.y * 0.45f;
        }

        EditorStreamSlot* slot = nullptr;
        for (EditorStreamSlot& candidate : m_editorStreams)
        {
            if (candidate.entityId == e.id())
            {
                slot = &candidate;
                break;
            }
        }
        if (!slot)
        {
            m_editorStreams.push_back(EditorStreamSlot{});
            slot = &m_editorStreams.back();
            slot->entityId = e.id();
        }

        bool samePoints = slot->builtPoints.size() == stream->points.size();
        if (samePoints)
        {
            for (size_t p = 0; p < stream->points.size(); ++p)
            {
                const float px = slot->builtPoints[p].x - stream->points[p].x;
                const float pz = slot->builtPoints[p].y - stream->points[p].y;
                if (px * px + pz * pz > 1.0e-6f)
                {
                    samePoints = false;
                    break;
                }
            }
        }
        const bool inputsChanged = !samePoints || fabsf(slot->builtWidth - stream->width) > 1.0e-3f;
        const bool rebuild = force || inputsChanged || (heightMap && !slot->mesh.valid());
        if (!rebuild)
            return;
        m_placedWaterRetire.push(std::move(slot->mesh));
        slot->builtWidth = stream->width;
        slot->builtPoints = stream->points;
        if (!heightMap)
        {
            DE_LOG_ERROR(LogCategory::Render, "Editor: stream has no height map");
            return;
        }
        StreamDesc desc;
        desc.width        = stream->width;
        desc.flowSpeed    = stream->flowSpeed;
        desc.bedClearance = 0.45f;
        desc.pointsXZ     = stream->points;
        MeshData cpu;
        std::string err;
        if (!buildStreamRibbon(*heightMap, desc, lakeLevel, cpu, &slot->bounds, &err) || !Mesh::tryCreate(renderer(), cpu, slot->mesh))
            DE_LOG_ERROR(LogCategory::Render, "Editor: stream upload failed");
    });

    for (size_t i = 0; i < m_editorStreams.size();)
    {
        bool found = false;
        for (EntityID id : liveStreams)
        {
            if (id == m_editorStreams[i].entityId)
            {
                found = true;
                break;
            }
        }
        if (found)
        {
            ++i;
            continue;
        }
        m_placedWaterRetire.push(std::move(m_editorStreams[i].mesh));
        if (i + 1 != m_editorStreams.size())
            m_editorStreams[i] = std::move(m_editorStreams.back());
        m_editorStreams.pop_back();
    }
}

void EditorApp::drawPlacedWater(
    ID3D12GraphicsCommandList* cmd,
    const Frustum3f& frustum,
    ID3D12DescriptorHeap* heightHeap,
    D3D12_GPU_DESCRIPTOR_HANDLE heightGpu,
    D3D12_CPU_DESCRIPTOR_HANDLE sceneColorCpu,
    D3D12_CPU_DESCRIPTOR_HANDLE depthCpu,
    const SsrSettings* ssr)
{
    const bool sheet = m_water.wetChunkCount() > 0;
    if (!cmd || !m_scene.waterPipeline().isValid() || (!sheet && m_placedWater.empty() && m_editorStreams.empty()))
        return;
    const Terrain::HeightMap* heightMap = (m_haveTerrain && m_terrain.valid() && m_terrain.coarse().valid()) ? &m_terrain.coarse() : nullptr;
    uint32_t drawIndex = 0;
    auto pastCap = [&]() {
        if (drawIndex < WaterPipeline::kMaxWaterDrawsPerFrame)
            return false;
        DE_LOG_WARN(LogCategory::Render, "Editor: water draw cap {} reached", WaterPipeline::kMaxWaterDrawsPerFrame);
        return true;
    };
    if (sheet)
    {
        m_water.draw(
            cmd,
            m_scene.waterPipeline(),
            m_camera,
            &frustum,
            &m_env,
            &renderer().debugState(),
            0,
            0,
            nullptr,
            renderer().frameIndex(),
            heightHeap,
            heightGpu,
            &m_shadows,
            sceneColorCpu,
            depthCpu,
            ssr,
            drawIndex);
        drawIndex = 1;
    }
    for (const EditorWaterSlot& slot : m_placedWater)
    {
        if (pastCap())
            break;
        if (slot.body.draw(
                cmd,
                m_scene.waterPipeline(),
                m_camera,
                &frustum,
                &renderer().debugState(),
                m_water.time(),
                renderer().frameIndex(),
                drawIndex,
                heightHeap,
                heightGpu,
                &m_shadows,
                sceneColorCpu,
                depthCpu,
                ssr,
                heightMap,
                &m_env))
            ++drawIndex;
    }
    for (const EditorStreamSlot& slot : m_editorStreams)
    {
        if (pastCap())
            break;
        float flowSpeed = 1.6f;
        if (const StreamComponent* stream = world().get<StreamComponent>(Entity(slot.entityId)))
            flowSpeed = stream->flowSpeed;
        WaterParams params = m_water.params();
        params.flowSpeed = flowSpeed;
        if (drawStreamRibbon(
                cmd,
                m_scene.waterPipeline(),
                slot.mesh,
                slot.bounds,
                params,
                m_camera,
                &frustum,
                &renderer().debugState(),
                m_water.time(),
                renderer().frameIndex(),
                drawIndex,
                heightHeap,
                heightGpu,
                &m_shadows,
                sceneColorCpu,
                depthCpu,
                ssr,
                heightMap,
                &m_env))
            ++drawIndex;
    }
}

bool EditorApp::applyEditorGridGpu()
{
    if (!m_haveTerrain || !m_terrain.valid())
        return false;
    m_terrain.updateStreaming(m_camera.GetPosition(), &renderer(), &m_terrainMaterial);
    if (!m_terrain.uploadCoarseHeightTexture(renderer()))
    {
        DE_LOG_ERROR(LogCategory::Render, "Editor: coarse height texture upload failed");
        return false;
    }
    bindTerrainHeightSrv();
    m_terrainHeightDirty = false;
    m_terrainSplatDirty  = false;
    return true;
}

bool EditorApp::uploadTerrainSplatGpu()
{
    if (!m_haveTerrain || !m_terrain.valid())
        return false;
    HeightMap* working = m_terrain.editableWorking();
    SplatMap*  splat   = m_terrain.editableWorkingSplat();
    if (!working || !working->valid() || !splat || !splat->valid())
        return false;
    m_terrain.applyWorkingRect(0, 0, static_cast<int>(working->width()) - 1, static_cast<int>(working->height()) - 1, false, true);
    m_terrain.rebindResidentHeaps(renderer(), m_terrainMaterial);
    m_terrainMaterial.setLayerSamplingRaw(renderer().device(), renderer().debugState().legacyUnormAlbedo);
    m_terrainSplatDirty = false;
    return true;
}

bool EditorApp::rebuildTerrainGpuFromSurface()
{
    if (!m_haveTerrain)
        return false;
    if (!m_terrainMaterial.applySurfaceDesc(renderer(), m_terrainSurface))
    {
        DE_LOG_ERROR(LogCategory::Render, "Editor: terrain surface pack failed");
        return false;
    }
    m_terrainMaterial.setLayerSamplingRaw(renderer().device(), renderer().debugState().legacyUnormAlbedo);
    m_terrain.rebindResidentHeaps(renderer(), m_terrainMaterial);
    return true;
}

void EditorApp::syncTerrainLod()
{
    pollFoliageSpawn();
    if (!m_haveTerrain || !m_terrain.valid())
        return;

    if (m_playMode && m_playPlayer.valid())
    {
        if (const TransformComponent* xf = world().get<TransformComponent>(m_playPlayer))
            m_terrain.pinWorldXZ(xf->position.x, xf->position.z, 3);
    }
    else
        m_terrain.clearPin();

    m_terrain.updateStreaming(m_camera.GetPosition(), &renderer(), &m_terrainMaterial);
    if (m_terrainHeightDirty)
    {
        if (!m_terrain.uploadCoarseHeightTexture(renderer()))
            DE_LOG_ERROR(LogCategory::Render, "Editor: coarse height texture upload failed");
        else
            bindTerrainHeightSrv();
        m_terrainHeightDirty = false;
    }
    if (m_terrainSplatDirty)
        m_terrainSplatDirty = false;

    if (m_water.chunksX() > 0)
    {
        m_water.updateLod(m_camera.GetPosition());
        if (m_water.needsRebuild())
        {
            m_water.rebuildDirtyCpuMeshes();
            if (!m_water.uploadDirty(renderer()))
                DE_LOG_ERROR(LogCategory::Render, "Editor: water sheet upload failed");
        }
    }
}

void EditorApp::fillTerrainSceneDesc(SceneFileData& data) const
{
    data.hasTerrain = false;
    if (!m_haveTerrain || !m_terrain.valid() || data.mode == SceneMode::Scene2D)
        return;

    TerrainSceneDesc desc{};
    desc.bindLayout     = TerrainSceneDesc::kBindLayoutV1;
    desc.chunkCells     = m_terrain.chunkCells() > 0 ? m_terrain.chunkCells() : 16;
    if (!m_terrainSource.empty())
    {
        desc.source         = m_terrainSource;
        desc.worldSize      = m_worldEngineSize;
        desc.importHeight   = m_worldEngineHeight;
        desc.heightBlendK   = Terrain::kWorldEngineMacroBlend;
        desc.heightBlendT   = m_terrainMaterial.params().heightBlendT;
        desc.triplanarSlope = m_terrainMaterial.params().triplanarSlope;
        desc.hasGrid        = false;
        desc.foliage.present           = true;
        desc.foliage.seed              = m_foliageDensity.seed;
        desc.foliage.dirtTreesPerM2    = m_foliageDensity.dirtTreesPerM2;
        desc.foliage.dirtFlowersPerM2  = m_foliageDensity.dirtFlowersPerM2;
        desc.foliage.grassTreesPerM2   = m_foliageDensity.grassTreesPerM2;
        desc.foliage.grassFlowersPerM2 = m_foliageDensity.grassFlowersPerM2;
        desc.foliage.rockPerM2         = m_foliageDensity.rockPerM2;
        desc.foliage.grassPerM2        = m_foliageDensity.grassPerM2;
        desc.foliage.stale             = m_foliageStale;
        desc.foliage.treeModel         = m_foliageDensity.treeModel;
        desc.foliage.flowerModel       = m_foliageDensity.flowerModel;
        desc.foliage.rockModel         = m_foliageDensity.rockModel;
        desc.foliage.grassModel        = m_foliageDensity.grassModel;
        data.hasTerrain = true;
        data.terrain    = std::move(desc);
        return;
    }
    desc.heightBlendK   = m_terrainMaterial.params().heightBlendK;
    desc.heightBlendT   = m_terrainMaterial.params().heightBlendT;
    desc.triplanarSlope = m_terrainMaterial.params().triplanarSlope;
    desc.heightFile     = m_terrainHeightFile;
    desc.splatFile      = m_terrainSplatFile;
    defaultGridSidecarNames(data.name, desc.grid.coarseFile, desc.grid.tileDir, desc.heightFile, desc.splatFile);
    const uint64_t tileCount = static_cast<uint64_t>(m_terrain.tilesX()) * m_terrain.tilesZ();
    desc.hasGrid            = true;
    desc.grid.tilesX        = m_terrain.tilesX();
    desc.grid.tilesZ        = m_terrain.tilesZ();
    desc.grid.tileCells     = static_cast<uint32_t>(m_terrain.tileCells());
    desc.grid.cellSize      = m_terrain.cellSize();
    desc.grid.origin        = m_terrain.origin();
    desc.grid.heightScale   = m_terrain.heightScale();
    desc.grid.seed          = m_terrainSeed;
    desc.grid.coarseFile    = m_terrainCoarseFile.empty() ? desc.grid.coarseFile : m_terrainCoarseFile;
    desc.grid.tileDir       = m_terrainTileDir.empty() ? desc.grid.tileDir : m_terrainTileDir;
    desc.grid.seaLevel      = m_terrainSeaLevel;
    desc.grid.residentRing  = m_terrain.residentRing();
    if (tileCount > 1ull)
    {
        desc.heightFile.clear();
        desc.splatFile.clear();
    }
    desc.layerCount = Terrain::kMaxTerrainLayers;
    for (int i = 0; i < Terrain::kMaxTerrainLayers; ++i)
    {
        desc.layers[i].albedo = m_terrainAlbedoPath[i];
        desc.layers[i].normal = m_terrainNormalPath[i];
        desc.layers[i].orm    = m_terrainOrmPath[i];
        desc.layers[i].tiling = m_terrainMaterial.layer(i).tiling;
        std::memcpy(desc.layers[i].tint, m_terrainMaterial.layer(i).tint, sizeof(desc.layers[i].tint));
    }
    desc.foliage.present           = true;
    desc.foliage.seed              = m_foliageDensity.seed;
    desc.foliage.dirtTreesPerM2    = m_foliageDensity.dirtTreesPerM2;
    desc.foliage.dirtFlowersPerM2  = m_foliageDensity.dirtFlowersPerM2;
    desc.foliage.grassTreesPerM2   = m_foliageDensity.grassTreesPerM2;
    desc.foliage.grassFlowersPerM2 = m_foliageDensity.grassFlowersPerM2;
    desc.foliage.rockPerM2         = m_foliageDensity.rockPerM2;
    desc.foliage.grassPerM2        = m_foliageDensity.grassPerM2;
    desc.foliage.stale             = m_foliageStale;
    desc.foliage.treeModel         = m_foliageDensity.treeModel;
    desc.foliage.flowerModel       = m_foliageDensity.flowerModel;
    desc.foliage.rockModel         = m_foliageDensity.rockModel;
    desc.foliage.grassModel        = m_foliageDensity.grassModel;
    data.hasTerrain = true;
    data.terrain    = std::move(desc);
}

bool EditorApp::saveTerrainSidecars(const std::filesystem::path& scenePath) const
{
    if (!m_terrainSource.empty())
        return true;
    if (!m_haveTerrain || !m_terrain.valid())
        return true;

    std::string coarseFile = m_terrainCoarseFile;
    std::string tileDir    = m_terrainTileDir;
    std::string heightFile = m_terrainHeightFile;
    std::string splatFile  = m_terrainSplatFile;
    defaultGridSidecarNames(m_sceneName, coarseFile, tileDir, heightFile, splatFile);

    const HeightMap* working = m_terrain.editableWorking();
    const SplatMap*  splat   = m_terrain.editableWorkingSplat();
    if (!working || !working->valid())
    {
        DE_LOG_ERROR(LogCategory::Render, "Editor: no working heightfield to save");
        return false;
    }

    std::vector<std::vector<size_t>> bins;
    int tilesX = 0;
    int tilesZ = 0;
    if (m_foliageAuthored)
    {
        tilesX = static_cast<int>(m_terrain.tilesX());
        tilesZ = static_cast<int>(m_terrain.tilesZ());
        const int      tileCells = m_terrain.tileCells();
        const float    tileWorld = static_cast<float>(tileCells) * m_terrain.cellSize();
        const Vector3f origin    = m_terrain.origin();
        bins.resize(static_cast<size_t>(tilesX) * static_cast<size_t>(tilesZ));
        for (size_t i = 0; i < m_foliage.size(); ++i)
        {
            const FoliageRecord& rec = m_foliage[i];
            int tx = static_cast<int>(floorf((rec.x - origin.x) / tileWorld));
            int tz = static_cast<int>(floorf((rec.z - origin.z) / tileWorld));
            if (tx < 0)
                tx = 0;
            if (tz < 0)
                tz = 0;
            if (tx >= tilesX)
                tx = tilesX - 1;
            if (tz >= tilesZ)
                tz = tilesZ - 1;
            bins[static_cast<size_t>(tz) * static_cast<size_t>(tilesX) + static_cast<size_t>(tx)].push_back(i);
        }
        for (int tz = 0; tz < tilesZ; ++tz)
        {
            for (int tx = 0; tx < tilesX; ++tx)
            {
                const size_t count = bins[static_cast<size_t>(tz) * static_cast<size_t>(tilesX) + static_cast<size_t>(tx)].size();
                if (count > kMaxFoliagePerFile)
                {
                    DE_LOG_ERROR(LogCategory::Render, "Editor: foliage tile ({},{}) count {} exceeds the per-file cap", tx, tz, static_cast<uint64_t>(count));
                    return false;
                }
            }
        }
    }

    const std::filesystem::path coarsePath = sidecarPath(scenePath, coarseFile);
    if (!m_terrain.coarse().saveBinary(coarsePath))
        return false;

    const std::filesystem::path tileDirPath = sidecarPath(scenePath, tileDir);
    std::error_code ec;
    std::filesystem::create_directories(tileDirPath, ec);
    if (ec)
    {
        DE_LOG_ERROR(LogCategory::Render, "Editor: failed to create tile dir '{}' ({})", tileDirPath.string(), ec.message());
        return false;
    }

    const int tileCells = m_terrain.tileCells();
    const int samples   = tileCells + 1;
    for (int tz = 0; tz < static_cast<int>(m_terrain.tilesZ()); ++tz)
    {
        for (int tx = 0; tx < static_cast<int>(m_terrain.tilesX()); ++tx)
        {
            HeightMap tile;
            if (!tile.create(static_cast<uint32_t>(samples), static_cast<uint32_t>(samples), working->cellSize(), working->heightScale()))
                return false;
            const int srcX = tx * tileCells;
            const int srcZ = tz * tileCells;
            tile.setOrigin(Vector3f{
                working->origin().x + static_cast<float>(srcX) * working->cellSize(),
                working->origin().y,
                working->origin().z + static_cast<float>(srcZ) * working->cellSize() });
            for (int z = 0; z < samples; ++z)
            {
                for (int x = 0; x < samples; ++x)
                    tile.setHeight(x, z, working->height(srcX + x, srcZ + z));
            }
            if (!tile.saveBinary(tileHeightPath(tileDirPath, tx, tz)))
                return false;

            SplatMap tileSplat;
            if (splat && splat->valid())
            {
                if (!tileSplat.create(static_cast<uint32_t>(samples), static_cast<uint32_t>(samples)))
                    return false;
                for (int z = 0; z < samples; ++z)
                {
                    for (int x = 0; x < samples; ++x)
                    {
                        uint8_t c[4]{};
                        splat->getTexel(srcX + x, srcZ + z, c);
                        tileSplat.setTexel(x, z, c[0], c[1], c[2], c[3]);
                    }
                }
            }
            else
                tileSplat.generateFromHeight(tile);
            if (!saveRgbaPng(tileSplatPath(tileDirPath, tx, tz), tileSplat.width(), tileSplat.height(), tileSplat.rgba()))
            {
                DE_LOG_ERROR(LogCategory::Render, "Editor: tile splat write failed");
                return false;
            }
        }
    }

    if (m_foliageAuthored)
    {
        std::vector<FoliageRecord> tileRecs;
        for (int tz = 0; tz < tilesZ; ++tz)
        {
            for (int tx = 0; tx < tilesX; ++tx)
            {
                const std::vector<size_t>& ids = bins[static_cast<size_t>(tz) * static_cast<size_t>(tilesX) + static_cast<size_t>(tx)];
                tileRecs.clear();
                tileRecs.reserve(ids.size());
                for (size_t id : ids)
                    tileRecs.push_back(m_foliage[id]);
                const uint32_t count = static_cast<uint32_t>(tileRecs.size());
                if (!saveFoliageTile(tileDirPath / tileFoliageFileName(tx, tz), tx, tz, count > 0 ? tileRecs.data() : nullptr, count))
                    return false;
            }
        }
    }

    const uint64_t tileCount = static_cast<uint64_t>(m_terrain.tilesX()) * m_terrain.tilesZ();
    if (tileCount <= 1ull)
    {
        const std::filesystem::path heightPath = sidecarPath(scenePath, heightFile);
        const std::filesystem::path splatPath  = sidecarPath(scenePath, splatFile);
        if (!working->saveBinary(heightPath))
            return false;
        if (!splat || !splat->valid() || !saveRgbaPng(splatPath, splat->width(), splat->height(), splat->rgba()))
        {
            DE_LOG_ERROR(LogCategory::Render, "Editor: splat sidecar write failed");
            return false;
        }
    }
    return true;
}

bool EditorApp::loadTerrainFromScene(const SceneFileData& data, const std::filesystem::path& scenePath)
{
    removeEditorTerrain();
    if (data.mode == SceneMode::Scene2D || !data.hasTerrain)
    {
        resetFoliageAuthoring();
        return true;
    }

    const TerrainSceneDesc& desc = data.terrain;
    if (!desc.source.empty())
    {
        const std::filesystem::path dir = resolveWorldEngineDirectory(desc.source);
        if (dir.empty())
        {
            DE_LOG_ERROR(LogCategory::Render, "Editor: World Engine folder '{}' was not found", desc.source);
            resetFoliageAuthoring();
            return false;
        }
        const float worldSize = desc.worldSize > 1.0f ? desc.worldSize : 1024.0f;
        const float relief    = desc.importHeight > 0.0f ? desc.importHeight : 480.0f;
        std::snprintf(m_worldEnginePath, sizeof(m_worldEnginePath), "%s", desc.source.c_str());
        m_worldEngineSize   = worldSize;
        m_worldEngineHeight = relief;
        m_foliageDensity.dirtTreesPerM2    = desc.foliage.dirtTreesPerM2;
        m_foliageDensity.dirtFlowersPerM2  = desc.foliage.dirtFlowersPerM2;
        m_foliageDensity.grassTreesPerM2   = desc.foliage.grassTreesPerM2;
        m_foliageDensity.grassFlowersPerM2 = desc.foliage.grassFlowersPerM2;
        m_foliageDensity.rockPerM2         = desc.foliage.rockPerM2;
        m_foliageDensity.grassPerM2        = desc.foliage.grassPerM2;
        m_foliageDensity.seed              = desc.foliage.seed;
        m_foliageDensity.treeModel         = desc.foliage.treeModel;
        m_foliageDensity.flowerModel       = desc.foliage.flowerModel;
        m_foliageDensity.rockModel         = desc.foliage.rockModel;
        m_foliageDensity.grassModel        = desc.foliage.grassModel;
        m_foliageStale                     = desc.foliage.stale;
        if (!loadWorldEngineTerrain(dir, worldSize, relief))
        {
            resetFoliageAuthoring();
            return false;
        }
        return true;
    }
    std::string heightFile = desc.heightFile;
    std::string splatFile  = desc.splatFile;
    std::string coarseFile = desc.grid.coarseFile;
    std::string tileDir    = desc.grid.tileDir;
    defaultGridSidecarNames(data.name, coarseFile, tileDir, heightFile, splatFile);

    if (desc.hasGrid)
    {
        TerrainGridDesc gridDesc{};
        gridDesc.tilesX       = desc.grid.tilesX;
        gridDesc.tilesZ       = desc.grid.tilesZ;
        gridDesc.tileCells    = desc.grid.tileCells;
        gridDesc.chunkCells   = desc.chunkCells > 0 ? desc.chunkCells : 64;
        gridDesc.cellSize     = desc.grid.cellSize;
        gridDesc.heightScale  = desc.grid.heightScale;
        gridDesc.origin       = desc.grid.origin;
        gridDesc.coarseFile   = sidecarPath(scenePath, coarseFile);
        gridDesc.tileDir      = sidecarPath(scenePath, tileDir);
        gridDesc.residentRing = desc.grid.residentRing;
        if (!m_terrain.create(gridDesc))
        {
            DE_LOG_ERROR(LogCategory::Render, "Editor: terrain grid create failed");
            resetFoliageAuthoring();
            return false;
        }
        if (!m_terrain.assembleWorking())
        {
            DE_LOG_ERROR(LogCategory::Render, "Editor: working assemble failed");
            removeEditorTerrain();
            resetFoliageAuthoring();
            return false;
        }
        m_terrainSeed     = desc.grid.seed;
        m_terrainSeaLevel = desc.grid.seaLevel;
        m_terrainCoarseFile = coarseFile;
        m_terrainTileDir    = tileDir;
    }
    else
    {
        const std::filesystem::path heightPath = sidecarPath(scenePath, heightFile);
        HeightMap height;
        if (!height.loadBinary(heightPath))
        {
            DE_LOG_ERROR(LogCategory::Render, "Editor: terrain height sidecar missing or invalid — no terrain");
            resetFoliageAuthoring();
            return false;
        }
        const int chunkCells = desc.chunkCells > 0 ? desc.chunkCells : 16;
        if (!m_terrain.createFromHeightMap(std::move(height), chunkCells))
        {
            DE_LOG_ERROR(LogCategory::Render, "Editor: terrain create from scene failed");
            removeEditorTerrain();
            resetFoliageAuthoring();
            return false;
        }
        const std::filesystem::path splatPath = sidecarPath(scenePath, splatFile);
        std::error_code ec;
        SplatMap splat;
        HeightMap* working = m_terrain.editableWorking();
        if (std::filesystem::exists(splatPath, ec) && !ec)
        {
            Image splatImg;
            if (!splatImg.createFromFile(splatPath) || !splatImg.valid() || !working
                || splatImg.width() != working->width() || splatImg.height() != working->height()
                || !splat.createFromRGBA(splatImg.width(), splatImg.height(), splatImg.pixels()))
            {
                DE_LOG_ERROR(LogCategory::Render, "Editor: splat sidecar '{}' invalid — generateFromHeight", splatPath.string());
                if (!working || !splat.generateFromHeight(*working, SplatRules{}))
                {
                    DE_LOG_ERROR(LogCategory::Render, "Editor: splat generate failed");
                    removeEditorTerrain();
                    resetFoliageAuthoring();
                    return false;
                }
            }
        }
        else if (!working || !splat.generateFromHeight(*working, SplatRules{}))
        {
            DE_LOG_ERROR(LogCategory::Render, "Editor: splat generate failed");
            removeEditorTerrain();
            resetFoliageAuthoring();
            return false;
        }
        m_terrain.setWorkingSplat(std::move(splat));
        m_terrainCoarseFile = coarseFile;
        m_terrainTileDir    = tileDir;
    }

    m_foliageDensity.dirtTreesPerM2    = desc.foliage.dirtTreesPerM2;
    m_foliageDensity.dirtFlowersPerM2  = desc.foliage.dirtFlowersPerM2;
    m_foliageDensity.grassTreesPerM2   = desc.foliage.grassTreesPerM2;
    m_foliageDensity.grassFlowersPerM2 = desc.foliage.grassFlowersPerM2;
    m_foliageDensity.rockPerM2         = desc.foliage.rockPerM2;
    m_foliageDensity.grassPerM2        = desc.foliage.grassPerM2;
    m_foliageDensity.seed              = desc.foliage.seed;
    m_foliageDensity.treeModel         = desc.foliage.treeModel;
    m_foliageDensity.flowerModel       = desc.foliage.flowerModel;
    m_foliageDensity.rockModel         = desc.foliage.rockModel;
    m_foliageDensity.grassModel        = desc.foliage.grassModel;
    m_foliageStale                     = desc.foliage.stale;

    m_terrainSurface = {};
    m_terrainSurface.params.heightBlendK   = desc.heightBlendK;
    m_terrainSurface.params.heightBlendT   = desc.heightBlendT;
    m_terrainSurface.params.triplanarSlope = desc.triplanarSlope;
    bool anyMap = false;
    const int n = desc.layerCount > Terrain::kMaxTerrainLayers ? Terrain::kMaxTerrainLayers : desc.layerCount;
    for (int i = 0; i < Terrain::kMaxTerrainLayers; ++i)
    {
        if (i < n)
        {
            m_terrainSurface.layers[i].tiling = desc.layers[i].tiling;
            std::memcpy(m_terrainSurface.layers[i].tint, desc.layers[i].tint, sizeof(m_terrainSurface.layers[i].tint));
        }
        auto loadSlot = [&](const std::string& virt, AssetRef<Image>& outRef, std::string& outPath) {
            outPath = virt;
            if (virt.empty())
                return;
            AssetRef<Image> img = assets().loadImage(virt);
            if (!img || !img->valid())
            {
                DE_LOG_ERROR(LogCategory::Render, "Editor: missing terrain map '{}' — default slot", virt);
                outRef.reset();
                return;
            }
            outRef = std::move(img);
            anyMap = true;
        };
        if (i < n)
        {
            loadSlot(desc.layers[i].albedo, m_terrainSurface.albedo[i], m_terrainAlbedoPath[i]);
            loadSlot(desc.layers[i].normal, m_terrainSurface.normal[i], m_terrainNormalPath[i]);
            loadSlot(desc.layers[i].orm, m_terrainSurface.orm[i], m_terrainOrmPath[i]);
        }
    }

    SplatMap matSplat;
    const SplatMap* workingSplat = m_terrain.editableWorkingSplat();
    if (workingSplat && workingSplat->valid() && workingSplat->width() <= Terrain::kMaxSplatMapSize
        && workingSplat->height() <= Terrain::kMaxSplatMapSize)
        matSplat = *workingSplat;
    else if (!matSplat.create(2, 2))
    {
        removeEditorTerrain();
        resetFoliageAuthoring();
        return false;
    }

    Texture2D splatTex;
    Image splatImg;
    if (!splatImg.createFromRGBA(matSplat.rgba(), matSplat.width(), matSplat.height(), matSplat.width() * 4u)
        || !splatTex.createFromImage(renderer(), splatImg, Dark::Color::TextureUsage::Data))
    {
        DE_LOG_ERROR(LogCategory::Render, "Editor: splat GPU upload failed");
        removeEditorTerrain();
        resetFoliageAuthoring();
        return false;
    }

    bool ok = false;
    if (anyMap)
        ok = m_terrainMaterial.create(renderer(), m_terrainSurface, std::move(splatTex));
    else
        ok = m_terrainMaterial.createDefault(renderer(), matSplat);
    if (!ok)
    {
        DE_LOG_ERROR(LogCategory::Render, "Editor: terrain material from scene failed");
        removeEditorTerrain();
        resetFoliageAuthoring();
        return false;
    }
    if (!anyMap)
    {
        m_terrainMaterial.params() = m_terrainSurface.params;
        for (int i = 0; i < Terrain::kMaxTerrainLayers; ++i)
            m_terrainMaterial.layer(i) = m_terrainSurface.layers[i];
    }
    m_terrainMaterial.setLayerSamplingRaw(renderer().device(), renderer().debugState().legacyUnormAlbedo);

    m_terrainHeightFile  = heightFile;
    m_terrainSplatFile   = splatFile;
    m_haveTerrain        = true;
    if (!applyEditorGridGpu())
    {
        removeEditorTerrain();
        resetFoliageAuthoring();
        return false;
    }

    bool anyFoliageFile = false;
    std::vector<FoliageRecord> gathered;
    const int foliageTilesX = static_cast<int>(m_terrain.tilesX());
    const int foliageTilesZ = static_cast<int>(m_terrain.tilesZ());
    const std::filesystem::path foliageDir = sidecarPath(scenePath, tileDir);
    for (int tz = 0; tz < foliageTilesZ; ++tz)
    {
        for (int tx = 0; tx < foliageTilesX; ++tx)
        {
            const std::filesystem::path foliagePath = foliageDir / tileFoliageFileName(tx, tz);
            std::error_code foliageEc;
            if (!std::filesystem::exists(foliagePath, foliageEc) || foliageEc)
                continue;
            std::vector<FoliageRecord> loaded;
            if (!loadFoliageTile(foliagePath, tx, tz, loaded))
            {
                DE_LOG_ERROR(LogCategory::Render, "Editor: foliage bin '{}' invalid", foliagePath.string());
                resetFoliageAuthoring();
                removeEditorTerrain();
                return false;
            }
            if (static_cast<uint64_t>(gathered.size()) + static_cast<uint64_t>(loaded.size()) > static_cast<uint64_t>(kMaxFoliageInstances))
            {
                DE_LOG_ERROR(LogCategory::Render, "Editor: foliage exceeds instance cap at tile ({},{})", tx, tz);
                resetFoliageAuthoring();
                removeEditorTerrain();
                return false;
            }
            anyFoliageFile = true;
            if (!loaded.empty())
                gathered.insert(gathered.end(), loaded.begin(), loaded.end());
        }
    }
    m_foliage.swap(gathered);
    m_foliageAuthored = anyFoliageFile;

    rebuildEditorWater();
    return true;
}

void EditorApp::clearFoliageInstances(const char* reason)
{
    std::vector<Terrain::FoliageRecord>().swap(m_foliage);
    m_foliageStale = false;
    DE_LOG_INFO(LogCategory::Render, "Editor: cleared foliage instances ({})", reason != nullptr ? reason : "");
}

void EditorApp::resetFoliageAuthoring()
{
    m_foliageAuthored = false;
    m_foliageStale = false;
    m_foliageDensity = {};
}

bool EditorApp::onFoliageProgress(float t, const char* phase, void* user)
{
    auto* app = static_cast<EditorApp*>(user);
    if (!app)
        return false;
    app->m_foliageProgress.store(t);
    if (phase)
        std::snprintf(app->m_foliagePhase, sizeof(app->m_foliagePhase), "%s", phase);
    return !app->m_foliageCancel.load();
}

void EditorApp::cancelFoliageSpawn()
{
    if (!m_foliageRunning.load() && !m_foliageThread.joinable())
        return;
    m_foliageCancel.store(true);
    if (m_foliageThread.joinable())
        m_foliageThread.join();
    m_foliageRunning.store(false);
    m_foliageDone.store(false);
}

void EditorApp::startFoliageSpawn()
{
    if (terrainBrushesLocked())
        return;

    const HeightMap* height = m_terrain.editableWorking();
    const SplatMap*  splat  = m_terrain.editableWorkingSplat();
    if (height == nullptr)
        DE_LOG_ERROR(LogCategory::Render, "Editor: foliage spawn refused — editableWorking() is null");
    else if (!height->valid())
        DE_LOG_ERROR(LogCategory::Render, "Editor: foliage spawn refused — editableWorking() is invalid");
    if (splat == nullptr)
        DE_LOG_ERROR(LogCategory::Render, "Editor: foliage spawn refused — editableWorkingSplat() is null");
    else if (!splat->valid())
        DE_LOG_ERROR(LogCategory::Render, "Editor: foliage spawn refused — editableWorkingSplat() is invalid");
    if (height == nullptr || !height->valid() || splat == nullptr || !splat->valid())
        return;

    FoliageSpawnIn in;
    in.height    = height;
    in.splat     = splat;
    in.density   = m_foliageDensity;
    in.tilesX    = m_terrain.tilesX();
    in.tilesZ    = m_terrain.tilesZ();
    const int cells = m_terrain.tileCells();
    in.tileCells = cells > 0 ? static_cast<uint32_t>(cells) : 0u;
    in.cellSize  = m_terrain.cellSize();
    in.origin    = m_terrain.origin();
    // Imported valleys sit at the height origin. The old sea level would skip the forest floor.
    in.seaLevel  = m_terrainSource.empty() ? m_terrainSeaLevel : m_terrain.origin().y;
    in.onProgress = &EditorApp::onFoliageProgress;
    in.user       = this;

    m_foliageCancel.store(false);
    m_foliageDone.store(false);
    m_foliageOk = false;
    m_foliageProgress.store(0.0f);
    std::vector<Terrain::FoliageRecord>().swap(m_foliagePending);
    std::snprintf(m_foliagePhase, sizeof(m_foliagePhase), "starting");
    m_foliageRunning.store(true);

    m_foliageThread = std::thread([this, in]() {
        FoliageSpawnOut out;
        bool ok = spawnFoliage(in, out);
        if (ok)
        {
            const int      tilesX    = static_cast<int>(in.tilesX);
            const int      tilesZ    = static_cast<int>(in.tilesZ);
            const float    tileWorld = static_cast<float>(in.tileCells) * in.cellSize;
            const Vector3f origin    = in.origin;
            std::vector<uint32_t> counts(static_cast<size_t>(tilesX) * static_cast<size_t>(tilesZ), 0u);
            for (const FoliageRecord& rec : out.records)
            {
                int tx = static_cast<int>(floorf((rec.x - origin.x) / tileWorld));
                int tz = static_cast<int>(floorf((rec.z - origin.z) / tileWorld));
                if (tx < 0)
                    tx = 0;
                if (tz < 0)
                    tz = 0;
                if (tx >= tilesX)
                    tx = tilesX - 1;
                if (tz >= tilesZ)
                    tz = tilesZ - 1;
                ++counts[static_cast<size_t>(tz) * static_cast<size_t>(tilesX) + static_cast<size_t>(tx)];
            }
            for (int tz = 0; tz < tilesZ && ok; ++tz)
            {
                for (int tx = 0; tx < tilesX; ++tx)
                {
                    const uint32_t count = counts[static_cast<size_t>(tz) * static_cast<size_t>(tilesX) + static_cast<size_t>(tx)];
                    if (count > kMaxFoliagePerFile)
                    {
                        DE_LOG_ERROR(LogCategory::Render, "Editor: foliage spawn exceeds the per-tile file cap ({},{}) count {} - previous set kept", tx, tz, count);
                        ok = false;
                        break;
                    }
                }
            }
        }
        if (ok)
            m_foliagePending.swap(out.records);
        m_foliageOk = ok;
        m_foliageDone.store(true);
    });
}

void EditorApp::pollFoliageSpawn()
{
    if (!m_foliageDone.load())
        return;
    if (m_foliageThread.joinable())
        m_foliageThread.join();
    m_foliageRunning.store(false);
    m_foliageDone.store(false);
    if (m_foliageOk)
    {
        m_foliage.swap(m_foliagePending);
        m_foliageAuthored = true;
        m_foliageStale = false;
    }
    else if (m_foliageCancel.load())
        DE_LOG_INFO(LogCategory::Render, "Editor: foliage spawn cancelled - previous set kept");
    std::vector<Terrain::FoliageRecord>().swap(m_foliagePending);
    m_foliageOk = false;
}

void EditorApp::drawWaterTools()
{
    if (!m_showWaterTools)
        return;
    ImGui::SetNextWindowSize(ImVec2(340.0f, 240.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Water Waves", &m_showWaterTools))
    {
        ImGui::End();
        return;
    }

    if (m_sceneMode != SceneMode::Scene3D)
    {
        ImGui::TextDisabled("Water waves are part of the 3D terrain.");
        ImGui::End();
        return;
    }

    ImGui::TextDisabled("Wave height and speed apply to every placed water body.");

    WaterParams& wp      = m_water.params();
    float        baseAmp = 0.0f;
    for (int i = 0; i < kWaterWaveCount; ++i)
        baseAmp += wp.waves[i].amplitude;

    float heightM = baseAmp * (wp.amplitudeScale > 0.0f ? wp.amplitudeScale : 0.0f);
    if (ImGui::SliderFloat("Wave height", &heightM, 0.0f, 6.0f, "%.2f m"))
        wp.amplitudeScale = baseAmp > 1.0e-5f ? heightM / baseAmp : 0.0f;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("How high the crests rise if every wave peaks together. The authored height is about %.2f m.", static_cast<double>(baseAmp));
    ImGui::SliderFloat("Wave speed", &wp.speedScale, 0.0f, 4.0f, "%.2fx");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("How fast the waves travel. 1 is the authored speed. 0 holds them still.");
    ImGui::SliderFloat("Drift", &wp.flowSpeed, 0.0f, 6.0f, "%.2f m/s");
    ImGui::SliderFloat("Foam", &wp.foam, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Foam width", &wp.foamWidthScale, 0.5f, 2.0f, "%.2f");
    if (ImGui::Button("Reset waves"))
    {
        wp.amplitudeScale = 1.0f;
        wp.speedScale     = 1.0f;
    }
    ImGui::End();
}

bool EditorApp::loadWorldEngineTerrain(const std::filesystem::path& directory, float worldSizeMeters, float heightRangeMeters)
{
    WorldEngineLoadDesc importDesc{};
    importDesc.worldSizeMeters   = worldSizeMeters;
    importDesc.heightRangeMeters = heightRangeMeters;
    WorldEngineMaps maps;
    if (!loadWorldEngineDirectory(directory, importDesc, maps))
        return false;

    removeEditorTerrain();

    SplatMap splatCopy = maps.splat;
    if (!m_terrain.createFromHeightMap(std::move(maps.height), 64) || !m_terrain.setWorkingSplat(std::move(maps.splat)))
    {
        DE_LOG_ERROR(LogCategory::Render, "Editor: World Engine heightfield rejected");
        removeEditorTerrain();
        return false;
    }
    if (!uploadWorldEngineMaterial(renderer(), assets(), splatCopy, maps, m_terrainMaterial, &m_terrainSurface))
    {
        removeEditorTerrain();
        return false;
    }
    m_terrainMaterial.setLayerSamplingRaw(renderer().device(), renderer().debugState().legacyUnormAlbedo);
    if (!maps.diffuse.empty())
        m_terrainAlbedoPath[0] = terrainMapKey(assets(), maps.diffuse);

    m_haveTerrain       = true;
    m_terrainSource     = maps.sourceKey.empty() ? directory.generic_string() : maps.sourceKey;
    m_worldEngineSize   = worldSizeMeters;
    m_worldEngineHeight = heightRangeMeters;
    if (!applyEditorGridGpu())
    {
        removeEditorTerrain();
        return false;
    }
    const WaterParams session = m_water.params();
    rebuildEditorWater();
    if (m_authoredWater.present)
    {
        const AABox3f terrainBox = m_terrain.bounds();
        const float level = m_authoredWater.hasLevel
            ? m_authoredWater.level
            : Lerp(terrainBox.Min.y, terrainBox.Max.y, m_authoredWater.levelFraction);
        WaterParams params = sceneWaterParams(m_authoredWater, level);
        params.amplitudeScale = session.amplitudeScale;
        params.speedScale     = session.speedScale;
        params.foam           = session.foam;
        params.flowSpeed      = session.flowSpeed;
        params.foamWidthScale = session.foamWidthScale;
        params.detailAmount   = session.detailAmount;
        m_water.params() = params;
        createEditorWaterSheet();
    }
    startFoliageSpawn();
    DE_LOG_INFO(LogCategory::Render, "Editor: loaded World Engine terrain '{}'", m_terrainSource);
    return true;
}

void EditorApp::drawTerrainPanel()
{
    if (m_sceneMode != SceneMode::Scene3D || !m_showTerrainPanel)
        return;
    if (!ImGui::Begin("Terrain", &m_showTerrainPanel))
    {
        ImGui::End();
        return;
    }

    if (m_worldEnginePath[0] == 0)
        std::snprintf(m_worldEnginePath, sizeof(m_worldEnginePath), "terrain/HurricaneRidge");

    if (!m_scene.terrainPipeline().isValid())
        ImGui::TextDisabled("TerrainPipeline is not available.");

    ImGui::TextWrapped("Load a World Engine export. The mesh is built from the height map. Diffuse and roughness shade it. The splat map chooses the footstep and how fast NPCs move.");
    ImGui::InputText("Folder", m_worldEnginePath, sizeof(m_worldEnginePath));
    ImGui::SameLine();
    if (ImGui::Button("Browse"))
    {
        std::filesystem::path folder;
        if (pickFolder(static_cast<HWND>(window().nativeHandle()), folder))
        {
            const std::string key = worldEngineSourceKey(folder);
            std::snprintf(m_worldEnginePath, sizeof(m_worldEnginePath), "%s", key.c_str());
        }
    }
    ImGui::SliderFloat("World size (m)", &m_worldEngineSize, 128.0f, 8192.0f, "%.0f");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Width and depth of the heightfield, in meters.");
    ImGui::SliderFloat("Height range (m)", &m_worldEngineHeight, 20.0f, 4000.0f, "%.0f");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Meters from the lowest sample in the file to the highest. Valleys sit at Y = 0.");

    if (ImGui::Button("Load terrain"))
    {
        const std::filesystem::path dir = resolveWorldEngineDirectory(m_worldEnginePath);
        if (dir.empty())
            DE_LOG_ERROR(LogCategory::Render, "Editor: terrain folder '{}' was not found", m_worldEnginePath);
        else if (!loadWorldEngineTerrain(dir, m_worldEngineSize, m_worldEngineHeight))
            DE_LOG_ERROR(LogCategory::Render, "Editor: failed to load '{}'", dir.string());
    }

    if (m_haveTerrain && m_terrain.valid())
    {
        ImGui::SameLine();
        if (ImGui::Button("Frame"))
            frameCameraOnTerrain();
        ImGui::SameLine();
        if (ImGui::Button("Remove"))
            removeEditorTerrain();

        const HeightMap* hm = m_terrain.editableWorking();
        if (!hm || !hm->valid())
            hm = &m_terrain.coarse();
        ImGui::Text("Heightfield %ux%u    cell %.2f m    relief %.0f m", hm->width(), hm->height(), hm->cellSize(), m_worldEngineHeight);
        if (!m_terrainSource.empty())
            ImGui::TextDisabled("%s", m_terrainSource.c_str());
        ImGui::TextWrapped("Rock and snow are 0.75x move speed. Dirt and grass stay at full speed. Each surface plays its own footstep.");
        if (m_foliageRunning.load())
            ImGui::Text("Placing trees from the splat... %s", m_foliagePhase);
        else if (m_foliageAuthored)
        {
            uint32_t trees = 0;
            uint32_t grass = 0;
            for (const FoliageRecord& rec : m_foliage)
            {
                if (rec.kind == static_cast<uint8_t>(FoliageKind::Tree))
                    ++trees;
                else if (rec.kind == static_cast<uint8_t>(FoliageKind::Grass))
                    ++grass;
            }
            ImGui::Text("Trees follow grass and dirt. Grass tufts follow the grass layer. %u trees, %u grass.", trees, grass);
        }
    }
    else
        ImGui::TextWrapped("No terrain in this level. Outdoor scenes use the grey ground plane until you load one.");

    ImGui::End();
}

