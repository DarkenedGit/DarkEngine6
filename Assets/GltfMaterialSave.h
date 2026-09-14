#pragma once

#include <filesystem>
#include <string>

namespace Dark
{

    class Model;

    // Patch glTF JSON materials[] from Model parts (by materialIndex). Does not rewrite meshes.
    bool patchGltfMaterialsJson(const char* jsonText, const Model& model, std::string& outJson, std::string* errorOut = nullptr);

    // .gltf: rewrite JSON in place. .glb: rewrite JSON chunk, keep BIN chunk.
    bool saveGltfMaterials(const std::filesystem::path& path, const Model& model, std::string* errorOut = nullptr);

} // namespace Dark
