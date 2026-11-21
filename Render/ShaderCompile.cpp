#include "Render/ShaderCompile.h"
#include "Render/ShaderCache.h"
#include "Render/ShaderDxc.h"
#include "Core/ContentRoots.h"
#include "Core/Log.h"
#include "Core/Paths.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <d3dcompiler.h>

#include <algorithm>
#include <atomic>
#include <fstream>
#include <mutex>
#include <vector>

namespace Dark
{

    namespace
    {
        wchar_t envFlag(const wchar_t* name)
        {
            wchar_t value[8]{};
            return GetEnvironmentVariableW(name, value, 8) > 0 ? value[0] : L'\0';
        }

        // Minimal ID3DBlob over owned bytes, for cache hits and baked DXIL (no compiler DLL in Release).
        class ByteBlob final : public ID3DBlob
        {
        public:
            explicit ByteBlob(std::vector<uint8_t>&& bytes)
                : m_bytes(std::move(bytes))
            {
            }

            HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override
            {
                if (!object)
                    return E_POINTER;
                if (riid == __uuidof(ID3DBlob) || riid == __uuidof(IUnknown))
                {
                    *object = static_cast<ID3DBlob*>(this);
                    AddRef();
                    return S_OK;
                }
                *object = nullptr;
                return E_NOINTERFACE;
            }

            ULONG STDMETHODCALLTYPE AddRef() override { return m_refs.fetch_add(1) + 1; }

            ULONG STDMETHODCALLTYPE Release() override
            {
                const ULONG left = m_refs.fetch_sub(1) - 1;
                if (left == 0)
                    delete this;
                return left;
            }

            LPVOID STDMETHODCALLTYPE GetBufferPointer() override { return m_bytes.data(); }
            SIZE_T STDMETHODCALLTYPE GetBufferSize() override { return m_bytes.size(); }

        private:
            ~ByteBlob() = default;

            std::vector<uint8_t> m_bytes;
            std::atomic<ULONG>   m_refs{ 1 };
        };

        struct ShaderCompileState
        {
            std::once_flag         backendOnce;
            ShaderBackend          backend = ShaderBackend::Dxc;
            bool                   cacheEnabled = false;
            std::filesystem::path  cacheDir;
            std::once_flag         manifestOnce;
            bool                   manifestOk = false;
            ShaderManifest         manifest;
            std::mutex             keysMutex;
            std::vector<ShaderKey> requested;
            std::vector<ShaderKey> warnedMissing;
        };

        ShaderCompileState& state()
        {
            static ShaderCompileState s;
            return s;
        }

        std::filesystem::path shaderCacheDirectory()
        {
            wchar_t      buf[MAX_PATH]{};
            const DWORD  n = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH);
            if (n == 0 || n >= MAX_PATH)
                return {};
            return std::filesystem::path(buf) / L"DarkEngine6" / L"ShaderCache";
        }

        void chooseBackend(ShaderCompileState& s)
        {
            if (!kHostRuntimeShaderCompile || envFlag(L"DE_SHADER_BAKED") == L'1')
                s.backend = ShaderBackend::Baked;
            else if (envFlag(L"DE_SHADER_FXC") == L'1')
                s.backend = ShaderBackend::Fxc;
            else
                s.backend = ShaderBackend::Dxc;

            if (s.backend == ShaderBackend::Dxc && envFlag(L"DE_SHADER_CACHE") != L'0')
            {
                s.cacheDir     = shaderCacheDirectory();
                s.cacheEnabled = !s.cacheDir.empty();
            }
            DE_LOG_INFO(LogCategory::Render, "Shaders: {} backend{}{}", shaderBackendName(s.backend),
                        s.cacheEnabled ? ", cache " : "", s.cacheEnabled ? s.cacheDir.string() : std::string());
        }

        std::vector<ShaderDefine> toDefines(const D3D_SHADER_MACRO* defines)
        {
            std::vector<ShaderDefine> out;
            for (const D3D_SHADER_MACRO* m = defines; m && m->Name; ++m)
                out.push_back({ m->Name, m->Definition ? m->Definition : "" });
            return out;
        }

        bool readBinaryFile(const std::filesystem::path& path, std::vector<uint8_t>& out)
        {
            out.clear();
            std::ifstream file(path, std::ios::binary | std::ios::ate);
            if (!file)
                return false;
            const std::streamoff size = file.tellg();
            if (size <= 0)
                return false;
            file.seekg(0, std::ios::beg);
            out.resize(static_cast<size_t>(size));
            return static_cast<bool>(file.read(reinterpret_cast<char*>(out.data()), size));
        }

        // Path under content/shaders for an absolute shader path, so compileShaderFromFile has a key in Baked mode.
        std::string shaderFileKey(const std::filesystem::path& path)
        {
            std::error_code ec;
            for (const std::filesystem::path& root : contentRootCandidates())
            {
                if (root.empty())
                    continue;
                const std::filesystem::path shaders = std::filesystem::weakly_canonical(root / "shaders", ec);
                if (ec)
                    continue;
                const std::filesystem::path rel = std::filesystem::weakly_canonical(path, ec).lexically_relative(shaders);
                if (!ec && !rel.empty() && *rel.begin() != "..")
                    return rel.generic_string();
            }
            return path.filename().generic_string();
        }

        bool loadBaked(const ShaderKey& key, ComPtr<ID3DBlob>& outBytecode)
        {
            const ShaderManifest* manifest = loadedShaderManifest();
            if (!manifest || !manifest->contains(key))
            {
                DE_LOG_ERROR(LogCategory::Render,
                             "Shader '{}' is not in content/shaders/shaders.manifest. This build loads baked shaders only; add the line and rebuild (de_shaders).",
                             formatShaderKey(key));
                return false;
            }
            const std::filesystem::path file = executableDirectory() / "shaders" / bakedShaderFileName(key);
            std::vector<uint8_t>        bytes;
            if (!readBinaryFile(file, bytes))
            {
                DE_LOG_ERROR(LogCategory::Render, "Baked shader '{}' is missing: '{}'. Rebuild so de_shaders runs.", formatShaderKey(key), file.string());
                return false;
            }
            return makeShaderBlob(bytes.data(), bytes.size(), outBytecode);
        }

        bool compileFxc(const std::filesystem::path& path, const std::string& source, const char* entry, ShaderStage stage, const D3D_SHADER_MACRO* defines,
                        bool forceOptimize, ComPtr<ID3DBlob>& outBytecode)
        {
            // Full path so D3D_COMPILE_STANDARD_FILE_INCLUDE resolves sibling .hlsli files.
            const std::string sourceName = path.string();
            const std::string profile    = shaderProfile(stage, true);

            ComPtr<ID3DBlob> errors;
            UINT             flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
            flags |= D3DCOMPILE_DEBUG;
            if (!forceOptimize)
                flags |= D3DCOMPILE_SKIP_OPTIMIZATION;
#else
            (void)forceOptimize;
#endif
            const HRESULT hr = D3DCompile(source.data(), source.size(), sourceName.c_str(), defines, D3D_COMPILE_STANDARD_FILE_INCLUDE, entry, profile.c_str(),
                                          flags, 0, &outBytecode, &errors);
            if (FAILED(hr))
            {
                const char* msg = errors ? static_cast<const char*>(errors->GetBufferPointer()) : "unknown";
                DE_LOG_ERROR(LogCategory::Render, "Shader compile failed ({} {} from '{}'): {}", entry, profile, path.string(), msg);
                outBytecode.Reset();
                return false;
            }
            return true;
        }

        bool compileDxc(const std::filesystem::path& path, const std::string& source, const char* entry, ShaderStage stage, const D3D_SHADER_MACRO* defines,
                        bool forceOptimize, ComPtr<ID3DBlob>& outBytecode)
        {
            if (!dxcAvailable())
            {
                DE_LOG_ERROR(LogCategory::Render, "Shader compile failed ({} from '{}'): DXC is not available", entry, path.string());
                return false;
            }
            DxcCompileRequest request;
            request.path    = path;
            request.source  = &source;
            request.entry   = entry;
            request.stage   = stage;
            request.defines = toDefines(defines);
#if defined(_DEBUG)
            request.optimize  = forceOptimize;
            request.debugInfo = true;
#else
            (void)forceOptimize;
            request.optimize  = true;
            request.debugInfo = false;
#endif

            ShaderCompileState& s   = state();
            uint64_t            key = 0;
            if (s.cacheEnabled)
            {
                key = shaderCacheKey(source, path.generic_string(), entry, shaderProfile(stage, false), canonicalShaderDefines(request.defines),
                                     dxcArgumentString(request), dxcVersionString());
                ShaderCacheEntry entryOut;
                if (loadShaderCacheEntry(s.cacheDir, key, entryOut))
                    return makeShaderBlob(entryOut.bytecode.data(), entryOut.bytecode.size(), outBytecode);
            }

            std::vector<ShaderCacheDependency> deps;
            if (!dxcCompile(request, outBytecode, deps))
                return false;

            if (s.cacheEnabled)
            {
                ShaderCacheEntry entryIn;
                entryIn.key          = key;
                entryIn.dependencies = std::move(deps);
                const auto* bytes    = static_cast<const uint8_t*>(outBytecode->GetBufferPointer());
                entryIn.bytecode.assign(bytes, bytes + outBytecode->GetBufferSize());
                if (!storeShaderCacheEntry(s.cacheDir, entryIn))
                    DE_LOG_WARN(LogCategory::Render, "Shader cache: could not write '{}'", (s.cacheDir / shaderCacheFileName(key)).string());
            }
            return true;
        }
    } // namespace

    ShaderBackend activeShaderBackend()
    {
        ShaderCompileState& s = state();
        std::call_once(s.backendOnce, [&s]() { chooseBackend(s); });
        return s.backend;
    }

    const char* shaderBackendName(ShaderBackend backend)
    {
        switch (backend)
        {
        case ShaderBackend::Dxc: return "DXC";
        case ShaderBackend::Fxc: return "FXC (DE_SHADER_FXC=1)";
        case ShaderBackend::Baked: return "baked DXIL";
        }
        return "DXC";
    }

    const ShaderManifest* loadedShaderManifest()
    {
        ShaderCompileState& s = state();
        std::call_once(s.manifestOnce, [&s]() {
            const std::filesystem::path path = resolveContentPath("shaders/shaders.manifest");
            std::string                 text;
            if (path.empty() || !readTextFile(path, text))
            {
                DE_LOG_ERROR(LogCategory::Render, "Shader manifest content/shaders/shaders.manifest not found");
                return;
            }
            std::string error;
            if (!parseShaderManifest(text, s.manifest, error))
            {
                DE_LOG_ERROR(LogCategory::Render, "Shader manifest '{}': {}", path.string(), error);
                s.manifest.keys.clear();
                return;
            }
            s.manifestOk = true;
        });
        return s.manifestOk ? &s.manifest : nullptr;
    }

    ShaderKey makeShaderKey(const std::string& file, const char* entry, ShaderStage stage, const D3D_SHADER_MACRO* defines)
    {
        ShaderKey key;
        key.file    = file;
        key.entry   = entry ? entry : "";
        key.stage   = stage;
        key.defines = canonicalShaderDefines(toDefines(defines));
        return key;
    }

    std::vector<ShaderKey> requestedShaderKeys()
    {
        ShaderCompileState&         s = state();
        std::lock_guard<std::mutex> lock(s.keysMutex);
        return s.requested;
    }

    bool makeShaderBlob(const void* data, size_t size, ComPtr<ID3DBlob>& out)
    {
        out.Reset();
        if (!data || size == 0)
            return false;
        const auto*          p = static_cast<const uint8_t*>(data);
        std::vector<uint8_t> bytes(p, p + size);
        out.Attach(new ByteBlob(std::move(bytes)));
        return true;
    }

    bool reflectShader(ID3DBlob* blob, ComPtr<ID3D12ShaderReflection>& out)
    {
        out.Reset();
        if (!blob)
            return false;
        if (isDxilContainer(blob->GetBufferPointer(), blob->GetBufferSize()))
            return dxcReflect(blob->GetBufferPointer(), blob->GetBufferSize(), out);
        return SUCCEEDED(D3DReflect(blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&out)));
    }

    std::filesystem::path resolveContentPath(const std::filesystem::path& relativeUnderContent)
    {
        namespace fs = std::filesystem;

        if (relativeUnderContent.empty())
            return {};

        // Absolute paths: use as-is if present.
        if (relativeUnderContent.is_absolute())
        {
            std::error_code ec;
            if (fs::exists(relativeUnderContent, ec) && !ec)
                return relativeUnderContent;
            return {};
        }

        for (const fs::path& root : contentRootCandidates())
        {
            if (root.empty())
                continue;
            const fs::path  candidate = root / relativeUnderContent;
            std::error_code ec;
            if (fs::exists(candidate, ec) && !ec)
            {
                const fs::path canonical = fs::weakly_canonical(candidate, ec);
                return ec ? candidate : canonical;
            }
        }

        return {};
    }

    bool readTextFile(const std::filesystem::path& path, std::string& outText)
    {
        outText.clear();
        if (path.empty())
            return false;

        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file)
        {
            DE_LOG_ERROR(LogCategory::Render, "readTextFile: cannot open '{}'", path.string());
            return false;
        }

        const std::streamoff size = file.tellg();
        if (size < 0)
        {
            DE_LOG_ERROR(LogCategory::Render, "readTextFile: tellg failed for '{}'", path.string());
            return false;
        }

        file.seekg(0, std::ios::beg);
        std::vector<char> buf(static_cast<size_t>(size));
        if (size > 0 && !file.read(buf.data(), size))
        {
            DE_LOG_ERROR(LogCategory::Render, "readTextFile: read failed for '{}'", path.string());
            return false;
        }

        outText.assign(buf.begin(), buf.end());
        return true;
    }

    bool compileShaderFromFile(const std::filesystem::path& path, const char* entry, ShaderStage stage, ComPtr<ID3DBlob>& outBytecode,
                               const D3D_SHADER_MACRO* defines, bool forceOptimize)
    {
        outBytecode.Reset();

        if (!entry)
        {
            DE_LOG_ERROR(LogCategory::Render, "compileShaderFromFile: null entry");
            return false;
        }

        const ShaderBackend backend = activeShaderBackend();
        if (backend == ShaderBackend::Baked)
            return loadBaked(makeShaderKey(shaderFileKey(path), entry, stage, defines), outBytecode);

        std::string source;
        if (!readTextFile(path, source))
            return false;

        if (backend == ShaderBackend::Fxc)
            return compileFxc(path, source, entry, stage, defines, forceOptimize, outBytecode);
        return compileDxc(path, source, entry, stage, defines, forceOptimize, outBytecode);
    }

    bool compileShaderFromContent(const char* relativeUnderContent, const char* entry, ShaderStage stage, ComPtr<ID3DBlob>& outBytecode,
                                  const D3D_SHADER_MACRO* defines, bool forceOptimize)
    {
        outBytecode.Reset();
        if (!relativeUnderContent || relativeUnderContent[0] == '\0')
        {
            DE_LOG_ERROR(LogCategory::Render, "compileShaderFromContent: empty path");
            return false;
        }

        std::string file;
        if (!shaderFileFromContentPath(relativeUnderContent, file))
            file = relativeUnderContent;
        const ShaderKey key = makeShaderKey(file, entry, stage, defines);
        {
            ShaderCompileState&         s = state();
            std::lock_guard<std::mutex> lock(s.keysMutex);
            if (std::find(s.requested.begin(), s.requested.end(), key) == s.requested.end())
                s.requested.push_back(key);
        }

        const ShaderBackend backend = activeShaderBackend();
        if (backend == ShaderBackend::Baked)
            return loadBaked(key, outBytecode);

#if defined(_DEBUG)
        // Drift check (plan §3.15.1): a key missing from the manifest only fails in Release, so say so now.
        if (const ShaderManifest* manifest = loadedShaderManifest(); manifest && !manifest->contains(key))
        {
            ShaderCompileState&         s = state();
            std::lock_guard<std::mutex> lock(s.keysMutex);
            if (std::find(s.warnedMissing.begin(), s.warnedMissing.end(), key) == s.warnedMissing.end())
            {
                s.warnedMissing.push_back(key);
                DE_LOG_WARN(LogCategory::Render, "Shader '{}' is not in content/shaders/shaders.manifest; Release builds will fail to load it", formatShaderKey(key));
            }
        }
#endif

        const std::filesystem::path resolved = resolveContentPath(relativeUnderContent);
        if (resolved.empty())
        {
            DE_LOG_ERROR(LogCategory::Render, "compileShaderFromContent: could not find '{}' under content roots "
                         "(exe/content, cwd/content, ...)",
                         relativeUnderContent);
            return false;
        }

        DE_LOG_INFO(LogCategory::Render, "compileShaderFromContent: loading '{}'", resolved.string());
        return compileShaderFromFile(resolved, entry, stage, outBytecode, defines, forceOptimize);
    }

} // namespace Dark
