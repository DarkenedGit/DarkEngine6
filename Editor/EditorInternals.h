#pragma once

#include "Editor/EditorApp.h"
#include "Editor/EditorObject.h"
#include "ECS/Components.h"
#include "Network/Replication.h"
#include "Scene/SceneTypes.h"
#include "Particles/ParticleEmitter.h"
#include "Math/Matrix4f.h"
#include "Math/Quaternion.h"
#include "Math/Vector3f.h"

#include <cstdint>

namespace Dark {
namespace EditorDetail {

extern const float kPalette[][4];
extern const int kPaletteCount;

void mountContentRoots(AssetManager& assets);
void copyMatrix(float dst[16], const Math::Matrix4f& m);
Math::Matrix4f makeWorldMatrix(const TransformComponent& xf);
void copyColor(float dst[4], const float src[4]);
bool createChecker(
    Renderer& renderer,
    Texture2D& out,
    uint8_t r0, uint8_t g0, uint8_t b0,
    uint8_t r1, uint8_t g1, uint8_t b1,
    uint32_t size = 32,
    uint32_t cell = 8);
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
NetPrefab prefabFromType(SceneObjectType type);
SceneObjectType typeFromPrefab(NetPrefab prefab);
const char* netRoleLabel(NetRole role);
void defaultColor2D(SceneObjectType type, float out[4]);
void descFromSceneData(const SceneObjectData& d, ParticleEmitterDesc& out);
void sceneDataFromDesc(const ParticleEmitterDesc& src, SceneObjectData& d);

} // namespace EditorDetail
} // namespace Dark
