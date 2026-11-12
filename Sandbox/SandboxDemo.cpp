#include "SandboxDemo.h"
#include "SandboxApp.h"
#include "SandboxInternal.h"

#include "Animation/AnimGraphComponent.h"
#include "Animation/SkeletonDebug.h"
#include "Assets/Material.h"
#include "Assets/Model.h"
#include "Core/EntityPins.h"
#include "Core/Log.h"
#include "Gameplay/HealthPack.h"
#include "Render/GpuUpload.h"
#include "Render/MeshGen.h"
#include "Render/ModelDraw.h"
#include "Render/Profile.h"
#include "Render/ScenePath.h"
#include "Save/PersistentId.h"
#include "Scene/EntityMaster.h"

#include <cstring>
#include <format>
#include <memory>
#include <vector>

using namespace Dark;
using namespace Math;

SandboxApp& SandboxDemo::app() const
{
    return *m_app;
}

bool SandboxDemo::createSandboxModels(int step)
{
    if (step == 0)
    {
        MeshData cubeData;
        if (!CreateCube(cubeData, 1.0f))
            return false;
        auto cubeMat = std::make_shared<Material>();
        if (!cubeMat->createFromAlbedoPath(app().assets(), "textures/dark_engine_cube.png", /*fallback*/ 64, 166, 242, 255))
            return false;
        cubeMat = app().assets().internMaterial(cubeMat, "runtime:/sandbox/cube-mat");
        AssetRef<Model> cubeModel = internAndUploadProceduralModel(app().renderer(), app().assets(), std::move(cubeData), cubeMat, "runtime:/sandbox/cube");
        if (!cubeModel)
            return false;
        m_cubeModelId = cubeModel->id;
        return true;
    }
    if (step == 1)
    {
        MeshData packData;
        if (!CreateCross(packData, 1.0f, 0.30f, 0.22f))
            return false;
        AssetRef<Material> packMat = internSolidMaterial(app().assets(), 214, 28, 36, 255, "runtime:/sandbox/pack-mat");
        AssetRef<Model> packModel  = internAndUploadProceduralModel(app().renderer(), app().assets(), std::move(packData), packMat, "runtime:/sandbox/pack");
        if (!packModel)
            return false;
        m_packModelId = packModel->id;
        return true;
    }
    if (step == 2)
    {
        MeshData lanternData;
        if (!CreateCube(lanternData, 1.0f))
            return false;
        auto lanternMat = std::make_shared<Material>();
        if (!lanternMat->createSolid(app().assets(), 220, 150, 60, 255))
            return false;
        lanternMat->setEmissive(1.0f);
        lanternMat = app().assets().internMaterial(lanternMat, "runtime:/sandbox/lantern-mat");
        if (!lanternMat || lanternMat->id == NULL_ASSET)
            return false;
        AssetRef<Model> lanternModel = internAndUploadProceduralModel(app().renderer(), app().assets(), std::move(lanternData), lanternMat, "runtime:/sandbox/lantern");
        if (!lanternModel)
            return false;
        m_lanternModelId = lanternModel->id;
        return true;
    }
    if (step == 3)
    {
        MeshData tracerData;
        if (!CreateSphere(tracerData, 0.5f, 8, 12))
        {
            DE_LOG_ERROR("SandboxApp: projectile tracer mesh failed");
            return true;
        }
        AssetRef<Material> tracerMat = internSolidMaterial(app().assets(), 255, 196, 48, 255, "runtime:/sandbox/tracer-mat");
        AssetRef<Model> tracerModel  = internAndUploadProceduralModel(app().renderer(), app().assets(), std::move(tracerData), tracerMat, "runtime:/sandbox/tracer");
        if (tracerModel)
            m_tracerModelId = tracerModel->id;
        else
            DE_LOG_ERROR("SandboxApp: projectile tracer model failed");
        return true;
    }
    return true;
}

void SandboxDemo::spawnGltfDemo(int step)
{
    auto spawn = [&](const char* virtualPath, const char* tag, float x, float z, float scale) {
        AssetRef<Model> model = loadAndUploadModel(app().renderer(), app().assets(), virtualPath);
        if (!model || !model->valid())
        {
            DE_LOG_WARN("SandboxApp: glTF '{}' not loaded", virtualPath);
            return;
        }
        const float groundY = app().m_terrain.heightAtWorld(x, z);
        const float y       = groundY + scale * 0.5f;
        Entity e = app().world().createEntity();
        app().world().emplace<TagComponent>(e, tag);
        app().world().emplace<TransformComponent>(e, Vector3f{ x, y, z }, Quaternion::IDENTITY, Vector3f{ scale, scale, scale });
        ModelComponent mc;
        mc.modelAssetID = model->id;
        mc.castShadow   = model->hasOpaque();
        app().world().emplace<ModelComponent>(e, mc);
        DE_LOG_INFO("SandboxApp: spawned {} at ({:.1f},{:.1f},{:.1f}) ground {:.1f}", tag, x, y, z, groundY);
    };

    // Sit on the terrain next to the player cube at the origin (default camera looks here).
    if (step == 0)
    {
        const EntityMaster cubeMaster = loadEntityMasterType("unit_cube");
        const std::string  cubeGltf = cubeMaster.gltf.empty() ? std::string("models/unit_cube.gltf") : cubeMaster.gltf;
        spawn(cubeGltf.c_str(), "GltfCube", 3.0f, 3.0f, 2.0f);
        return;
    }
    if (step == 1)
    {
        const EntityMaster glassMaster = loadEntityMasterType("unit_glass");
        const std::string  glassGltf = glassMaster.gltf.empty() ? std::string("models/unit_glass.gltf") : glassMaster.gltf;
        spawn(glassGltf.c_str(), "GltfGlass", 5.5f, 3.0f, 2.0f);
        return;
    }
    if (step == 2)
        spawnAnimatedDemo();
}

void SandboxDemo::onWiggleNotify(void*, const AnimNotify& n)
{
    if (!n.name || std::strcmp(n.name, "footstep") != 0)
        return;
    DE_LOG_INFO("SandboxApp: wiggle footstep t={:.2f}", n.time);
}

void SandboxDemo::spawnAnimatedDemo()
{
    const EntityMaster wiggleMaster = loadEntityMasterType("wiggle");
    const std::string  wiggleGltf = wiggleMaster.gltf.empty() ? std::string("models/wiggle.gltf") : wiggleMaster.gltf;
    const char*        kGltf = wiggleGltf.c_str();
    AssetRef<Model> model = loadAndUploadModel(app().renderer(), app().assets(), kGltf);
    if (!model || !model->valid())
    {
        DE_LOG_WARN("SandboxApp: animated glTF '{}' not loaded", kGltf);
        return;
    }
    AssetRef<AnimGraphDef> graph;
    if (!wiggleMaster.anim.empty())
        graph = app().assets().loadAnimGraph(wiggleMaster.anim);
    if (!graph)
        graph = app().assets().tryLoadAnimGraphForModel(kGltf);
    if (!graph)
        DE_LOG_WARN("SandboxApp: '{}' has no anim graph sidecar; clips still play if present", kGltf);

    constexpr float x = 8.0f;
    constexpr float z = 3.0f;
    constexpr float scale = 2.0f;
    const float groundY = app().m_terrain.heightAtWorld(x, z);
    Entity e = app().world().createEntity();
    app().world().emplace<TagComponent>(e, "Wiggle");
    app().world().emplace<TransformComponent>(e, Vector3f{ x, groundY, z }, Quaternion::IDENTITY, Vector3f{ scale, scale, scale });
    ModelComponent mc;
    mc.modelAssetID = model->id;
    mc.castShadow   = model->hasOpaque();
    app().world().emplace<ModelComponent>(e, mc);

    AnimGraphComponent ag;
    ag.model   = model;
    ag.animSet = model->animationSet();
    ag.graphDef = graph;
    if (graph && model->skeleton())
        ag.graph.bind(graph.get(), model->skeleton());
    else if (ag.animSet && model->skeleton())
    {
        ag.graph.player().bind(model->skeleton(), ag.animSet.get());
        if (!ag.graph.player().play("Idle", 0.0f))
            ag.graph.player().playIndex(0, 0.0f);
    }
    ag.graph.setApplyRootMotion(false);
    ag.graph.player().addListener(&SandboxDemo::onWiggleNotify, this);
    app().world().emplace<AnimGraphComponent>(e, std::move(ag));
    m_wiggle = e;
    DE_LOG_INFO("SandboxApp: spawned Wiggle at ({:.1f},{:.1f},{:.1f}) ground {:.1f} graph={}", x, groundY, z, groundY, graph ? "yes" : "no");
}

void SandboxDemo::updateWiggleAnim()
{
    if (!m_wiggle.valid())
        return;
    AnimGraphComponent* ag = app().world().get<AnimGraphComponent>(m_wiggle);
    if (!ag || !ag->graphDef)
        return;

    float speed = 0.0f;
    const bool uiKeys = app().uiOpen() && app().m_hud.m_imgui.isReady() && app().m_hud.m_imgui.wantCaptureKeyboard();
    if (!uiKeys && app().input().actionDown("anim_walk"))
        speed = 1.0f;
    else
    {
        const Vector3f v = app().localMotor() ? app().localMotor()->velocity() : Vector3f{};
        speed = Vector3f(v.x, 0.0f, v.z).Magnitude();
    }
    ag->graph.setFloat("speed", speed);
}

bool SandboxDemo::createSkeletonLineBuffers()
{
    ID3D12Device* device = app().renderer().device();
    if (!device)
        return false;
    constexpr uint64_t kMaxVerts = 1024;
    const uint64_t vbBytes = sizeof(Vector3f) * kMaxVerts;
    const uint64_t ibBytes = sizeof(uint32_t) * kMaxVerts;
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Height           = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels        = 1;
    desc.SampleDesc       = { 1, 0 };
    desc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    for (int i = 0; i < 2; ++i)
    {
        desc.Width = vbBytes;
        if (FAILED(device->CreateCommittedResource(
                &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_skelLineVb[i]))))
            return false;
        desc.Width = ibBytes;
        if (FAILED(device->CreateCommittedResource(
                &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_skelLineIb[i]))))
            return false;
        m_skelLineVbv[i].BufferLocation = m_skelLineVb[i]->GetGPUVirtualAddress();
        m_skelLineVbv[i].StrideInBytes  = sizeof(Vector3f);
        m_skelLineVbv[i].SizeInBytes    = static_cast<UINT>(vbBytes);
        m_skelLineIbv[i].BufferLocation = m_skelLineIb[i]->GetGPUVirtualAddress();
        m_skelLineIbv[i].Format         = DXGI_FORMAT_R32_UINT;
        m_skelLineIbv[i].SizeInBytes    = static_cast<UINT>(ibBytes);
    }
    return true;
}

void SandboxDemo::drawSkeletonOverlay(ID3D12GraphicsCommandList* cmd, const Matrix4f& viewProj)
{
    if (!app().m_showSkeleton || !cmd || !m_skelLinePipeline.isValid() || !m_skelLineVb[0])
        return;

    const GpuScope skeleton(cmd, "Skeleton", ProfileColor::Skeleton);
    SkeletonDebugLines lines;
    app().world().each<ModelComponent>([&](Entity e, ModelComponent& mc) {
        if (!mc.visible)
            return;
        const TransformComponent* xf = app().world().get<TransformComponent>(e);
        const Model* model = app().assets().find<Model>(mc.modelAssetID);
        if (!xf || !model || !model->skinned() || !model->skeleton())
            return;
        const AnimPose* pose = skinnedPose(*model, app().world().get<AnimGraphComponent>(e));
        if (!pose || pose->boneCount == 0)
            return;
        SkeletonDebugLines one;
        collectSkeletonDebugLines(*model->skeleton(), *pose, makeWorldMatrix(*xf), 0.25f, one);
        lines.bones.insert(lines.bones.end(), one.bones.begin(), one.bones.end());
        lines.axisX.insert(lines.axisX.end(), one.axisX.begin(), one.axisX.end());
        lines.axisY.insert(lines.axisY.end(), one.axisY.begin(), one.axisY.end());
        lines.axisZ.insert(lines.axisZ.end(), one.axisZ.begin(), one.axisZ.end());
    });

    struct Batch
    {
        const std::vector<Vector3f>* verts;
        float r, g, b;
    };
    const Batch batches[] = {
        { &lines.bones, 1.00f, 0.45f, 0.95f },
        { &lines.axisX, 1.00f, 0.25f, 0.20f },
        { &lines.axisY, 0.25f, 1.00f, 0.30f },
        { &lines.axisZ, 0.30f, 0.55f, 1.00f },
    };

    std::vector<Vector3f> verts;
    std::vector<uint32_t> idx;
    uint32_t rangeStart[4]{};
    uint32_t rangeCount[4]{};
    verts.reserve(256);
    idx.reserve(256);
    for (int b = 0; b < 4; ++b)
    {
        rangeStart[b] = static_cast<uint32_t>(idx.size());
        const auto& src = *batches[b].verts;
        for (size_t i = 0; i + 1 < src.size(); i += 2)
        {
            const uint32_t i0 = static_cast<uint32_t>(verts.size());
            verts.push_back(src[i]);
            verts.push_back(src[i + 1]);
            idx.push_back(i0);
            idx.push_back(i0 + 1);
        }
        rangeCount[b] = static_cast<uint32_t>(idx.size()) - rangeStart[b];
    }
    constexpr size_t kMaxVerts = 1024;
    if (verts.empty() || idx.empty() || verts.size() > kMaxVerts)
        return;

    const uint32_t fi = app().renderer().frameIndex() % 2;
    void* vp = nullptr;
    void* ip = nullptr;
    if (FAILED(m_skelLineVb[fi]->Map(0, nullptr, &vp)) || FAILED(m_skelLineIb[fi]->Map(0, nullptr, &ip)))
        return;
    std::memcpy(vp, verts.data(), verts.size() * sizeof(Vector3f));
    std::memcpy(ip, idx.data(), idx.size() * sizeof(uint32_t));
    m_skelLineVb[fi]->Unmap(0, nullptr);
    m_skelLineIb[fi]->Unmap(0, nullptr);
    m_skelLineVbv[fi].SizeInBytes = static_cast<UINT>(verts.size() * sizeof(Vector3f));
    m_skelLineIbv[fi].SizeInBytes = static_cast<UINT>(idx.size() * sizeof(uint32_t));

    m_skelLinePipeline.bind(cmd);
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
    cmd->IASetVertexBuffers(0, 1, &m_skelLineVbv[fi]);
    cmd->IASetIndexBuffer(&m_skelLineIbv[fi]);
    LineFrameConstants lc{};
    copyMatrix(lc.worldViewProj, viewProj);
    lc.color[3] = 1.0f;
    for (int b = 0; b < 4; ++b)
    {
        if (rangeCount[b] == 0)
            continue;
        lc.color[0] = batches[b].r;
        lc.color[1] = batches[b].g;
        lc.color[2] = batches[b].b;
        m_skelLinePipeline.setConstants(cmd, lc);
        cmd->DrawIndexedInstanced(rangeCount[b], 1, rangeStart[b], 0, 0);
    }
}

void SandboxDemo::spawnHybridLocalLights()
{
    if (app().renderer().scenePath() != ScenePath::HybridDeferred)
        return;

    app().m_muzzle = app().world().createEntity();
    app().world().emplace<TagComponent>(app().m_muzzle, "Muzzle");
    app().world().emplace<TransformComponent>(app().m_muzzle, Vector3f{}, Quaternion::IDENTITY, Vector3f{ 1.0f, 1.0f, 1.0f });
    auto& muzzle     = app().world().emplace<LocalLightComponent>(app().m_muzzle);
    muzzle.type      = LocalLightType::Point;
    muzzle.color     = Vector3f{ 1.0f, 0.82f, 0.45f };
    muzzle.intensity = 37699.0f; // 12000*π after Fd/π
    muzzle.range     = 6.0f;
    muzzle.enabled   = false;
    app().m_muzzleTimer = 0.0f;

    // ±1.2 from pack/tree XZ so 0.22 cubes sit outside trunks (r=0.5) and health crosses.
    const Vector3f spots[] = {
        { 6.2f, 0.0f, 5.0f },
        { -8.2f, 0.0f, 7.0f },
        { 8.0f, 0.0f, -9.2f },
        { -4.0f, 0.0f, -5.8f },
        { 13.2f, 0.0f, 8.0f },
        { -11.2f, 0.0f, 10.0f },
        { 14.0f, 0.0f, -7.2f },
        { 5.2f, 0.0f, -18.0f },
    };
    if (!app().m_demoScene)
    {
        app().m_play.runtime().offhand.lightOn = false;
        DE_LOG_INFO("SandboxApp: flashlight off, no demo lanterns in this scene");
        return;
    }
    const float waterY = app().m_water.params().waterLevel;
    int         spawned = 0;
    bool        firstLantern = true;
    for (const Vector3f& s : spots)
    {
        const float gy = app().m_terrain.heightAtWorld(s.x, s.z);
        if (gy < waterY - 0.2f)
            continue;
        const Vector3f pos{ s.x, gy + 1.55f, s.z };

        Entity fixture = app().world().createEntity();
        app().world().emplace<TagComponent>(fixture, "Lantern");
        app().world().emplace<TransformComponent>(fixture, pos, Quaternion::IDENTITY, Vector3f{ 0.22f, 0.22f, 0.22f });
        if (m_lanternModelId != NULL_ASSET)
        {
            ModelComponent mc{};
            mc.modelAssetID = m_lanternModelId;
            mc.castShadow   = true;
            setModelComponent(app().world(), app().pins(), app().assets(), fixture, mc);
        }

        Entity lightE = app().world().createEntity();
        app().world().emplace<TagComponent>(lightE, "LanternLight");
        app().world().emplace<TransformComponent>(lightE, pos, Quaternion::IDENTITY, Vector3f{ 1.0f, 1.0f, 1.0f });
        auto& light        = app().world().emplace<LocalLightComponent>(lightE);
        light.type         = LocalLightType::Point;
        light.color        = Vector3f{ 1.0f, 0.72f, 0.35f };
        light.intensity    = 1257.0f; // 400*π after Fd/π
        light.range        = 6.0f;
        light.emissiveMesh = fixture;
        if (firstLantern)
        {
            light.castShadow = true;
            firstLantern     = false;
        }
        ++spawned;
    }
    DE_LOG_INFO("SandboxApp: muzzle ready, {} demo lanterns", spawned);
}

void SandboxDemo::placeHealthPacks()
{
    std::vector<Entity> old;
    app().world().each<HealthPackComponent>([&](Entity e, HealthPackComponent&) { old.push_back(e); });
    for (Entity e : old)
    {
        onEntityRemoved(app().world(), e, &app().pins());
        app().world().destroyEntity(e);
    }

    const Vector3f spots[] = {
        { 5.0f, 0.0f, 5.0f },
        { -7.0f, 0.0f, 7.0f },
        { 8.0f, 0.0f, -8.0f },
        { -4.0f, 0.0f, -7.0f },
    };
    const float   waterY = app().m_water.params().waterLevel;
    int count = 0;
    for (const Vector3f& s : spots)
    {
        const float gy = app().m_terrain.heightAtWorld(s.x, s.z);
        if (gy < waterY - 0.2f)
            continue;
        const Vector3f pos{ s.x, gy + 0.95f, s.z };
        Entity         e = app().world().createEntity();
        app().world().emplace<TagComponent>(e, "HealthPack");
        TransformComponent xf{};
        xf.position = pos;
        xf.scale    = Vector3f{ 0.9f, 0.9f, 0.9f };
        app().world().emplace<TransformComponent>(e, xf);
        if (m_packModelId != NULL_ASSET)
        {
            ModelComponent mc{};
            mc.modelAssetID = m_packModelId;
            mc.castShadow   = true;
            setModelComponent(app().world(), app().pins(), app().assets(), e, mc);
        }
        HealthPackComponent pack{};
        pack.restPos = pos;
        pack.active  = true;
        app().world().emplace<HealthPackComponent>(e, pack);
        stampProceduralId(app().world(), e, std::format("sandbox/healthpack/{}", count), "healthpack");
        ++count;
    }
    DE_LOG_INFO("SandboxApp: {} health packs", count);
}
