#pragma once

#include <d3d12.h>
#include <d3dcommon.h>
#include <wrl/client.h>

#include <filesystem>
#include <string>

namespace Dark
{

    using Microsoft::WRL::ComPtr;

    // Resolve a path relative to a content root (e.g. "shaders/BasicMesh.hlsl").
    // Searches contentRootCandidates(). Absolute paths are used as-is if present.
    // Returns empty path if not found.
    std::filesystem::path resolveContentPath(const std::filesystem::path& relativeUnderContent);

    // Read entire file into outText. Returns false on I/O failure (no exceptions).
    bool readTextFile(const std::filesystem::path& path, std::string& outText);

    // Load HLSL from disk and compile entry/target (e.g. "VSMain", "vs_5_0").
    // sourceName is used in compiler diagnostics (typically the filename).
    bool compileShaderFromFile(const std::filesystem::path& path, const char* entry, const char* target, ComPtr<ID3DBlob>& outBytecode,
                               const D3D_SHADER_MACRO* defines = nullptr, bool forceOptimize = false);

    // Resolve relativeUnderContent (e.g. "shaders/Line.hlsl"), then compile.
    bool compileShaderFromContent(const char* relativeUnderContent, const char* entry, const char* target, ComPtr<ID3DBlob>& outBytecode,
                                  const D3D_SHADER_MACRO* defines = nullptr, bool forceOptimize = false);

    // Scene-referred forward writing display-referred UNORM. HDR and _SRGB RTVs stay 0.
    inline bool encodeSrgbForColorFormat(DXGI_FORMAT colorFormat)
    {
        return colorFormat == DXGI_FORMAT_R8G8B8A8_UNORM;
    }

    // outMacros must have room for 2 entries (define + terminator).
    inline void makeEncodeSrgbMacros(bool encode, D3D_SHADER_MACRO outMacros[2])
    {
        outMacros[0].Name       = "ENCODE_SRGB";
        outMacros[0].Definition = encode ? "1" : "0";
        outMacros[1].Name       = nullptr;
        outMacros[1].Definition = nullptr;
    }

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

} // namespace Dark
