#pragma once

#include "Animation/AnimNotify.h"
#include "Assets/AssetHandle.h"
#include "ECS/Entity.h"
#include "Math/Matrix4f.h"
#include "Render/LinePipeline.h"
#include "Render/Mesh.h"

#include <d3d12.h>
#include <wrl/client.h>

class SandboxApp;

// Sandbox demo content: the origin cube, the glTF samples, the wiggle actor,
// skeleton debug lines, demo lanterns, and the health packs placed at boot.
class SandboxDemo
{
public:
    void bind(SandboxApp* app) { m_app = app; }

    bool createSandboxModels(int step);
    void spawnGltfDemo(int step);
    void spawnAnimatedDemo();
    void updateWiggleAnim();
    bool createSkeletonLineBuffers();
    void drawSkeletonOverlay(ID3D12GraphicsCommandList* cmd, const Dark::Math::Matrix4f& viewProj);
    void spawnHybridLocalLights();
    void placeHealthPacks();

    Dark::Entity&                    cube() { return m_cube; }
    Dark::Mesh&                      cubeMesh() { return m_cubeMesh; }
    Dark::AssetID                    cubeModelId() const { return m_cubeModelId; }
    Dark::AssetID                    packModelId() const { return m_packModelId; }
    Dark::AssetID                    tracerModelId() const { return m_tracerModelId; }
    Dark::LinePipeline&              skeletonPipeline() { return m_skelLinePipeline; }

private:
    static void onWiggleNotify(void* user, const Dark::AnimNotify& n);
    SandboxApp& app() const;

    SandboxApp*  m_app = nullptr;
    Dark::Entity m_cube{};
    Dark::Entity m_wiggle{};
    Dark::Mesh   m_cubeMesh{};
    Dark::AssetID m_cubeModelId    = Dark::NULL_ASSET;
    Dark::AssetID m_packModelId    = Dark::NULL_ASSET;
    Dark::AssetID m_lanternModelId = Dark::NULL_ASSET;
    Dark::AssetID m_tracerModelId  = Dark::NULL_ASSET;

    Dark::LinePipeline                     m_skelLinePipeline;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_skelLineVb[2];
    Microsoft::WRL::ComPtr<ID3D12Resource> m_skelLineIb[2];
    D3D12_VERTEX_BUFFER_VIEW               m_skelLineVbv[2]{};
    D3D12_INDEX_BUFFER_VIEW                m_skelLineIbv[2]{};
};
