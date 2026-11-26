#pragma once

#include "Render/ShaderKey.h"

#include <d3d12.h>
#include <d3d12shader.h>
#include <d3dcommon.h>
#include <wrl/client.h>

#include <filesystem>
#include <string>
#include <vector>

namespace Dark
{

    using Microsoft::WRL::ComPtr;

    // Every exe defines this through Core/HostShaderPolicy.cpp, added by de_host_exe in CMake (plan D10):
    // true compiles HLSL at runtime with DXC (Debug, Editor, UnitTests); false loads baked DXIL from
    // <exe dir>/shaders/ and never compiles (Release game exes).
    extern const bool kHostRuntimeShaderCompile;

    // Where compileShaderFrom* gets bytecode in this process. Chosen once, on first use:
    //   Baked when the host policy says so, or DE_SHADER_BAKED=1 (to test the Release path in Debug);
    //   Fxc   when DE_SHADER_FXC=1 (fallback for one release, plan §3.15 step 1);
    //   Dxc   otherwise, with the on-disk cache in %LOCALAPPDATA%/DarkEngine6/ShaderCache/ (DE_SHADER_CACHE=0 turns it off).
    enum class ShaderBackend : uint8_t
    {
        Dxc,
        Fxc,
        Baked,
    };

    ShaderBackend activeShaderBackend();
    const char*   shaderBackendName(ShaderBackend backend);

    // Resolve a path relative to a content root (e.g. "shaders/BasicMesh.hlsl").
    // Searches contentRootCandidates(). Absolute paths are used as-is if present.
    // Returns empty path if not found.
    std::filesystem::path resolveContentPath(const std::filesystem::path& relativeUnderContent);

    // Read entire file into outText. Returns false on I/O failure (no exceptions).
    bool readTextFile(const std::filesystem::path& path, std::string& outText);

    // Compile (or, in Baked mode, load) entry for stage from an HLSL file. The profile comes from the
    // stage and the backend (kShaderModelDxc, or 5_1 for FXC). forceOptimize keeps optimization on in
    // Debug for heavy load-time shaders.
    bool compileShaderFromFile(const std::filesystem::path& path, const char* entry, ShaderStage stage, ComPtr<ID3DBlob>& outBytecode,
                               const D3D_SHADER_MACRO* defines = nullptr, bool forceOptimize = false);

    // Adds a shader root: a folder under content/ (e.g. "<name>/shaders") with its own shaders.manifest,
    // used beside content/shaders. Its shaders compile as compileShaderFromContent("<root>/<file>"), and
    // their #include "x" searches the including file's folder, then content/shaders. A shader file or
    // baked name may be in only one root: a clash, or a missing or bad manifest, fails (logged) and the
    // root is not added. Adding a root again returns true. Call before compiling the root's shaders.
    bool addShaderRoot(const char* relativeUnderContent);

    // Resolve relativeUnderContent (e.g. "shaders/Line.hlsl"), then compile. Records the ShaderKey for the
    // manifest drift check, and in Debug warns when the key is in no shader root's shaders.manifest.
    bool compileShaderFromContent(const char* relativeUnderContent, const char* entry, ShaderStage stage, ComPtr<ID3DBlob>& outBytecode,
                                  const D3D_SHADER_MACRO* defines = nullptr, bool forceOptimize = false);

    // ShaderKey for a compile call (defines sorted). file is the path under its shader root.
    ShaderKey makeShaderKey(const std::string& file, const char* entry, ShaderStage stage, const D3D_SHADER_MACRO* defines);

    // Keys requested through compileShaderFromContent in this process, in first-use order.
    std::vector<ShaderKey> requestedShaderKeys();

    // Loads and parses content/shaders/shaders.manifest (the engine root's) once. Null (logged) when
    // missing or invalid.
    const ShaderManifest* loadedShaderManifest();

    // ID3D12ShaderReflection for DXIL (through DXC) and DXBC (D3DReflect) blobs.
    bool reflectShader(ID3DBlob* blob, ComPtr<ID3D12ShaderReflection>& out);

    // Wraps bytes in an ID3DBlob (no compiler DLL needed).
    bool makeShaderBlob(const void* data, size_t size, ComPtr<ID3DBlob>& out);

    // outMacros must have room for 3 entries. classHeat is a literal such as "0.72" and must outlive the compile.
    inline void makeThermalMacros(const char* classHeat, D3D_SHADER_MACRO outMacros[3])
    {
        outMacros[0].Name       = "DE_THERMAL";
        outMacros[0].Definition = "1";
        outMacros[1].Name       = "DE_THERMAL_HEAT";
        outMacros[1].Definition = classHeat;
        outMacros[2].Name       = nullptr;
        outMacros[2].Definition = nullptr;
    }

    // outMacros must have room for 3 entries. classReflect is a literal such as "0.30" and must outlive the compile.
    // gUv lives in register space 1: fine for DXC (SM 6.x) and for the 5_1 FXC fallback.
    inline void makeUvMacros(const char* classReflect, D3D_SHADER_MACRO outMacros[3])
    {
        outMacros[0].Name       = "DE_ULTRAVIOLET";
        outMacros[0].Definition = "1";
        outMacros[1].Name       = "DE_UV_CLASS";
        outMacros[1].Definition = classReflect;
        outMacros[2].Name       = nullptr;
        outMacros[2].Definition = nullptr;
    }

} // namespace Dark
