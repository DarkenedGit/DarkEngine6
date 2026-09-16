#include "EditorApp.h"

#include "Assets/GltfMaterialSave.h"
#include "Assets/Material.h"
#include "Assets/Model.h"
#include "Animation/AnimGraphComponent.h"
#include "Core/EntityPins.h"
#include "Core/Log.h"
#include "Editor/EditorInternals.h"
#include "ECS/Components.h"
#include "Math/AABox3f.h"
#include "Math/Quaternion.h"
#include "Math/Vector3f.h"
#include "Render/GpuResourceCache.h"
#include "Render/GpuUpload.h"
#include "Ui/Icons.h"

#include <imgui.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <commdlg.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>

using namespace Dark;
using namespace Math;

namespace
{

bool pickGltfPath(HWND owner, bool save, const std::filesystem::path& suggested, std::filesystem::path& out)
{
    wchar_t file[MAX_PATH];
    file[0] = 0;
    if (save && !suggested.empty())
    {
        const std::wstring w = suggested.wstring();
        wcsncpy_s(file, w.c_str(), _TRUNCATE);
    }

    OPENFILENAMEW ofn{};
    ofn.lStructSize  = sizeof(ofn);
    ofn.hwndOwner    = owner;
    ofn.lpstrFilter  = L"glTF (*.gltf;*.glb)\0*.gltf;*.glb\0All files (*.*)\0*.*\0";
    ofn.lpstrFile    = file;
    ofn.nMaxFile     = MAX_PATH;
    ofn.lpstrDefExt  = L"gltf";
    ofn.Flags        = OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_HIDEREADONLY
        | (save ? OFN_OVERWRITEPROMPT : (OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST));
    ofn.lpstrTitle   = save ? L"Save glTF Model" : L"Load glTF Model";

    const BOOL ok = save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
    if (!ok)
        return false;
    out = std::filesystem::path(file);
    return true;
}

const char* alphaModeLabel(MaterialAlphaMode mode)
{
    switch (mode)
    {
    case MaterialAlphaMode::Mask:
        return "Mask";
    case MaterialAlphaMode::Blend:
        return "Blend";
    case MaterialAlphaMode::Opaque:
    default:
        return "Opaque";
    }
}

} // namespace

AnimGraphComponent* EditorApp::selectedAnimGraph()
{
    if (!m_selected.valid() || !world().alive(m_selected))
        return nullptr;
    return world().get<AnimGraphComponent>(m_selected);
}

bool EditorApp::attachAnimGraph(Entity e, const AssetRef<Model>& model)
{
    if (!e.valid() || !world().alive(e) || !model || !model->skeleton())
        return false;

    AssetRef<AnimGraphDef> graph;
    const std::string virt = assets().virtualPathFromAbsolute(model->sourcePath());
    if (!virt.empty())
        graph = assets().tryLoadAnimGraphForModel(virt);
    if (!graph)
    {
        std::filesystem::path sidecar = model->sourcePath();
        if (!sidecar.empty())
        {
            sidecar.replace_extension(".anim.json");
            std::error_code ec;
            if (std::filesystem::is_regular_file(sidecar, ec) && !ec)
                graph = assets().loadAnimGraphFile(sidecar);
        }
    }

    AnimGraphComponent ag;
    ag.model    = model;
    ag.animSet  = model->animationSet();
    ag.graphDef = graph;
    if (graph)
        ag.graph.bind(graph.get(), model->skeleton());
    else if (ag.animSet)
    {
        ag.graph.player().bind(model->skeleton(), ag.animSet.get());
        if (!ag.graph.player().play("Idle", 0.0f))
            ag.graph.player().playIndex(0, 0.0f);
    }
    ag.graph.setApplyRootMotion(false);
    world().emplace<AnimGraphComponent>(e, std::move(ag));
    DE_LOG_INFO("Editor: animation graph {} for '{}'", graph ? "attached" : "missing (clips only)", model->sourcePath().string());
    return true;
}

bool EditorApp::ensureAnimGraphOnSelected()
{
    if (selectedAnimGraph())
        return true;
    if (!m_selected.valid() || !world().alive(m_selected))
        return false;
    const ModelComponent* mc = world().get<ModelComponent>(m_selected);
    if (!mc)
        return false;
    const AssetRef<Model> model = assets().getAs<Model>(mc->modelAssetID);
    if (!model)
        return false;
    return attachAnimGraph(m_selected, model);
}

AssetRef<Model> EditorApp::selectedModel()
{
    if (!m_selected.valid() || !world().alive(m_selected))
        return {};
    const ModelComponent* mc = world().get<ModelComponent>(m_selected);
    if (!mc)
        return {};
    return assets().getAs<Model>(mc->modelAssetID);
}

AssetRef<Material> EditorApp::meshMaterialOf(Entity e)
{
    if (!e.valid() || !world().alive(e))
        return {};
    const MeshComponent* mc = world().get<MeshComponent>(e);
    if (!mc || mc->matAssetID == NULL_ASSET)
        return {};
    return assets().getAs<Material>(mc->matAssetID);
}

bool EditorApp::meshMaterialShared(AssetID id)
{
    if (id == NULL_ASSET)
        return true;
    if (m_propMaterial && id == m_propMaterial->id)
        return true;
    if (m_groundMaterial && id == m_groundMaterial->id)
        return true;
    int users = 0;
    world().each<MeshComponent>([&](Entity, MeshComponent& mc) {
        if (mc.matAssetID == id)
            ++users;
    });
    return users > 1;
}

AssetRef<Material> EditorApp::ensureUniqueMeshMaterial(Entity e)
{
    MeshComponent* mc = (e.valid() && world().alive(e)) ? world().get<MeshComponent>(e) : nullptr;
    if (!mc)
        return {};

    AssetRef<Material> src = assets().getAs<Material>(mc->matAssetID);
    if (!src)
        src = m_propMaterial;
    if (!src)
        return {};

    if (mc->matAssetID != NULL_ASSET && !meshMaterialShared(mc->matAssetID))
        return src;

    auto cloned = std::make_shared<Material>();
    if (!cloned->copyFrom(*src))
        return src;
    if (const EditorObjectComponent* so = findObject(e))
        cloned->setBaseColor(so->color[0], so->color[1], so->color[2], cloned->baseColor()[3]);
    cloned = assets().internMaterial(cloned);
    if (!cloned || cloned->id == NULL_ASSET || !renderer().gpuResources().ensureMaterial(cloned))
        return src;
    setMeshMaterial(world(), pins(), assets(), e, cloned->id);
    return cloned;
}

const Model::Part* EditorApp::selectedModelPart()
{
    AssetRef<Model> model = selectedModel();
    if (!model)
        return nullptr;
    if (m_selectedPart < 0)
        return nullptr;
    return model->partAt(static_cast<uint32_t>(m_selectedPart));
}

void EditorApp::frameCameraOnModel(const Model& model, const TransformComponent& xf)
{
    AABox3f b = model.bounds();
    if (!b.IsValid())
        return;
    const Vector3f localC = b.Center();
    const Vector3f localE = b.Extents();
    const Vector3f worldC = xf.position + Vector3f(localC.x * xf.scale.x, localC.y * xf.scale.y, localC.z * xf.scale.z);
    float r = localE.Magnitude();
    if (r < 0.5f)
        r = 0.5f;
    r *= xf.scale.x > xf.scale.y ? xf.scale.x : xf.scale.y;
    const Vector3f eye = worldC + Vector3f(r * 1.7f, r * 1.15f, -r * 1.9f);
    m_camera.LookAt(eye, worldC, Vector3f::Y_AXIS);
}

bool EditorApp::spawnLoadedModel(const AssetRef<Model>& model)
{
    if (!model || !model->valid())
        return false;

    applySceneMode(SceneMode::Scene3D);
    Entity e = world().createEntity();
    world().emplace<TagComponent>(e, "glTF");
    TransformComponent xf{};
    xf.position = Vector3f{ 0.0f, 0.0f, 0.0f };
    xf.scale    = Vector3f{ 1.0f, 1.0f, 1.0f };
    world().emplace<TransformComponent>(e, xf);

    ModelComponent mc{};
    mc.modelAssetID = model->id;
    mc.castShadow   = model->hasOpaque();
    setModelComponent(world(), pins(), assets(), e, mc);
    attachAnimGraph(e, model);

    m_selected          = e;
    m_selectedPart      = 0;
    m_showModelParts    = true;
    m_showMaterialEditor = true;
    frameCameraOnModel(*model, xf);
    DE_LOG_INFO("Editor: loaded model '{}' ({} parts)", model->sourcePath().string(), model->partCount());
    return true;
}

bool EditorApp::loadGltfModel()
{
    std::filesystem::path path;
    if (!pickGltfPath(static_cast<HWND>(window().nativeHandle()), false, {}, path))
        return false;

    AssetRef<Model> model = loadAndUploadModelFile(renderer(), assets(), path);
    if (!model || !model->valid())
    {
        DE_LOG_ERROR("Editor: failed to load '{}'", path.string());
        return false;
    }
    return spawnLoadedModel(model);
}

bool EditorApp::saveGltfModel()
{
    AssetRef<Model> model = selectedModel();
    if (!model)
    {
        DE_LOG_WARN("Editor: no model selected to save");
        return false;
    }
    std::filesystem::path path = model->sourcePath();
    if (path.empty())
        return saveGltfModelAs();
    std::string err;
    if (!saveGltfMaterials(path, *model, &err))
    {
        DE_LOG_ERROR("Editor: save model failed — {}", err);
        return false;
    }
    if (m_sfxSave)
        audio().play2D(m_sfxSave, 0.45f);
    return true;
}

bool EditorApp::saveGltfModelAs()
{
    AssetRef<Model> model = selectedModel();
    if (!model)
    {
        DE_LOG_WARN("Editor: no model selected to save");
        return false;
    }
    std::filesystem::path dest;
    if (!pickGltfPath(static_cast<HWND>(window().nativeHandle()), true, model->sourcePath(), dest))
        return false;

    const std::filesystem::path src = model->sourcePath();
    if (!src.empty() && src != dest)
    {
        std::error_code ec;
        std::filesystem::copy_file(src, dest, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec)
        {
            DE_LOG_ERROR("Editor: copy to '{}' failed — {}", dest.string(), ec.message());
            return false;
        }
    }

    std::string err;
    if (!saveGltfMaterials(dest, *model, &err))
    {
        DE_LOG_ERROR("Editor: save model as failed — {}", err);
        return false;
    }
    model->setSourcePath(dest);
    if (m_sfxSave)
        audio().play2D(m_sfxSave, 0.45f);
    return true;
}

void EditorApp::drawModelPartsPanel()
{
    if (!m_showModelParts)
        return;
    if (!ImGui::Begin("Model Parts", &m_showModelParts))
    {
        ImGui::End();
        return;
    }

    AssetRef<Model> model = selectedModel();
    if (!model)
    {
        ImGui::TextWrapped("Select a glTF model (or File → Load Model) to inspect parts.");
        ImGui::End();
        return;
    }

    ImGui::TextUnformatted(model->sourcePath().filename().string().c_str());
    ImGui::Text("%u parts  (%zu opaque, %zu translucent)", model->partCount(), model->opaque().size(), model->translucent().size());
    ImGui::Separator();

    const uint32_t n = model->partCount();
    for (uint32_t i = 0; i < n; ++i)
    {
        const Model::Part* part = model->partAt(i);
        if (!part)
            continue;
        ImGui::PushID(static_cast<int>(i));
        const bool selected = m_selectedPart == static_cast<int>(i);
        char label[160];
        const char* kind = part->skinned ? "skinned" : (part->translucent ? "blend" : "opaque");
        if (part->materialIndex >= 0)
            std::snprintf(label, sizeof(label), "%s  [%s]  mat %d", part->name.c_str(), kind, part->materialIndex);
        else
            std::snprintf(label, sizeof(label), "%s  [%s]", part->name.c_str(), kind);
        if (ImGui::Selectable(label, selected))
        {
            m_selectedPart       = static_cast<int>(i);
            m_showMaterialEditor = true;
        }
        ImGui::PopID();
    }
    ImGui::End();
}

void EditorApp::drawMaterialPanel()
{
    if (!m_showMaterialEditor)
        return;
    if (!ImGui::Begin("Material", &m_showMaterialEditor))
    {
        ImGui::End();
        return;
    }

    if (!m_selected.valid() || !world().alive(m_selected))
    {
        ImGui::TextWrapped("Select an object to edit its material.");
        ImGui::End();
        return;
    }

    Material* mat          = nullptr;
    bool      meshInstance = false;
    if (world().get<ModelComponent>(m_selected))
    {
        const Model::Part* part = selectedModelPart();
        if (!part || !part->material)
        {
            ImGui::TextWrapped("Select a part in Model Parts to edit its material.");
            ImGui::End();
            return;
        }
        mat = part->material.get();
        ImGui::Text("Material %d", part->materialIndex);
        if (!part->name.empty())
            ImGui::TextUnformatted(part->name.c_str());
    }
    else if (world().get<MeshComponent>(m_selected))
    {
        AssetRef<Material> meshMat = meshMaterialOf(m_selected);
        if (!meshMat)
            meshMat = m_propMaterial;
        if (!meshMat)
        {
            ImGui::TextWrapped("This object has no material.");
            ImGui::End();
            return;
        }
        mat          = meshMat.get();
        meshInstance = true;
        if (const EditorObjectComponent* so = findObject(m_selected))
            ImGui::Text("Selected: %s", toString(so->type));
        else
            ImGui::TextUnformatted("Selected: Mesh");
        if (meshMaterialShared(meshMat->id))
            ImGui::TextDisabled("Shared until edited — first change makes a unique copy.");
    }
    else
    {
        ImGui::TextWrapped("This object has no material.");
        ImGui::End();
        return;
    }

    ImGui::Separator();

    float color[4] = { mat->baseColor()[0], mat->baseColor()[1], mat->baseColor()[2], mat->baseColor()[3] };
    if (meshInstance)
    {
        if (const EditorObjectComponent* so = findObject(m_selected))
        {
            color[0] = so->color[0];
            color[1] = so->color[1];
            color[2] = so->color[2];
        }
    }
    if (ImGui::ColorEdit4("Base Color", color))
    {
        Material* edit = mat;
        if (meshInstance)
        {
            if (AssetRef<Material> unique = ensureUniqueMeshMaterial(m_selected))
                edit = unique.get();
            if (EditorObjectComponent* so = findObject(m_selected))
            {
                so->color[0] = color[0];
                so->color[1] = color[1];
                so->color[2] = color[2];
            }
        }
        if (edit)
            edit->setBaseColor(color[0], color[1], color[2], color[3]);
    }

    float metallic  = mat->metallic();
    float roughness = mat->roughness();
    if (ImGui::SliderFloat("Metallic", &metallic, 0.0f, 1.0f))
    {
        Material* edit = mat;
        if (meshInstance)
        {
            if (AssetRef<Material> unique = ensureUniqueMeshMaterial(m_selected))
                edit = unique.get();
        }
        if (edit)
            edit->setMetallicRoughness(metallic, roughness);
    }
    if (ImGui::SliderFloat("Roughness", &roughness, 0.0f, 1.0f))
    {
        Material* edit = mat;
        if (meshInstance)
        {
            if (AssetRef<Material> unique = ensureUniqueMeshMaterial(m_selected))
                edit = unique.get();
        }
        if (edit)
            edit->setMetallicRoughness(metallic, roughness);
    }

    int         mode      = static_cast<int>(mat->alphaMode());
    const char* modes[]   = { "Opaque", "Mask", "Blend" };
    if (ImGui::Combo("Alpha Mode", &mode, modes, 3))
    {
        Material* edit = mat;
        if (meshInstance)
        {
            if (AssetRef<Material> unique = ensureUniqueMeshMaterial(m_selected))
                edit = unique.get();
        }
        if (edit)
            edit->setAlphaMode(static_cast<MaterialAlphaMode>(mode));
    }
    ImGui::TextDisabled(meshInstance ? "Edits apply live to this object." : "Color / metal / rough update live. Alpha mode is stored on Save.");
    ImGui::End();
}
