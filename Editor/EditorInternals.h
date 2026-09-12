#pragma once

#include "Editor/EditorApp.h"

// SceneRenderer owns shared pipelines; keep existing call sites stable.
#define m_meshPipeline m_scene.meshPipeline()
#define m_meshTransparentPipeline m_scene.meshTransparentPipeline()
#define m_skinnedPipeline m_scene.skinnedPipeline()
#define m_skinnedTransparentPipeline m_scene.skinnedTransparentPipeline()
#define m_skinnedShadowPipeline m_scene.skinnedShadowPipeline()
#define m_skinRing m_scene.skinRing()
#define m_tonemap m_scene.tonemap()
#define m_lighting m_scene.lighting()
#define m_localLightVolumes m_scene.localLightVolumes()
#define m_localLightGpu m_scene.localLightGpu()
#define m_pointVolumeMesh m_scene.pointVolumeMesh()
#define m_spotVolumeMesh m_scene.spotVolumeMesh()
#define m_bloom m_scene.bloom()
#define m_motionBlur m_scene.motionBlur()
#define m_taa m_scene.taa()
#define m_shadows m_scene.shadows()
#define m_debugOverlay m_scene.debugOverlay()

#include "Editor/EditorObject.h"
#include "ECS/Components.h"
#include "Network/Replication.h"
#include "Scene/SceneTypes.h"
#include "Particles/ParticleEmitter.h"
#include "Math/Matrix4f.h"
#include "Math/Quaternion.h"
#include "Math/Vector3f.h"

#include <cstdint>

namespace Dark::EditorDetail 
{

    extern const float kPalette[][4];
    extern const int kPaletteCount;

    void mountContentRoots(AssetManager& assets);
    void copyMatrix(float dst[16], const Math::Matrix4f& m);
    Math::Matrix4f makeWorldMatrix(const TransformComponent& xf);
    void copyColor(float dst[4], const float src[4]);
    bool createChecker(Renderer& renderer, Texture2D& out,
        uint8_t r0, uint8_t g0, uint8_t b0, uint8_t r1, uint8_t g1, uint8_t b1,
        uint32_t size = 32, uint32_t cell = 8);
    Math::Vector3f defaultScale2D(SceneObjectType type);
    uint32_t packRgba8(const float c[4]);
    void unpackRgba8(uint32_t rgba, float out[4]);
    bool isReplicatedProp(SceneObjectType type);
    bool isLocalLightType(SceneObjectType type);
    void defaultLightColor(float out[4]);
    void fillDefaultLocalLight(LocalLightComponent& light, SceneObjectType type);
    Entity glowMeshOf(World& world, Entity e);
    Entity lightOwningGlowMesh(World& world, Entity mesh);
    bool keepsPlacedHeight(World& world, const EditorObjectComponent* so, Entity e);
    void syncGlowPairPosition(World& world, Entity e);
    void eulerXYZFromQuat(const Math::Quaternion& q, float& pitch, float& yaw, float& roll);
    Math::Matrix4f makePointLightGizmoWorld(const Math::Vector3f& pos, float range);
    Math::Matrix4f makeSpotLightGizmoWorld(const TransformComponent& xf, float range, float outerConeDeg);
    // Local +Z is the cone axis. Identity aims +Z (horizon); placed spots should light the floor.
    Math::Quaternion defaultSpotRotation();
    NetPrefab prefabFromType(SceneObjectType type);
    SceneObjectType typeFromPrefab(NetPrefab prefab);
    const char* netRoleLabel(NetRole role);
    void defaultColor2D(SceneObjectType type, float out[4]);
    void descFromSceneData(const SceneObjectData& d, ParticleEmitterDesc& out);
    void sceneDataFromDesc(const ParticleEmitterDesc& src, SceneObjectData& d);

} // namespace Dark::EditorDetail
