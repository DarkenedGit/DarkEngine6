#pragma once

#include "ECS/Entity.h"
#include "ECS/FrameEntitySet.h"
#include "Math/Matrix4f.h"
#include "Particles/GpuParticleSystem.h"
#include "Particles/LeafRenderer.h"
#include "Particles/ParticleRenderer.h"
#include "Render/AutoExposure.h"
#include "Render/CloudVolumePipeline.h"
#include "Render/DrawList.h"
#include "Render/FoliagePipeline.h"
#include "Render/FoliagePrototypes.h"
#include "Render/GrassPipeline.h"
#include "Render/MeshConstants.h"
#include "Render/SceneRenderer.h"
#include "Render/WalkabilityDebug.h"
#include "Render/SkinnedInstanceBuffer.h"
#include "Terrain/TerrainMaterial.h"

#include <d3d12.h>
#include <unordered_map>
#include <vector>

class SandboxApp;

// Sandbox's 3D frame: scene renderer, instanced draws, and the accepted-entity sets
// for one rendered view. SandboxApp::onRender calls render().
class SandboxFrame
{
public:
    void bind(SandboxApp* app) { m_app = app; }

    void render();

    Dark::SceneRenderer&              scene() { return m_scene; }
    const Dark::SceneRenderer&        scene() const { return m_scene; }
    Dark::DrawList&                   drawList() { return m_drawList; }
    Dark::SkinnedInstanceBuffer&      skinnedInstances() { return m_skinnedInstances; }
    std::unordered_map<Dark::EntityID, Dark::Math::Matrix4f>& prevWorldByEntity() { return m_prevWorldByEntity; }

    Dark::FoliagePrototypes&     foliagePrototypes() { return m_foliagePrototypes; }
    Dark::FoliagePipeline&       foliagePipeline() { return m_foliagePipeline; }
    Dark::GrassPipeline&         grassPipeline() { return m_grassPipeline; }
    Dark::LeafRenderer&          leafRenderer() { return m_leafRenderer; }
    Dark::TerrainMaterial&       terrainMaterial() { return m_terrainMaterial; }
    Dark::ParticleRenderer&      particles() { return m_particles; }
    Dark::GpuParticleSystem&     gpuParticles() { return m_gpuParticles; }
    Dark::CloudLodSettings&      cloudLod() { return m_cloudLod; }
    Dark::AutoExposureSettings&  autoExposure() { return m_autoExposure; }
    Dark::AutoExposureState&     autoExposureState() { return m_autoExposureState; }
    Dark::AutoExposureResult&    autoExposureResult() { return m_autoExposureResult; }

private:
    void drawProjectiles(ID3D12GraphicsCommandList* cmd, const Dark::Math::Matrix4f& viewProj, Dark::MeshFrameConstants& cb, bool instanced, const std::vector<int>* rejectedShots);
    void drawProjectilesGBuffer(ID3D12GraphicsCommandList* cmd, const Dark::Math::Matrix4f& viewProj, const Dark::Math::Matrix4f& prevViewProj, bool instanced, const std::vector<int>* rejectedShots);
    void drawDebugOverlays(ID3D12GraphicsCommandList* cmd);
    void dropDeadPrevWorlds();
    SandboxApp& app() const;

    SandboxApp* m_app = nullptr;
    Dark::FoliagePrototypes      m_foliagePrototypes;
    Dark::FoliagePipeline        m_foliagePipeline;
    Dark::GrassPipeline          m_grassPipeline;
    Dark::LeafRenderer           m_leafRenderer;
    Dark::TerrainMaterial        m_terrainMaterial;
    Dark::ParticleRenderer       m_particles;
    Dark::GpuParticleSystem      m_gpuParticles;
    Dark::CloudLodSettings       m_cloudLod{};
    Dark::AutoExposureSettings   m_autoExposure{};
    Dark::AutoExposureState      m_autoExposureState{};
    Dark::AutoExposureResult     m_autoExposureResult{};
    Dark::DrawList                              m_drawList;
    Dark::SkinnedInstanceBuffer                 m_skinnedInstances;
    Dark::SceneRenderer                         m_scene;
    Dark::WalkabilityDebug                      m_walkDebug;
    std::unordered_map<Dark::EntityID, Dark::Math::Matrix4f> m_prevWorldByEntity;
    Dark::FrameEntitySet                        m_acceptedMesh;
    Dark::FrameEntitySet                        m_acceptedModel;
    Dark::FrameEntitySet                        m_acceptedSkinned;
    std::vector<int>                            m_rejectedShots;
};
