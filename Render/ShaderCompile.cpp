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
#include <memory>
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
            std::mutex             rootsMutex;
            bool                   engineRootTried = false;
            ShaderRootSet          roots;
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

        // Path of `path` below dir ("sub/X.hlsl"), or empty when it is not below dir.
        std::filesystem::path pathBelow(const std::filesystem::path& path, const std::filesystem::path& dir)
        {
            std::error_code             ec;
            const std::filesystem::path rel = std::filesystem::weakly_canonical(path, ec).lexically_relative(dir);
            if (ec || rel.empty() || *rel.begin() == ".." || *rel.begin() == ".")
                return {};
            return rel;
        }

        // .hlsl/.hlsli paths under dir, relative to it with '/' separators.
        std::vector<std::string> listShaderFiles(const std::filesystem::path& dir)
        {
            std::vector<std::string> files;
            std::error_code          ec;
            for (std::filesystem::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
            {
                const std::filesystem::path ext = it->path().extension();
                if (it->is_regular_file(ec) && (ext == ".hlsl" || ext == ".hlsli"))
                    files.push_back(it->path().lexically_relative(dir).generic_string());
            }
            return files;
        }

        // Caller holds rootsMutex.
        bool addShaderRootLocked(ShaderCompileState& s, std::string_view dir)
        {
            if (s.roots.find(dir))
                return true;
            const std::filesystem::path folder = resolveContentPath(std::filesystem::path(std::string(dir)));
            std::error_code             ec;
            std::string                 text;
            if (folder.empty() || !std::filesystem::is_regular_file(folder / "shaders.manifest", ec) || !readTextFile(folder / "shaders.manifest", text))
            {
                DE_LOG_ERROR(LogCategory::Render, "Shader root content/{}/ not added: content/{}/shaders.manifest not found", dir, dir);
                return false;
            }
            std::string error;
            if (!s.roots.add(dir, text, listShaderFiles(folder), error))
            {
                DE_LOG_ERROR(LogCategory::Render, "Shader root content/{}/ not added: {}", dir, error);
                return false;
            }
            return true;
        }

        // The shader roots, content/shaders first. Caller holds rootsMutex.
        const ShaderRootSet& shaderRootsLocked(ShaderCompileState& s)
        {
            if (!s.engineRootTried)
            {
                s.engineRootTried = true;
                addShaderRootLocked(s, kEngineShaderRoot);
            }
            return s.roots;
        }

        bool shaderRootsContain(const ShaderKey& key)
        {
            ShaderCompileState&         s = state();
            std::lock_guard<std::mutex> lock(s.rootsMutex);
            return shaderRootsLocked(s).contains(key);
        }

        // Key file (path under its shader root) for a path under content/, e.g. "shaders/Line.hlsl".
        std::string shaderFileFromContent(const char* relativeUnderContent)
        {
            std::string file;
            {
                ShaderCompileState&         s = state();
                std::lock_guard<std::mutex> lock(s.rootsMutex);
                if (shaderRootsLocked(s).fileFromContentPath(relativeUnderContent, file))
                    return file;
            }
            if (shaderFileFromContentPath(relativeUnderContent, file))
                return file;
            return relativeUnderContent;
        }

        // Key file for an absolute shader path, so compileShaderFromFile has a key in Baked mode.
        std::string shaderFileKey(const std::filesystem::path& path)
        {
            std::vector<std::string> dirs;
            {
                ShaderCompileState&         s = state();
                std::lock_guard<std::mutex> lock(s.rootsMutex);
                for (const ShaderRootSet::Root& root : shaderRootsLocked(s).roots())
                    dirs.push_back(root.dir);
            }
            if (dirs.empty())
                dirs.emplace_back(kEngineShaderRoot);
            std::error_code ec;
            for (const std::filesystem::path& contentRoot : contentRootCandidates())
            {
                if (contentRoot.empty())
                    continue;
                for (const std::string& dir : dirs)
                {
                    const std::filesystem::path shaders = std::filesystem::weakly_canonical(contentRoot / dir, ec);
                    if (ec)
                        continue;
                    const std::filesystem::path rel = pathBelow(path, shaders);
                    if (!rel.empty())
                        return rel.generic_string();
                }
            }
            return path.filename().generic_string();
        }

        // #include "x" search after the including file's folder: content/shaders, for shaders outside it.
        std::vector<std::filesystem::path> shaderIncludeDirs(const std::filesystem::path& path)
        {
            const std::filesystem::path engine = resolveContentPath(std::filesystem::path(std::string(kEngineShaderRoot)));
            if (engine.empty() || !pathBelow(path, engine).empty())
                return {};
            return { engine };
        }

        // FXC include handler with DXC's search order: the including file's folder, then extraDirs.
        class SearchPathInclude final : public ID3DInclude
        {
        public:
            SearchPathInclude(std::filesystem::path fileDir, std::vector<std::filesystem::path> extraDirs)
                : m_fileDir(std::move(fileDir))
                , m_extraDirs(std::move(extraDirs))
            {
            }

            HRESULT STDMETHODCALLTYPE Open(D3D_INCLUDE_TYPE, LPCSTR fileName, LPCVOID parentData, LPCVOID* outData, UINT* outBytes) override
            {
                if (!fileName || !outData || !outBytes)
                    return E_INVALIDARG;
                std::vector<std::filesystem::path> dirs = { parentDir(parentData) };
                dirs.insert(dirs.end(), m_extraDirs.begin(), m_extraDirs.end());
                for (const std::filesystem::path& dir : dirs)
                {
                    const std::filesystem::path candidate = dir / std::filesystem::path(fileName);
                    std::error_code             ec;
                    if (!std::filesystem::is_regular_file(candidate, ec))
                        continue;
                    auto opened = std::make_unique<Opened>();
                    if (!readTextFile(candidate, opened->text))
                        return E_FAIL;
                    opened->dir = candidate.parent_path();
                    *outData    = opened->text.data();
                    *outBytes   = static_cast<UINT>(opened->text.size());
                    m_opened.push_back(std::move(opened));
                    return S_OK;
                }
                return E_FAIL;
            }

            // Buffers live until the compile ends (this object's lifetime).
            HRESULT STDMETHODCALLTYPE Close(LPCVOID) override { return S_OK; }

            size_t extraDirCount() const { return m_extraDirs.size(); }

        private:
            struct Opened
            {
                std::string           text;
                std::filesystem::path dir;
            };

            std::filesystem::path parentDir(LPCVOID parentData) const
            {
                for (const std::unique_ptr<Opened>& opened : m_opened)
                    if (opened->text.data() == parentData)
                        return opened->dir;
                return m_fileDir;
            }

            std::filesystem::path                m_fileDir;
            std::vector<std::filesystem::path>   m_extraDirs;
            std::vector<std::unique_ptr<Opened>> m_opened;
        };

        bool loadBaked(const ShaderKey& key, ComPtr<ID3DBlob>& outBytecode)
        {
            if (!shaderRootsContain(key))
            {
                DE_LOG_ERROR(LogCategory::Render,
                             "Shader '{}' is in no shaders.manifest (content/shaders/ or a root added with addShaderRoot). This build loads baked shaders only; add the line and rebuild (de_shaders).",
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
            std::vector<std::filesystem::path> includeDirs = shaderIncludeDirs(path);
            SearchPathInclude                  searchInclude(path.parent_path(), std::move(includeDirs));
            ID3DInclude* const                 include = searchInclude.extraDirCount() == 0 ? D3D_COMPILE_STANDARD_FILE_INCLUDE : &searchInclude;

            ComPtr<ID3DBlob> errors;
            UINT             flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
            flags |= D3DCOMPILE_DEBUG;
            if (!forceOptimize)
                flags |= D3DCOMPILE_SKIP_OPTIMIZATION;
#else
            (void)forceOptimize;
#endif
            const HRESULT hr = D3DCompile(source.data(), source.size(), sourceName.c_str(), defines, include, entry, profile.c_str(), flags, 0, &outBytecode, &errors);
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
            request.includeDirs = shaderIncludeDirs(path);
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

    bool addShaderRoot(const char* relativeUnderContent)
    {
        if (!relativeUnderContent || relativeUnderContent[0] == '\0')
        {
            DE_LOG_ERROR(LogCategory::Render, "addShaderRoot: empty path");
            return false;
        }
        ShaderCompileState&         s = state();
        std::lock_guard<std::mutex> lock(s.rootsMutex);
        shaderRootsLocked(s);
        return addShaderRootLocked(s, relativeUnderContent);
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

        const ShaderKey key = makeShaderKey(shaderFileFromContent(relativeUnderContent), entry, stage, defines);
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
        if (!shaderRootsContain(key))
        {
            ShaderCompileState&         s = state();
            std::lock_guard<std::mutex> lock(s.keysMutex);
            if (std::find(s.warnedMissing.begin(), s.warnedMissing.end(), key) == s.warnedMissing.end())
            {
                s.warnedMissing.push_back(key);
                DE_LOG_WARN(LogCategory::Render, "Shader '{}' is in no shaders.manifest (content/shaders/ or a root added with addShaderRoot); Release builds will fail to load it",
                            formatShaderKey(key));
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
