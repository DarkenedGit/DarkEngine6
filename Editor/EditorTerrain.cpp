#include "EditorApp.h"

#include "Assets/Image.h"
#include "Core/Log.h"
#include "Editor/EditorInternals.h"
#include "Math/MathHelper.h"
#include "Ui/Icons.h"

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

} // namespace

void EditorApp::removeEditorTerrain()
{
    if (!m_haveTerrain && !m_terrainMaterial.isValid())
    {
        m_terrain              = {};
        m_terrainMaterial      = {};
        m_splat                = {};
        m_terrainSurface       = {};
        m_haveTerrain          = false;
        m_terrainHeightDirty   = false;
        m_terrainSplatDirty    = false;
        return;
    }

    renderer().waitForGpu();
    m_terrain            = {};
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

bool EditorApp::uploadTerrainSplatGpu()
{
    if (!m_haveTerrain || !m_splat.valid())
        return false;
    if (!m_terrainMaterial.uploadSplat(renderer(), m_splat))
    {
        DE_LOG_ERROR(LogCategory::Render, "Editor: terrain splat upload failed");
        return false;
    }
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
    return true;
}

bool EditorApp::createEditorTerrain()
{
    removeEditorTerrain();

    Terrain::TerrainDesc terrainDesc;
    terrainDesc.chunkCells       = 16;
    terrainDesc.lodDistanceCount = 5;
    terrainDesc.lodDistances[0]  = 40.0f;
    terrainDesc.lodDistances[1]  = 80.0f;
    terrainDesc.lodDistances[2]  = 160.0f;
    terrainDesc.lodDistances[3]  = 320.0f;
    terrainDesc.lodDistances[4]  = 640.0f;

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
    terrainDesc.heightMap = std::move(base);

    if (!m_splat.generateFromHeight(terrainDesc.heightMap, m_splatRules))
    {
        DE_LOG_ERROR(LogCategory::Render, "Editor: splat generate failed");
        return false;
    }
    if (!m_terrain.create(std::move(terrainDesc)))
    {
        DE_LOG_ERROR(LogCategory::Render, "Editor: terrain create failed");
        return false;
    }
    if (!m_terrainMaterial.createDefault(renderer(), m_splat))
    {
        DE_LOG_ERROR(LogCategory::Render, "Editor: terrain material create failed");
        removeEditorTerrain();
        return false;
    }
    m_terrainMaterial.setLayerSamplingRaw(renderer().device(), renderer().debugState().legacyUnormAlbedo);
    m_terrain.updateLod(m_camera.GetPosition());
    if (!m_terrain.createGpu(renderer()))
    {
        DE_LOG_ERROR(LogCategory::Render, "Editor: terrain GPU upload failed");
        removeEditorTerrain();
        return false;
    }
    if (m_terrain.heightTexture().valid())
        renderer().setHeightSrv(m_terrain.heightTexture().cpuHandle());

    m_terrainSurface = {};
    m_terrainSurface.params.heightBlendK = 0.0f;
    for (int i = 0; i < Terrain::kMaxTerrainLayers; ++i)
        m_terrainSurface.layers[i] = m_terrainMaterial.layer(i);
    defaultSidecarNames(m_sceneName, m_terrainHeightFile, m_terrainSplatFile);
    m_haveTerrain        = true;
    m_terrainHeightDirty = false;
    m_terrainSplatDirty  = false;
    DE_LOG_INFO(LogCategory::Render, "Editor: created 129x129 terrain (checkers)");
    return true;
}

void EditorApp::syncTerrainLod()
{
    if (!m_haveTerrain)
        return;

    m_terrain.updateLod(m_camera.GetPosition());
    const bool meshDirty   = m_terrain.needsRebuild();
    const bool heightDirty = m_terrainHeightDirty;
    const bool splatDirty  = m_terrainSplatDirty;
    if (!meshDirty && !heightDirty && !splatDirty)
        return;

    renderer().waitForGpu();
    if (meshDirty)
    {
        m_terrain.rebuildDirtyCpuMeshes();
        if (!m_terrain.uploadDirty(renderer()))
            DE_LOG_ERROR(LogCategory::Render, "Editor: terrain upload failed");
    }
    if (heightDirty)
    {
        if (!m_terrain.uploadHeightTexture(renderer()))
            DE_LOG_ERROR(LogCategory::Render, "Editor: height texture upload failed");
        else if (m_terrain.heightTexture().valid())
            renderer().setHeightSrv(m_terrain.heightTexture().cpuHandle());
        m_terrainHeightDirty = false;
    }
    if (splatDirty)
        uploadTerrainSplatGpu();
}

void EditorApp::applyTerrainBrush(float dt)
{
    if (!m_haveTerrain || m_terrainBrush == TerrainBrushMode::None)
        return;
    if (dt < 0.0f)
        dt = 0.0f;

    Vector3f hit{};
    if (!groundHitFromMouse(hit))
        return;

    const HeightMap& hm = m_terrain.heightMap();
    if (!hm.valid() || !hm.containsXZ(hit.x, hit.z))
        return;

    const float radius = m_terrainBrushRadius > 0.05f ? m_terrainBrushRadius : 0.05f;
    const float strength = Math::Clamp(m_terrainBrushStrength, 0.0f, 1.0f);

    if (m_terrainBrush == TerrainBrushMode::Paint)
    {
        float fx = 0.0f;
        float fz = 0.0f;
        hm.worldToSample(hit.x, hit.z, fx, fz);
        const float radiusSamples = hm.cellSize() > 0.0f ? (radius / hm.cellSize()) : radius;
        float amount = strength * dt * 4.0f;
        if (m_terrainPaintLower)
            amount = -amount;
        amount = Math::Clamp(amount, -1.0f, 1.0f);
        m_splat.paintDisk(fx, fz, radiusSamples, m_terrainPaintLayer, amount);
        m_terrainSplatDirty = true;
        return;
    }

    if (m_terrainBrush == TerrainBrushMode::SculptSmooth)
    {
        const float alpha = Math::Clamp(strength * dt * 4.0f, 0.0f, 1.0f);
        m_terrain.heightMap().smoothDisk(hit.x, hit.z, radius, alpha);
    }
    else
    {
        float delta = strength * dt * 12.0f;
        if (m_terrainBrush == TerrainBrushMode::SculptLower)
            delta = -delta;
        m_terrain.heightMap().addDisk(hit.x, hit.z, radius, delta);
    }

    float fx = 0.0f;
    float fz = 0.0f;
    m_terrain.heightMap().worldToSample(hit.x, hit.z, fx, fz);
    const float radiusSamples = m_terrain.heightMap().cellSize() > 0.0f ? (radius / m_terrain.heightMap().cellSize()) : radius;
    const int x0 = static_cast<int>(floorf(fx - radiusSamples));
    const int x1 = static_cast<int>(ceilf(fx + radiusSamples));
    const int z0 = static_cast<int>(floorf(fz - radiusSamples));
    const int z1 = static_cast<int>(ceilf(fz + radiusSamples));
    m_terrain.markHeightDirtyRect(x0, z0, x1, z1);
    m_terrainHeightDirty = true;
}

void EditorApp::fillTerrainSceneDesc(SceneFileData& data) const
{
    data.hasTerrain = false;
    if (!m_haveTerrain || data.mode == SceneMode::Scene2D)
        return;

    TerrainSceneDesc desc{};
    desc.bindLayout     = TerrainSceneDesc::kBindLayoutV1;
    desc.chunkCells     = m_terrain.chunkCells() > 0 ? m_terrain.chunkCells() : 16;
    desc.heightBlendK   = m_terrainMaterial.params().heightBlendK;
    desc.heightBlendT   = m_terrainMaterial.params().heightBlendT;
    desc.triplanarSlope = m_terrainMaterial.params().triplanarSlope;
    desc.heightFile     = m_terrainHeightFile;
    desc.splatFile      = m_terrainSplatFile;
    defaultSidecarNames(data.name, desc.heightFile, desc.splatFile);
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
    if (!m_haveTerrain)
        return true;
    std::string heightFile = m_terrainHeightFile;
    std::string splatFile  = m_terrainSplatFile;
    defaultSidecarNames(m_sceneName, heightFile, splatFile);

    const std::filesystem::path heightPath = sidecarPath(scenePath, heightFile);
    const std::filesystem::path splatPath  = sidecarPath(scenePath, splatFile);
    if (!m_terrain.heightMap().saveBinary(heightPath))
        return false;
    if (!m_splat.valid() || !saveRgbaPng(splatPath, m_splat.width(), m_splat.height(), m_splat.rgba()))
    {
        DE_LOG_ERROR(LogCategory::Render, "Editor: splat sidecar write failed");
        return false;
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
    defaultSidecarNames(data.name, heightFile, splatFile);

    const std::filesystem::path heightPath = sidecarPath(scenePath, heightFile);
    HeightMap height;
    if (!height.loadBinary(heightPath))
    {
        DE_LOG_ERROR(LogCategory::Render, "Editor: terrain height sidecar missing or invalid — no terrain");
        return false;
    }

    const std::filesystem::path splatPath = sidecarPath(scenePath, splatFile);
    std::error_code ec;
    if (std::filesystem::exists(splatPath, ec) && !ec)
    {
        Image splatImg;
        if (!splatImg.createFromFile(splatPath) || !splatImg.valid()
            || splatImg.width() != height.width() || splatImg.height() != height.height()
            || !m_splat.createFromRGBA(splatImg.width(), splatImg.height(), splatImg.pixels()))
        {
            DE_LOG_ERROR(LogCategory::Render, "Editor: splat sidecar '{}' invalid — generateFromHeight", splatPath.string());
            if (!m_splat.generateFromHeight(height, m_splatRules))
            {
                DE_LOG_ERROR(LogCategory::Render, "Editor: splat generate failed");
                return false;
            }
        }
    }
    else
    {
        DE_LOG_ERROR(LogCategory::Render, "Editor: missing splat '{}' — generateFromHeight", splatPath.string());
        if (!m_splat.generateFromHeight(height, m_splatRules))
        {
            DE_LOG_ERROR(LogCategory::Render, "Editor: splat generate failed");
            return false;
        }
    }

    Terrain::TerrainDesc terrainDesc;
    terrainDesc.chunkCells       = desc.chunkCells > 0 ? desc.chunkCells : 16;
    terrainDesc.lodDistanceCount = 5;
    terrainDesc.lodDistances[0]  = 40.0f;
    terrainDesc.lodDistances[1]  = 80.0f;
    terrainDesc.lodDistances[2]  = 160.0f;
    terrainDesc.lodDistances[3]  = 320.0f;
    terrainDesc.lodDistances[4]  = 640.0f;
    terrainDesc.heightMap        = std::move(height);
    if (!m_terrain.create(std::move(terrainDesc)))
    {
        DE_LOG_ERROR(LogCategory::Render, "Editor: terrain create from scene failed");
        removeEditorTerrain();
        return false;
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

    Texture2D splatTex;
    Image splatImg;
    if (!splatImg.createFromRGBA(m_splat.rgba(), m_splat.width(), m_splat.height(), m_splat.width() * 4u)
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
        ok = m_terrainMaterial.createDefault(renderer(), m_splat);
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
    m_terrain.updateLod(m_camera.GetPosition());
    if (!m_terrain.createGpu(renderer()))
    {
        DE_LOG_ERROR(LogCategory::Render, "Editor: terrain GPU upload from scene failed");
        removeEditorTerrain();
        return false;
    }
    if (m_terrain.heightTexture().valid())
        renderer().setHeightSrv(m_terrain.heightTexture().cpuHandle());

    m_terrainHeightFile  = heightFile;
    m_terrainSplatFile   = splatFile;
    m_haveTerrain        = true;
    m_terrainHeightDirty = false;
    m_terrainSplatDirty  = false;
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

    if (!m_scene.terrainPipeline().isValid())
        ImGui::TextDisabled("TerrainPipeline is not available.");

    if (!m_haveTerrain)
    {
        ImGui::TextWrapped("No world terrain. Outdoor 3D scenes use the grey ground plane until you create one.");
        if (ImGui::Button("Create terrain"))
            createEditorTerrain();
        ImGui::End();
        return;
    }

    const HeightMap& hm = m_terrain.heightMap();
    ImGui::Text("Heightfield %ux%u  cell %.2f m", hm.width(), hm.height(), hm.cellSize());
    if (ImGui::Button("Remove terrain"))
    {
        removeEditorTerrain();
        ImGui::End();
        return;
    }

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
    if (ImGui::Button("Generate from height"))
    {
        if (m_splat.generateFromHeight(m_terrain.heightMap(), m_splatRules))
            m_terrainSplatDirty = true;
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
        ImGui::TextDisabled("Hold LMB on the heightfield. Off-map is a no-op.");

    ImGui::End();
}
