#pragma once

#include "Core/Application.h"
#include "Geometry/Mesh.h"
#include "Geometry/LineMesh.h"
#include "Render/MeshPipeline.h"
#include "Render/LinePipeline.h"
#include "Render/Camera3D.h"
#include "Render/Material.h"
#include "Editor/SceneTypes.h"
#include "Editor/EditorImGui.h"
#include "Editor/ParticleEditorPanel.h"
#include "Particles/ParticleEmitter.h"
#include "Particles/ParticleRenderer.h"

#include <filesystem>
#include <memory>
#include <vector>

class EditorApp : public Dark::Application
{
public:
    using Application::Application;

    void onInit() override;
    void onUpdate(float dt) override;
    void onRender() override;
    void onShutdown() override;

private:
    void registerActions();
    void updateCamera(float dt);
    void handleEditorCommands(float dt);
    void drawEditorUi();

    bool groundHitFromMouse(Dark::Math::Vector3f& outPoint);
    bool groundHitFromRay(const Dark::Math::Ray3f& ray, Dark::Math::Vector3f& outPoint) const;
    Dark::Entity pickObject(const Dark::Math::Ray3f& ray);

    Dark::Entity spawnObject(Dark::SceneObjectType type,
                             const Dark::Math::Vector3f& pos,
                             const Dark::Math::Vector3f& scale,
                             const Dark::Math::Quaternion& rot,
                             const float color[4],
                             const Dark::ParticleEmitterDesc* particleDesc = nullptr);

    Dark::Entity placeAtCursor(Dark::SceneObjectType type);
    void         deleteSelected();
    void         selectNext(int delta);
    void         cyclePlaceType(int delta);
    void         cycleSelectedColor();

    void clearScene();
    bool saveScene();
    bool loadScene();

    Dark::ParticleEmitterDesc makeDefaultParticleDesc() const;
    void applyParticleDescToEmitter(int emitterIndex, const Dark::ParticleEmitterDesc& desc);
    void fillParticleDescFromEmitter(int emitterIndex, Dark::ParticleEmitterDesc& out) const;
    void syncSelectedEmitterFromUi();

    const Dark::Geometry::Mesh* meshForType(Dark::SceneObjectType type) const;
    Dark::SceneObject* findObject(Dark::Entity e);
    const Dark::SceneObject* findObject(Dark::Entity e) const;
    Dark::ParticleEmitter* selectedEmitter();

    static float snap(float v, float grid);

    Dark::MeshPipeline m_meshPipeline;
    Dark::LinePipeline m_linePipeline;

    Dark::Geometry::Mesh m_cubeMesh;
    Dark::Geometry::Mesh m_sphereMesh;
    Dark::Geometry::Mesh m_groundMesh;
    Dark::Geometry::LineMesh m_gridMesh;

    Dark::AssetRef<Dark::Material> m_propMaterial;
    Dark::AssetRef<Dark::Material> m_groundMaterial;

    Dark::Camera3D m_camera;

    std::vector<Dark::SceneObject>                   m_objects;
    std::vector<std::unique_ptr<Dark::ParticleEmitter>> m_emitters;
    Dark::ParticleRenderer                           m_particleRenderer;
    Dark::Entity                                     m_selected{};

    EditorImGui          m_imgui;
    ParticleEditorPanel  m_particlePanel;
    bool                 m_showParticlePanel = true;

    Dark::SceneObjectType m_placeType  = Dark::SceneObjectType::Cube;
    int                   m_colorIndex = 0;

    std::filesystem::path m_scenePath;
    std::string           m_sceneName = "level";

    bool  m_showGrid  = true;
    bool  m_showSolid = true;
    float m_gridSnap  = 1.0f;

    float m_moveSpeed = 8.0f;
    float m_lookSpeed = 0.005f;

    bool m_dragging = false;
    int  m_lmbDownX = 0;
    int  m_lmbDownY = 0;
};
