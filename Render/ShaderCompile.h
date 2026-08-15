#pragma once

#include <d3d12.h>
#include <wrl/client.h>

#include <filesystem>
#include <string>

namespace Dark
{

    //using Microsoft::WRL::ComPtr;

    //// Resolve a path relative to a content root (e.g. "shaders/BasicMesh.hlsl").
    //// Searches: <exe>/content, <cwd>/content, and a few VS-output parent walks.
    //// Returns empty path if not found.
    //std::filesystem::path resolveContentPath(const std::filesystem::path& relativeUnderContent);

    //// Read entire file into outText. Returns false on I/O failure (no exceptions).
    //bool readTextFile(const std::filesystem::path& path, std::string& outText);

    //// Load HLSL from disk and compile entry/target (e.g. "VSMain", "vs_5_0").
    //// sourceName is used in compiler diagnostics (typically the filename).
    //bool compileShaderFromFile(const std::filesystem::path& path, const char* entry, const char* target, ComPtr<ID3DBlob>& outBytecode);

    //// Resolve relativeUnderContent (e.g. "shaders/Line.hlsl"), then compile.
    //bool compileShaderFromContent(const char* relativeUnderContent, const char* entry, const char* target, ComPtr<ID3DBlob>& outBytecode);

} // namespace Dark
