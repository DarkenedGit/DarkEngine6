#pragma once

#include "Assets/Image.h"
#include "Terrain/HeightMap.h"
#include "Terrain/SplatMap.h"
#include "Terrain/TerrainMaterial.h"

#include <filesystem>
#include <string>

namespace Dark
{

class AssetManager;
class Renderer;

namespace Terrain
{

// heightBlendK below zero selects the baked PBR shader path (one albedo + ORM over the mesh UV).
constexpr float kWorldEngineMacroBlend = -1.0f;

// World Engine / World Creator conventional export: height, splat, and baked PBR maps in one folder.
// Optional surfaces.json remaps splat RGBA onto dirt, grass, rock, snow (content/terrain/ground.json).
struct WorldEngineLoadDesc
{
    float worldSizeMeters   = 1024.0f; // XZ extent; cell size is extent / (samples - 1)
    float heightRangeMeters = 480.0f;  // valley (file min) to peak (file max)
    float baseHeightMeters  = 0.0f;
};

struct WorldEngineMaps
{
    HeightMap                 height;
    SplatMap                  splat; // canonical channels: R dirt, G grass, B rock, A snow
    std::filesystem::path     diffuse;
    std::filesystem::path     roughness;
    std::filesystem::path     mask;
    std::filesystem::path     displacement;
    std::string               sourceKey; // content-relative when the folder sits under a content root
};

// Directory may be absolute or content-relative ("terrain/HurricaneRidge"). Empty if it is not a directory.
std::filesystem::path resolveWorldEngineDirectory(const std::string& source);

// Content-relative key when `directory` is under a content root, otherwise the generic absolute path.
std::string worldEngineSourceKey(const std::filesystem::path& directory);

// Height is required. Splat is required and remapped. Diffuse is required for the baked look.
// Samples are normalized so the file's own min is 0 and its max is 1. heightScale is heightRangeMeters.
bool loadWorldEngineDirectory(const std::filesystem::path& directory, const WorldEngineLoadDesc& desc, WorldEngineMaps& out);

// Linear ORM: R = AO, G = roughness, B = metal, A = 0.5. A flat white mask is ignored (metal 0, AO 1).
bool buildWorldEngineOrmImage(const std::filesystem::path& roughnessPath, const std::filesystem::path& maskPath, Image& out);

// Uploads the baked albedo + ORM and a splat texture. Shader uses kWorldEngineMacroBlend.
// `splat` is the gameplay map (also bound; the macro shader does not blend with it).
bool uploadWorldEngineMaterial(
    Renderer& renderer,
    AssetManager& assets,
    const SplatMap& splat,
    const WorldEngineMaps& maps,
    TerrainMaterial& material,
    TerrainSurfaceDesc* outSurface);

} // namespace Terrain
} // namespace Dark
