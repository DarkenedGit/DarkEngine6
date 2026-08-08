#pragma once
#include "Core/Application.h"
#include "Geometry/Mesh.h"
#include "Render/MeshPipeline.h"
#include "Render/Camera3D.h"
#include "Render/Material.h"

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

    Dark::Entity m_camera;
    Dark::Entity m_cube;

    Dark::Mesh         m_cubeMesh;
    Dark::MeshPipeline m_meshPipeline;
    Dark::Camera3D     m_viewCamera;

    Dark::AssetRef<Dark::Material> m_cubeMaterial;

    bool  m_spinPaused = false;
    float m_spinSpeed  = 0.8f;
};
