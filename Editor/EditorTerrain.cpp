#include "EditorApp.h"

#include "Assets/Image.h"
#include "Core/Log.h"
#include "Editor/EditorInternals.h"
#include "Math/MathHelper.h"
#include "Terrain/TerrainGen.h"
#include "Terrain/TerrainTileFile.h"
#include "Water/WaterWaves.h"
#include "Ui/Icons.h"
#include "ECS/Components.h"

#include <imgui.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <commdlg.h>
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
    cancelGenerateWorld();
    clearTerrainUndo();
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
    m_water = WaterWorld{};
    m_terrain.clear();
    m_terrainMaterial    = {};
    m_splat              = {};
    m_terrainSurface     = {};
    m_haveTerrain        = false;
    m_terrainHeightDirty = false;
    m_terrainSplatDirty  = false;
    m_terrainBrush       = TerrainBrushMode::None;
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
    return m_genRunning.load();
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
    const float amplitudeScale = m_water.params().amplitudeScale;
    const float speedScale     = m_water.params().speedScale;
    m_water = WaterWorld{};
    if (!m_haveTerrain || !m_terrain.valid() || !m_terrain.coarse().valid())
        return true;
    renderer().waitForGpu();
    WaterDesc desc;
    desc.chunkCells       = kWaterChunkCellsCoarse;
    desc.waterLevel       = m_terrainSeaLevel;
    desc.lodDistanceCount = 5;
    desc.lodDistances[0]  = 40.0f;
    desc.lodDistances[1]  = 80.0f;
    desc.lodDistances[2]  = 160.0f;
    desc.lodDistances[3]  = 320.0f;
    desc.lodDistances[4]  = 640.0f;
    desc.params                = defaultWaterParams(m_terrainSeaLevel);
    desc.params.amplitudeScale = amplitudeScale;
    desc.params.speedScale     = speedScale;
    if (!m_water.create(m_terrain.coarse(), desc))
    {
        DE_LOG_ERROR(LogCategory::Render, "Editor: water create failed");
        m_water.params().amplitudeScale = amplitudeScale;
        m_water.params().speedScale     = speedScale;
        return false;
    }
    m_water.updateLod(m_camera.GetPosition());
    if (!m_water.createGpu(renderer()))
    {
        DE_LOG_ERROR(LogCategory::Render, "Editor: water GPU upload failed");
        m_water = WaterWorld{};
        m_water.params().amplitudeScale = amplitudeScale;
        m_water.params().speedScale     = speedScale;
        return false;
    }
    bindTerrainHeightSrv();
    if (!m_scene.waterPipeline().isValid())
        DE_LOG_ERROR(LogCategory::Render, "Editor: WaterPipeline invalid — water will not draw");
    DE_LOG_INFO(LogCategory::Render, "Editor: water level {:.1f} m, {} wet chunks", m_terrainSeaLevel, m_water.wetChunkCount());
    return true;
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

bool EditorApp::createEditorTerrain()
{
    removeEditorTerrain();

    HeightMap base;
    HeightMap detail;
    if (!base.createFbm(129, 129, 1337u, 6, 3.5f, 1.0f, 2.1f, 0.48f, 2.0f, 22.0f)
        || !detail.createFbm(129, 129, 9001u, 3, 18.0f, 0.12f, 2.0f, 0.5f, 2.0f, 22.0f)
        || !base.addLayer(detail, 1.0f))
    {
        DE_LOG_ERROR(LogCategory::Render, "Editor: height map create failed");
        return false;
    }
    const float extent = 128.0f * 2.0f;
    base.setOrigin(Vector3f{ -0.5f * extent, 0.0f, -0.5f * extent });

    SplatMap splat;
    if (!splat.generateFromHeight(base, m_splatRules))
    {
        DE_LOG_ERROR(LogCategory::Render, "Editor: splat generate failed");
        return false;
    }
    if (!m_terrain.createFromHeightMap(std::move(base), 16))
    {
        DE_LOG_ERROR(LogCategory::Render, "Editor: terrain create failed");
        return false;
    }
    if (!m_terrainMaterial.createDefault(renderer(), splat))
    {
        DE_LOG_ERROR(LogCategory::Render, "Editor: terrain material create failed");
        removeEditorTerrain();
        return false;
    }
    m_terrain.setWorkingSplat(std::move(splat));
    m_terrainMaterial.setLayerSamplingRaw(renderer().device(), renderer().debugState().legacyUnormAlbedo);
    m_terrainSurface = {};
    m_terrainSurface.params.heightBlendK = 0.0f;
    for (int i = 0; i < Terrain::kMaxTerrainLayers; ++i)
        m_terrainSurface.layers[i] = m_terrainMaterial.layer(i);
    defaultGridSidecarNames(m_sceneName, m_terrainCoarseFile, m_terrainTileDir, m_terrainHeightFile, m_terrainSplatFile);
    m_terrainSeed     = 1337u;
    m_terrainSeaLevel = 32.0f;
    m_haveTerrain     = true;
    if (!applyEditorGridGpu())
    {
        removeEditorTerrain();
        return false;
    }
    rebuildEditorWater();
    DE_LOG_INFO(LogCategory::Render, "Editor: created 129x129 terrain (1-tile Grid)");
    return true;
}

void EditorApp::syncTerrainLod()
{
    pollTerrainGenerate();
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
    m_water.updateLod(m_camera.GetPosition());
    if (m_water.needsRebuild())
    {
        m_water.rebuildDirtyCpuMeshes();
        if (!m_water.uploadDirty(renderer()))
            DE_LOG_ERROR(LogCategory::Render, "Editor: water upload failed");
    }
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
}

void EditorApp::applyTerrainBrush(float dt)
{
    if (!m_haveTerrain || m_terrainBrush == TerrainBrushMode::None || terrainBrushesLocked())
        return;
    if (dt < 0.0f)
        dt = 0.0f;

    Vector3f hit{};
    if (!groundHitFromMouse(hit))
        return;

    HeightMap* working = m_terrain.editableWorking();
    SplatMap*  splat   = m_terrain.editableWorkingSplat();
    if (!working || !working->valid() || !working->containsXZ(hit.x, hit.z))
        return;

    const float radius = m_terrainBrushRadius > 0.05f ? m_terrainBrushRadius : 0.05f;
    const float strength = Math::Clamp(m_terrainBrushStrength, 0.0f, 1.0f);

    float fx = 0.0f;
    float fz = 0.0f;
    working->worldToSample(hit.x, hit.z, fx, fz);
    const float radiusSamples = working->cellSize() > 0.0f ? (radius / working->cellSize()) : radius;
    const int x0 = static_cast<int>(floorf(fx - radiusSamples));
    const int x1 = static_cast<int>(ceilf(fx + radiusSamples));
    const int z0 = static_cast<int>(floorf(fz - radiusSamples));
    const int z1 = static_cast<int>(ceilf(fz + radiusSamples));

    if (!m_terrainStrokeActive)
    {
        pushTerrainUndo(x0, z0, x1, z1, m_terrainBrush != TerrainBrushMode::Paint, m_terrainBrush == TerrainBrushMode::Paint);
        m_terrainStrokeActive = true;
    }

    if (m_terrainBrush == TerrainBrushMode::Paint)
    {
        if (!splat || !splat->valid())
            return;
        float amount = strength * dt * 4.0f;
        if (m_terrainPaintLower)
            amount = -amount;
        amount = Math::Clamp(amount, -1.0f, 1.0f);
        splat->paintDisk(fx, fz, radiusSamples, m_terrainPaintLayer, amount);
        m_terrain.applyWorkingRect(x0, z0, x1, z1, false, true);
        m_terrainSplatDirty = true;
        return;
    }

    if (m_terrainBrush == TerrainBrushMode::SculptSmooth)
    {
        const float alpha = Math::Clamp(strength * dt * 4.0f, 0.0f, 1.0f);
        working->smoothDisk(hit.x, hit.z, radius, alpha);
    }
    else
    {
        float delta = strength * dt * 12.0f;
        if (m_terrainBrush == TerrainBrushMode::SculptLower)
            delta = -delta;
        working->addDisk(hit.x, hit.z, radius, delta);
    }

    m_terrain.applyWorkingRect(x0, z0, x1, z1, true, false);
    m_terrainHeightDirty = true;
}

void EditorApp::fillTerrainSceneDesc(SceneFileData& data) const
{
    data.hasTerrain = false;
    if (!m_haveTerrain || !m_terrain.valid() || data.mode == SceneMode::Scene2D)
        return;

    TerrainSceneDesc desc{};
    desc.bindLayout     = TerrainSceneDesc::kBindLayoutV1;
    desc.chunkCells     = m_terrain.chunkCells() > 0 ? m_terrain.chunkCells() : 16;
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
    data.hasTerrain = true;
    data.terrain    = std::move(desc);
}

bool EditorApp::saveTerrainSidecars(const std::filesystem::path& scenePath) const
{
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
        return true;

    const TerrainSceneDesc& desc = data.terrain;
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
            return false;
        }
        if (!m_terrain.assembleWorking())
        {
            DE_LOG_ERROR(LogCategory::Render, "Editor: working assemble failed");
            removeEditorTerrain();
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
            return false;
        }
        const int chunkCells = desc.chunkCells > 0 ? desc.chunkCells : 16;
        if (!m_terrain.createFromHeightMap(std::move(height), chunkCells))
        {
            DE_LOG_ERROR(LogCategory::Render, "Editor: terrain create from scene failed");
            removeEditorTerrain();
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
                if (!working || !splat.generateFromHeight(*working, m_splatRules))
                {
                    DE_LOG_ERROR(LogCategory::Render, "Editor: splat generate failed");
                    removeEditorTerrain();
                    return false;
                }
            }
        }
        else if (!working || !splat.generateFromHeight(*working, m_splatRules))
        {
            DE_LOG_ERROR(LogCategory::Render, "Editor: splat generate failed");
            removeEditorTerrain();
            return false;
        }
        m_terrain.setWorkingSplat(std::move(splat));
        m_terrainCoarseFile = coarseFile;
        m_terrainTileDir    = tileDir;
    }

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
        return false;
    }

    Texture2D splatTex;
    Image splatImg;
    if (!splatImg.createFromRGBA(matSplat.rgba(), matSplat.width(), matSplat.height(), matSplat.width() * 4u)
        || !splatTex.createFromImage(renderer(), splatImg, Dark::Color::TextureUsage::Data))
    {
        DE_LOG_ERROR(LogCategory::Render, "Editor: splat GPU upload failed");
        removeEditorTerrain();
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
        return false;
    }
    rebuildEditorWater();
    return true;
}

bool EditorApp::onGenProgress(float t, const char* phase, void* user)
{
    auto* app = static_cast<EditorApp*>(user);
    if (!app)
        return false;
    app->m_genProgress.store(t);
    if (phase)
        std::snprintf(app->m_genPhase, sizeof(app->m_genPhase), "%s", phase);
    return !app->m_genCancel.load();
}

void EditorApp::cancelGenerateWorld()
{
    if (!m_genRunning.load() && !m_genThread.joinable())
        return;
    m_genCancel.store(true);
    if (m_genThread.joinable())
        m_genThread.join();
    m_genRunning.store(false);
    m_genDone.store(false);
}

void EditorApp::startGenerateWorld()
{
    if (m_genRunning.load())
        return;
    if (!m_erosionAttempted)
    {
        m_erosionAttempted = true;
        if (!m_erosionPipe.create(renderer().device()))
            DE_LOG_INFO(LogCategory::Render, "Editor: erosion PSO invalid — CPU fallback");
    }

    const int tiles = kGenTileCounts[m_genTilesIndex < 0 ? 0 : (m_genTilesIndex > 3 ? 3 : m_genTilesIndex)];
    WorldGenDesc desc{};
    desc.tilesX      = static_cast<uint32_t>(tiles);
    desc.tilesZ      = static_cast<uint32_t>(tiles);
    desc.tileCells   = static_cast<uint32_t>(kTileCells);
    desc.cellSize    = m_genCellSize > 0.05f ? m_genCellSize : 1.0f;
    desc.heightScale = m_genHeightScale > 0.0f ? m_genHeightScale : 80.0f;
    const float half = 0.5f * static_cast<float>(tiles * kTileCells) * desc.cellSize;
    desc.origin      = Vector3f{ -half, 0.0f, -half };
    desc.erosion                          = m_genFilter;
    desc.erosion.seed                 = m_terrainSeed;
    desc.erosion.thermalIterations    = m_genThermal;
    desc.erosion.hydraulicIterations  = m_genHydroIters;
    desc.erosion.hydraulicMaxSteps    = m_genHydroSteps;
    desc.erosion.seaLevelRaw          = desc.heightScale > 0.0f ? (m_terrainSeaLevel / desc.heightScale) : 0.0f;

    m_genRunTilesX      = desc.tilesX;
    m_genRunTilesZ      = desc.tilesZ;
    m_genRunTileCells   = desc.tileCells;
    m_genRunCellSize    = desc.cellSize;
    m_genRunHeightScale = desc.heightScale;
    m_genRunOrigin      = desc.origin;

    m_genCancel.store(false);
    m_genDone.store(false);
    m_genOk = false;
    m_genProgress.store(0.0f);
    std::snprintf(m_genPhase, sizeof(m_genPhase), "starting");
    m_genRunning.store(true);

    ID3D12Device*       device = renderer().device();
    ID3D12CommandQueue* queue  = renderer().queue();
    TerrainErosionPipeline* pipe = m_erosionPipe.isValid() ? &m_erosionPipe : nullptr;
    m_genThread = std::thread([this, device, queue, pipe, desc]() {
        WorldGenGpu gpu{};
        gpu.pipeline = pipe;
        gpu.device   = device;
        gpu.queue    = queue;
        m_genOk      = generateWorld(desc, m_genOut, m_genSplat, &EditorApp::onGenProgress, this, pipe ? &gpu : nullptr);
        m_genDone.store(true);
    });
}

void EditorApp::pollTerrainGenerate()
{
    if (!m_genDone.load())
        return;
    if (m_genThread.joinable())
        m_genThread.join();
    m_genRunning.store(false);
    m_genDone.store(false);
    if (m_genOk)
    {
        if (!applyGeneratedWorld())
            DE_LOG_ERROR(LogCategory::Render, "Editor: generate apply failed — terrain cleared");
    }
    else
        DE_LOG_INFO(LogCategory::Render, "Editor: generate cancelled or failed — keeping previous terrain");
    m_genOut   = HeightMap{};
    m_genSplat = SplatMap{};
}

bool EditorApp::applyGeneratedWorld()
{
    if (!m_genOut.valid())
        return false;
    renderer().waitForGpu();
    clearTerrainUndo();

    const bool keepMaterial = m_terrainMaterial.isValid();
    m_terrain.clear();

    TerrainGridDesc desc{};
    desc.tilesX       = m_genRunTilesX;
    desc.tilesZ       = m_genRunTilesZ;
    desc.tileCells    = m_genRunTileCells;
    desc.chunkCells   = 64;
    desc.cellSize     = m_genRunCellSize;
    desc.heightScale  = m_genRunHeightScale;
    desc.origin       = m_genRunOrigin;
    desc.residentRing = kResidentRingDefault;

    HeightMap stub;
    if (!stub.create(2, 2, desc.cellSize, desc.heightScale))
    {
        DE_LOG_ERROR(LogCategory::Render, "Editor: generate stub height failed");
        return false;
    }
    stub.setOrigin(desc.origin);
    if (!m_terrain.createFromCoarse(desc, std::move(stub)))
    {
        DE_LOG_ERROR(LogCategory::Render, "Editor: generate grid create failed");
        return false;
    }
    if (!m_terrain.setWorking(std::move(m_genOut), std::move(m_genSplat)))
    {
        DE_LOG_ERROR(LogCategory::Render, "Editor: generate setWorking failed");
        return false;
    }
    if (!m_terrain.boxFilterCoarseFromWorking())
    {
        DE_LOG_ERROR(LogCategory::Render, "Editor: generate coarse downsample failed");
        return false;
    }

    SplatMap matSplat;
    const SplatMap* ws = m_terrain.editableWorkingSplat();
    if (ws && ws->valid() && ws->width() <= kMaxSplatMapSize)
        matSplat = *ws;
    else if (!matSplat.create(2, 2))
        return false;

    if (!keepMaterial)
    {
        if (!m_terrainMaterial.createDefault(renderer(), matSplat))
        {
            m_terrain.clear();
            return false;
        }
        m_terrainSurface = {};
        m_terrainSurface.params.heightBlendK = 0.0f;
        for (int i = 0; i < Terrain::kMaxTerrainLayers; ++i)
            m_terrainSurface.layers[i] = m_terrainMaterial.layer(i);
    }
    m_terrainMaterial.setLayerSamplingRaw(renderer().device(), renderer().debugState().legacyUnormAlbedo);
    defaultGridSidecarNames(m_sceneName, m_terrainCoarseFile, m_terrainTileDir, m_terrainHeightFile, m_terrainSplatFile);
    m_haveTerrain = true;
    if (!applyEditorGridGpu())
    {
        removeEditorTerrain();
        return false;
    }
    rebuildEditorWater();
    DE_LOG_INFO(LogCategory::Render, "Editor: generated {}x{} tiles", desc.tilesX, desc.tilesZ);
    return true;
}

void EditorApp::clearTerrainUndo()
{
    m_terrainUndoCount    = 0;
    m_terrainStrokeActive = false;
    for (int i = 0; i < kTerrainUndoDepth; ++i)
        m_terrainUndo[i] = TerrainBrushUndo{};
}

void EditorApp::pushTerrainUndo(int x0, int z0, int x1, int z1, bool heights, bool splat)
{
    HeightMap* working = m_terrain.editableWorking();
    SplatMap*  sp      = m_terrain.editableWorkingSplat();
    if (!working || !working->valid())
        return;
    if (x0 > x1)
    {
        const int t = x0;
        x0          = x1;
        x1          = t;
    }
    if (z0 > z1)
    {
        const int t = z0;
        z0          = z1;
        z1          = t;
    }
    if (x0 < 0)
        x0 = 0;
    if (z0 < 0)
        z0 = 0;
    const int maxX = static_cast<int>(working->width()) - 1;
    const int maxZ = static_cast<int>(working->height()) - 1;
    if (x1 > maxX)
        x1 = maxX;
    if (z1 > maxZ)
        z1 = maxZ;
    const int w = x1 - x0 + 1;
    const int h = z1 - z0 + 1;
    if (w <= 0 || h <= 0)
        return;
    if (w > 256 || h > 256)
    {
        DE_LOG_INFO(LogCategory::Render, "Editor: brush rect {}x{} skips undo", w, h);
        return;
    }

    TerrainBrushUndo snap{};
    snap.x0      = x0;
    snap.z0      = z0;
    snap.x1      = x1;
    snap.z1      = z1;
    snap.heights = heights;
    snap.splat   = splat;
    if (heights)
    {
        snap.height.resize(static_cast<size_t>(w) * static_cast<size_t>(h));
        for (int z = 0; z < h; ++z)
        {
            for (int x = 0; x < w; ++x)
                snap.height[static_cast<size_t>(z) * w + x] = working->height(x0 + x, z0 + z);
        }
    }
    if (splat && sp && sp->valid())
    {
        snap.splatRgba.resize(static_cast<size_t>(w) * static_cast<size_t>(h) * 4u);
        for (int z = 0; z < h; ++z)
        {
            for (int x = 0; x < w; ++x)
            {
                uint8_t c[4]{};
                sp->getTexel(x0 + x, z0 + z, c);
                const size_t i = (static_cast<size_t>(z) * w + x) * 4u;
                snap.splatRgba[i + 0] = c[0];
                snap.splatRgba[i + 1] = c[1];
                snap.splatRgba[i + 2] = c[2];
                snap.splatRgba[i + 3] = c[3];
            }
        }
    }

    if (m_terrainUndoCount < kTerrainUndoDepth)
        m_terrainUndo[m_terrainUndoCount++] = std::move(snap);
    else
    {
        for (int i = 1; i < kTerrainUndoDepth; ++i)
            m_terrainUndo[i - 1] = std::move(m_terrainUndo[i]);
        m_terrainUndo[kTerrainUndoDepth - 1] = std::move(snap);
    }
}

void EditorApp::undoTerrainBrush()
{
    if (m_terrainUndoCount <= 0 || terrainBrushesLocked())
        return;
    HeightMap* working = m_terrain.editableWorking();
    SplatMap*  sp      = m_terrain.editableWorkingSplat();
    if (!working || !working->valid())
        return;
    TerrainBrushUndo& snap = m_terrainUndo[m_terrainUndoCount - 1];
    const int w = snap.x1 - snap.x0 + 1;
    const int h = snap.z1 - snap.z0 + 1;
    if (snap.heights && static_cast<int>(snap.height.size()) == w * h)
    {
        for (int z = 0; z < h; ++z)
        {
            for (int x = 0; x < w; ++x)
                working->setHeight(snap.x0 + x, snap.z0 + z, snap.height[static_cast<size_t>(z) * w + x]);
        }
    }
    if (snap.splat && sp && sp->valid() && static_cast<int>(snap.splatRgba.size()) == w * h * 4)
    {
        for (int z = 0; z < h; ++z)
        {
            for (int x = 0; x < w; ++x)
            {
                const size_t i = (static_cast<size_t>(z) * w + x) * 4u;
                sp->setTexel(snap.x0 + x, snap.z0 + z, snap.splatRgba[i], snap.splatRgba[i + 1], snap.splatRgba[i + 2], snap.splatRgba[i + 3]);
            }
        }
    }
    m_terrain.applyWorkingRect(snap.x0, snap.z0, snap.x1, snap.z1, snap.heights, snap.splat);
    m_terrainHeightDirty = snap.heights;
    m_terrainSplatDirty  = snap.splat;
    --m_terrainUndoCount;
    snap = TerrainBrushUndo{};
}

void EditorApp::drawWaterTools()
{
    if (!m_showWaterTools)
        return;
    ImGui::SetNextWindowSize(ImVec2(340.0f, 160.0f), ImGuiCond_FirstUseEver);
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

    const bool haveWater = m_water.chunksX() > 0;
    if (!haveWater)
        ImGui::TextDisabled("Generate or load terrain to shape the water.");

    WaterParams& wp      = m_water.params();
    float        baseAmp = 0.0f;
    for (int i = 0; i < kWaterWaveCount; ++i)
        baseAmp += wp.waves[i].amplitude;

    ImGui::BeginDisabled(!haveWater);
    float heightM = baseAmp * (wp.amplitudeScale > 0.0f ? wp.amplitudeScale : 0.0f);
    if (ImGui::SliderFloat("Wave height", &heightM, 0.0f, 6.0f, "%.2f m"))
        wp.amplitudeScale = baseAmp > 1.0e-5f ? heightM / baseAmp : 0.0f;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("How high the crests rise if every wave peaks together. The authored height is about %.2f m.", static_cast<double>(baseAmp));
    ImGui::SliderFloat("Wave speed", &wp.speedScale, 0.0f, 4.0f, "%.2fx");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("How fast the waves travel. 1 is the authored speed. 0 holds them still.");
    if (ImGui::Button("Reset waves"))
    {
        wp.amplitudeScale = 1.0f;
        wp.speedScale     = 1.0f;
    }
    ImGui::EndDisabled();
    ImGui::End();
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

    if (!m_scene.terrainPipeline().isValid())
        ImGui::TextDisabled("TerrainPipeline is not available.");

    if (!m_erosionAttempted)
    {
        m_erosionAttempted = true;
        if (!m_erosionPipe.create(renderer().device()))
            DE_LOG_INFO(LogCategory::Render, "Editor: erosion PSO invalid — CPU fallback");
    }

    const bool baking = m_genRunning.load();
    ImGui::SeparatorText("Generate");
    ImGui::BeginDisabled(baking);
    ImGui::Combo("Size (tiles)", &m_genTilesIndex, "1 (512 m)\0 2 (1 km)\0 4 (2 km)\0 8 (4 km)\0");
    ImGui::InputScalar("Seed", ImGuiDataType_U32, &m_terrainSeed);
    ImGui::SliderFloat("Cell size (m)", &m_genCellSize, 0.5f, 4.0f, "%.2f");
    ImGui::SliderFloat("Height scale (m)", &m_genHeightScale, 40.0f, 800.0f, "%.0f");
    ImGui::SliderFloat("Sea level (m)", &m_terrainSeaLevel, -20.0f, 80.0f, "%.1f");
    if (ImGui::CollapsingHeader("Shape layers", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::SliderInt("Shape octaves", &m_genFilter.shapeOctaves, 1, 8);
        ImGui::SliderFloat("Shape frequency", &m_genFilter.shapeFrequency, 0.5f, 12.0f, "%.2f");
        ImGui::SliderFloat("Shape amplitude", &m_genFilter.shapeAmplitude, 0.02f, 0.5f, "%.3f");
        ImGui::SliderFloat("Shape gain", &m_genFilter.shapeGain, 0.0f, 0.8f, "%.2f");
        ImGui::SliderFloat("Shape lacunarity", &m_genFilter.shapeLacunarity, 1.2f, 3.5f, "%.2f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("How much finer each shape layer is than the one under it.");
    }
    if (ImGui::CollapsingHeader("Gully layers", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::SliderInt("Gully octaves", &m_genFilter.gullyOctaves, 1, 8);
        ImGui::SliderFloat("Gully strength", &m_genFilter.gullyStrength, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Gully scale", &m_genFilter.gullyScale, 0.02f, 0.6f, "%.2f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Deformation is strength times scale. Scale also sets how wide a gully is.");
        ImGui::SliderFloat("Gully weight", &m_genFilter.gullyWeight, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Gully gain", &m_genFilter.gullyGain, 0.0f, 0.9f, "%.2f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("How much deformation each finer layer keeps.");
        ImGui::SliderFloat("Gully lacunarity", &m_genFilter.gullyLacunarity, 1.2f, 3.0f, "%.2f");
        ImGui::SliderFloat("Cell scale", &m_genFilter.gullyCellScale, 0.2f, 2.0f, "%.2f");
        ImGui::SliderFloat("Normalization", &m_genFilter.gullyNormalization, 0.0f, 0.95f, "%.2f");
        ImGui::SliderFloat("Detail", &m_genFilter.gullyDetail, 0.5f, 3.0f, "%.2f");
        ImGui::SliderFloat("Depth bias", &m_genFilter.gullyDepthBias, 0.0f, 1.5f, "%.2f");
        ImGui::SliderFloat("Altitude start", &m_genFilter.gullyAltitudeStart, -1.0f, 1.0f, "%.2f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Where gullies begin. -1 is the basin, +1 is the peaks. Lower this to cut the foothills.");
        ImGui::SliderFloat("Altitude full", &m_genFilter.gullyAltitudeFull, -1.0f, 1.0f, "%.2f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Where gullies reach full strength. Keep this above Altitude start.");
    }
    ImGui::SliderInt("Thermal iterations", &m_genThermal, 1, 80);
    ImGui::SliderInt("Hydraulic iterations (GPU)", &m_genHydroIters, 1, 96);
    if (!m_erosionPipe.isValid())
        ImGui::SliderInt("Hydraulic max steps (CPU)", &m_genHydroSteps, 4, 128);
    ImGui::EndDisabled();
    if (!baking)
    {
        if (ImGui::Button("Generate world"))
            startGenerateWorld();
        ImGui::SameLine();
        if (ImGui::Button("Create small FBM"))
            createEditorTerrain();
        if (m_haveTerrain && m_terrain.valid())
        {
            ImGui::SameLine();
            if (ImGui::Button("Frame terrain"))
                frameCameraOnTerrain();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Move the 3D camera to see the whole heightfield and sea.");
        }
    }
    else
    {
        ImGui::ProgressBar(m_genProgress.load(), ImVec2(-1.0f, 0.0f), m_genPhase);
        if (ImGui::Button("Cancel"))
            m_genCancel.store(true);
        ImGui::TextDisabled("Brushes disabled until generate applies.");
    }

    if (!m_haveTerrain)
    {
        ImGui::TextWrapped("No world terrain. Outdoor 3D scenes use the grey ground plane until you create one.");
        ImGui::End();
        return;
    }

    const HeightMap* hm = m_terrain.editableWorking();
    if (!hm)
        hm = &m_terrain.coarse();
    ImGui::Text("Heightfield %ux%u  cell %.2f m  tiles %ux%u", hm->width(), hm->height(), hm->cellSize(), m_terrain.tilesX(), m_terrain.tilesZ());
    ImGui::BeginDisabled(baking);
    if (ImGui::Button("Remove terrain"))
    {
        ImGui::EndDisabled();
        removeEditorTerrain();
        ImGui::End();
        return;
    }
    ImGui::EndDisabled();

    ImGui::SeparatorText("Layers");
    auto internAndPack = [&]() {
        rebuildTerrainGpuFromSurface();
    };
    auto drawMapSlot = [&](int layer, const char* id, const char* label, AssetRef<Image>& img, std::string& path) {
        ImGui::PushID(id);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        ImGui::SameLine();
        if (img && img->valid())
            ImGui::Text("%ux%u", img->width(), img->height());
        else if (!path.empty())
            ImGui::TextDisabled("%s", path.c_str());
        else
            ImGui::TextDisabled("none");
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_FOLDER_OPEN "  Load"))
        {
            std::filesystem::path file;
            if (pickImagePath(static_cast<HWND>(window().nativeHandle()), file))
            {
                AssetRef<Image> loaded = loadTerrainMapImage(assets(), file);
                if (loaded && loaded->valid())
                {
                    img  = loaded;
                    path = terrainMapKey(assets(), file);
                    internAndPack();
                }
                else
                    DE_LOG_ERROR(LogCategory::Render, "Editor: failed to load terrain map '{}'", file.string());
            }
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!(img && img->valid()) && path.empty());
        if (ImGui::Button("Clear"))
        {
            img.reset();
            path.clear();
            internAndPack();
        }
        ImGui::EndDisabled();
        ImGui::PopID();
        (void)layer;
    };

    for (int i = 0; i < Terrain::kMaxTerrainLayers; ++i)
    {
        ImGui::PushID(i);
        if (ImGui::TreeNode(kLayerNames[i]))
        {
            drawMapSlot(i, "albedo", "Albedo", m_terrainSurface.albedo[i], m_terrainAlbedoPath[i]);
            drawMapSlot(i, "normal", "Normal", m_terrainSurface.normal[i], m_terrainNormalPath[i]);
            drawMapSlot(i, "orm", "ORM", m_terrainSurface.orm[i], m_terrainOrmPath[i]);
            float tiling = m_terrainMaterial.layer(i).tiling;
            if (ImGui::SliderFloat("Tiling", &tiling, 1.0f, 64.0f, "%.1f"))
            {
                m_terrainMaterial.layer(i).tiling = tiling;
                m_terrainSurface.layers[i].tiling = tiling;
            }
            if (ImGui::ColorEdit3("Tint", m_terrainMaterial.layer(i).tint))
                std::memcpy(m_terrainSurface.layers[i].tint, m_terrainMaterial.layer(i).tint, sizeof(float) * 4);
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    ImGui::SeparatorText("Blend");
    Terrain::TerrainMaterialParams& params = m_terrainMaterial.params();
    ImGui::SliderFloat("Height blend k", &params.heightBlendK, 0.0f, 2.0f, "%.2f");
    ImGui::SliderFloat("Height blend t", &params.heightBlendT, 0.01f, 0.5f, "%.3f");
    ImGui::SliderFloat("Triplanar slope", &params.triplanarSlope, 0.0f, 1.0f, "%.2f");
    m_terrainSurface.params = params;

    ImGui::SeparatorText("Splat");
    ImGui::SliderFloat("Dirt max", &m_splatRules.dirtMax, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Grass min", &m_splatRules.grassMin, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Grass max", &m_splatRules.grassMax, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Snow min", &m_splatRules.snowMin, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Rock slope", &m_splatRules.rockSlope, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Blend", &m_splatRules.blend, 0.0f, 0.5f, "%.2f");
    ImGui::BeginDisabled(baking);
    if (ImGui::Button("Generate from height"))
    {
        HeightMap* working = m_terrain.editableWorking();
        SplatMap*  splat   = m_terrain.editableWorkingSplat();
        SplatMap   built;
        if (working && working->valid() && built.generateFromHeight(*working, m_splatRules) && m_terrain.setWorkingSplat(std::move(built)))
        {
            (void)splat;
            m_terrainSplatDirty = true;
            uploadTerrainSplatGpu();
        }
        else
            DE_LOG_ERROR(LogCategory::Render, "Editor: generateFromHeight failed");
    }

    ImGui::Combo("Paint layer", &m_terrainPaintLayer, "Dirt\0Grass\0Rock\0Snow\0");
    if (m_terrainPaintLayer < 0)
        m_terrainPaintLayer = 0;
    if (m_terrainPaintLayer >= Terrain::kMaxTerrainLayers)
        m_terrainPaintLayer = Terrain::kMaxTerrainLayers - 1;
    ImGui::Checkbox("Paint lower", &m_terrainPaintLower);
    bool paintOn = m_terrainBrush == TerrainBrushMode::Paint;
    if (ImGui::Checkbox("Paint brush", &paintOn))
        m_terrainBrush = paintOn ? TerrainBrushMode::Paint : TerrainBrushMode::None;

    ImGui::SeparatorText("Sculpt");
    int sculpt = 0;
    if (m_terrainBrush == TerrainBrushMode::SculptRaise)
        sculpt = 1;
    else if (m_terrainBrush == TerrainBrushMode::SculptLower)
        sculpt = 2;
    else if (m_terrainBrush == TerrainBrushMode::SculptSmooth)
        sculpt = 3;
    const char* sculptModes[] = { "Off", "Raise", "Lower", "Smooth" };
    if (ImGui::Combo("Sculpt mode", &sculpt, sculptModes, 4))
    {
        if (sculpt == 1)
            m_terrainBrush = TerrainBrushMode::SculptRaise;
        else if (sculpt == 2)
            m_terrainBrush = TerrainBrushMode::SculptLower;
        else if (sculpt == 3)
            m_terrainBrush = TerrainBrushMode::SculptSmooth;
        else if (m_terrainBrush != TerrainBrushMode::Paint)
            m_terrainBrush = TerrainBrushMode::None;
    }
    ImGui::SliderFloat("Brush radius (m)", &m_terrainBrushRadius, 0.5f, 32.0f, "%.1f");
    ImGui::SliderFloat("Brush strength", &m_terrainBrushStrength, 0.05f, 1.0f, "%.2f");
    if (m_terrainBrush != TerrainBrushMode::None)
        ImGui::TextDisabled("Hold LMB on the heightfield. Off-map is a no-op. Ctrl+Z undoes the last stroke.");
    ImGui::EndDisabled();

    ImGui::End();
}
