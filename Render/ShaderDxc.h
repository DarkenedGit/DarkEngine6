#pragma once

#include "Render/ShaderCache.h"
#include "Render/ShaderKey.h"

#include <d3d12shader.h>
#include <d3dcommon.h>
#include <wrl/client.h>

#include <filesystem>
#include <string>
#include <vector>

// Runtime DXC (dxcompiler.dll + dxil.dll from <exe dir>/dxc/), loaded on first use with LoadLibrary
// so an exe that never compiles at runtime (Release game exes, plan D10) has no DXC dependency.

namespace Dark
{

    struct DxcCompileRequest
    {
        std::filesystem::path     path; // full path; PIX and diagnostics show it
        const std::string*        source = nullptr;
        const char*               entry  = nullptr;
        ShaderStage               stage  = ShaderStage::Vertex;
        std::vector<ShaderDefine> defines;
        bool                      optimize  = true;  // false: -Od
        bool                      debugInfo = false; // true: -Zi -Qembed_debug (PIX/Nsight show HLSL source)
    };

    // Loads DXC once. False (logged once) when <exe dir>/dxc/dxcompiler.dll is missing or broken.
    bool dxcAvailable();

    // "1.9.2609.5 (commit)" or empty when DXC is not loaded.
    const std::string& dxcVersionString();

    // Canonical argument string for the request (cache key input).
    std::string dxcArgumentString(const DxcCompileRequest& request);

    // Compiles with IDxcCompiler3 at shaderProfile(stage, false) and -HV 2021.
    // outDependencies lists every file the include handler opened (path + content hash).
    bool dxcCompile(const DxcCompileRequest& request, Microsoft::WRL::ComPtr<ID3DBlob>& outBytecode, std::vector<ShaderCacheDependency>& outDependencies);

    // ID3D12ShaderReflection for a DXIL container (IDxcUtils::CreateReflection).
    bool dxcReflect(const void* bytecode, size_t size, Microsoft::WRL::ComPtr<ID3D12ShaderReflection>& out);

} // namespace Dark
