#pragma once

#include "Assets/AssetManager.h"
#include "Core/AssetPinTable.h"
#include "Audio/AudioSystem.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Math/AABox3f.h"
#include "Math/Matrix4f.h"
#include "Render/Mesh.h"
#include "Render/MeshConstants.h"
#include "Render/MeshGen.h"
#include "Render/RenderItem.h"
#include "Render/Renderer.h"
#include "Terrain/TerrainMaterial.h"

#include "ECS/FrameEntitySet.h"

#include <cstdint>

class SandboxApp;

struct SandboxShadowUser
{
    SandboxApp*                  app          = nullptr;
    const Dark::FrameEntitySet*  acceptedMesh = nullptr;
};

// Defined in SandboxApp.cpp. SandboxBoot and SandboxDemo call them.
bool tryCreateTerrainFromContent(Dark::Renderer& renderer, Dark::AssetManager& assets, const Dark::Terrain::SplatMap& splat, Dark::TerrainMaterial& material);
Dark::MeshData makeGrassPatch(float sizeX, float sizeZ, uint32_t seed);
void attachCameraSounds(Dark::World& world, Dark::AssetPinTable& pins, Dark::AssetManager& assets, Dark::Audio::AudioSystem& audio, Dark::Entity camera);
void attachSandboxCubeModel(Dark::World& world, Dark::AssetPinTable& pins, Dark::AssetManager& assets, Dark::Entity e, Dark::AssetID modelId);
void copyMatrix(float dst[16], const Dark::Math::Matrix4f& m);
Dark::Math::Matrix4f makeWorldMatrix(const Dark::TransformComponent& xf);
bool useAcesTonemap(const Dark::Renderer& renderer);
void fillForwardLighting(Dark::MeshForwardBatchConstants& dst, const Dark::MeshFrameConstants& src);
void fillMeshGBufferXforms(Dark::MeshGBufferConstants& cb, const Dark::Math::Matrix4f& world, const Dark::Math::Matrix4f& viewProj, const Dark::Math::Matrix4f& prevViewProj, const Dark::Math::Matrix4f& prevWorld);
void unpackRgba8(uint32_t rgba, float out[4]);
const Dark::Mesh* sandboxPrimitiveMesh(Dark::PrimitiveMesh primitive, const Dark::Mesh& cube, const Dark::Mesh& cross);
Dark::Math::AABox3f sandboxPrimitiveBounds(Dark::PrimitiveMesh primitive);
