#pragma once
#include "Core/Application.h"
#include "Geometry/Mesh.h"
#include "Render/MeshPipeline.h"
#include "Render/TerrainPipeline.h"
#include "Render/WaterPipeline.h"
#include "Render/SkyPipeline.h"
#include "Render/Camera3D.h"
#include "Sky/Environment.h"
#include "Render/Material.h"
#include "Terrain/Terrain.h"
#include "Terrain/TerrainMaterial.h"
#include "Water/Water.h"

class SandboxApp : public Dark::Application
{
public:
    using Application::Application;

    void onInit() override;
    void onUpdate(float dt) override;
    void onRender() override;
    void onShutdown() override;

private:
    void registerDefaultActions();
    void handleRuntimeCommands(float dt);
    void updateFlyCamera(float dt);
    void syncTerrainLod();

    Dark::Entity m_camera;
    Dark::Entity m_cube;

    Dark::Geometry::Mesh    m_cubeMesh;
    Dark::MeshPipeline      m_meshPipeline;
    Dark::TerrainPipeline   m_terrainPipeline;
    Dark::WaterPipeline     m_waterPipeline;
    Dark::SkyPipeline       m_skyPipeline;
    Dark::Camera3D          m_viewCamera;
    Dark::Sky::Environment  m_env;

    Dark::AssetRef<Dark::Material> m_cubeMaterial;

    Dark::Terrain::TerrainWorld m_terrain;
    Dark::TerrainMaterial       m_terrainMaterial;
    Dark::Water::WaterWorld     m_water;

    bool  m_spinPaused = false;
    float m_spinSpeed  = 0.8f;
};
