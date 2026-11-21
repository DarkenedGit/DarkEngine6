#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Shader identity shared by the runtime compiler, the runtime cache and the Release bake (plan D10, §3.15.1).
// Pure code (no D3D headers) so the manifest rules are unit-tested natively.

namespace Dark
{

    enum class ShaderStage : uint8_t
    {
        Vertex,
        Pixel,
        Compute,
    };

    // DXC shader model for every stage. cmake/ShaderBake.cmake reads this line, so the bake and the
    // runtime compiler cannot disagree. Plan §3.15 step 3 bumps it to "6_6" after the FXC A/B.
    inline constexpr const char* kShaderModelDxc = "6_0";

    // FXC fallback (DE_SHADER_FXC=1) compiles every stage at 5.1, which also accepts the register-space
    // bindings the ultraviolet permutations use.
    inline constexpr const char* kShaderModelFxc = "5_1";

    const char* shaderStageId(ShaderStage stage); // "vs", "ps", "cs"
    bool        shaderStageFromId(std::string_view id, ShaderStage& out);

    // "ps_6_0" (DXC) or "ps_5_1" (FXC).
    std::string shaderProfile(ShaderStage stage, bool fxc);

    struct ShaderDefine
    {
        std::string name;
        std::string value;
    };

    struct ShaderKey
    {
        std::string file;  // path under content/shaders with '/' separators, e.g. "TerrainGBuffer.hlsl"
        std::string entry; // "PSMain"
        ShaderStage stage = ShaderStage::Vertex;
        std::string defines; // canonical: NAME=VALUE pairs sorted by name, comma-separated, e.g. "DE_THERMAL=1,DE_THERMAL_HEAT=0.41"

        bool operator==(const ShaderKey&) const = default;
    };

    // Sorted by name; an empty value is written as "NAME=" so the key stays unambiguous.
    std::string canonicalShaderDefines(std::span<const ShaderDefine> defines);

    // Splits canonical defines back into pairs. Returns false on a malformed list.
    bool splitShaderDefines(std::string_view defines, std::vector<ShaderDefine>& out);

    // "shaders/TerrainGBuffer.hlsl" -> "TerrainGBuffer.hlsl". Backslashes become '/'. Returns false
    // when the path is not under shaders/.
    bool shaderFileFromContentPath(std::string_view relativeUnderContent, std::string& outFile);

    // The manifest line for this key: "<file> <entry> <stage> [defines]".
    std::string formatShaderKey(const ShaderKey& key);

    // Name of the baked DXIL in bin/<config>/shaders/: "<file stem>.<entry>.<stage>[.<defines>].dxil",
    // with '=' -> '-', ',' -> '_', '/' -> '_' and anything outside [A-Za-z0-9_.-] -> '_'.
    // cmake/ShaderBake.cmake builds the same name.
    std::string bakedShaderFileName(const ShaderKey& key);

    struct ShaderManifest
    {
        std::vector<ShaderKey> keys;

        bool contains(const ShaderKey& key) const;
    };

    // Parses content/shaders/shaders.manifest. Blank lines and '#' comments are skipped.
    // Fails (with "line N: reason") on a bad stage, unsorted or malformed defines, or a duplicate key.
    bool parseShaderManifest(std::string_view text, ShaderManifest& out, std::string& error);

    // True when bytecode is a shader container with a DXIL part (DXC); false for FXC DXBC or garbage.
    bool isDxilContainer(const void* data, size_t size);

    // FNV-1a 64-bit, used for the runtime cache key and file names.
    uint64_t hashBytes64(const void* data, size_t size, uint64_t seed = 0xcbf29ce484222325ull);
    uint64_t hashString64(std::string_view text, uint64_t seed = 0xcbf29ce484222325ull);

} // namespace Dark
