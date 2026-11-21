#include "Render/ShaderDxc.h"
#include "Core/Log.h"
#include "Core/Paths.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <dxcapi.h>

#include <format>
#include <mutex>

namespace Dark
{

    using Microsoft::WRL::ComPtr;

    namespace
    {
        struct DxcRuntime
        {
            std::once_flag        once;
            std::mutex            compileMutex;
            bool                  ok       = false;
            HMODULE               dxil     = nullptr;
            HMODULE               compiler = nullptr;
            ComPtr<IDxcUtils>     utils;
            ComPtr<IDxcCompiler3> compiler3;
            std::string           version;
        };

        DxcRuntime& runtime()
        {
            static DxcRuntime rt;
            return rt;
        }

        std::wstring widen(std::string_view s)
        {
            if (s.empty())
                return {};
            const int    n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
            std::wstring out(static_cast<size_t>(n > 0 ? n : 0), L'\0');
            if (n > 0)
                MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
            return out;
        }

        std::string narrow(const wchar_t* s)
        {
            if (!s || !*s)
                return {};
            const int n = WideCharToMultiByte(CP_UTF8, 0, s, -1, nullptr, 0, nullptr, nullptr);
            if (n <= 1)
                return {};
            std::string out(static_cast<size_t>(n - 1), '\0');
            WideCharToMultiByte(CP_UTF8, 0, s, -1, out.data(), n, nullptr, nullptr);
            return out;
        }

        void loadRuntime(DxcRuntime& rt)
        {
            const std::filesystem::path dir = executableDirectory() / "dxc";
            // dxil.dll first: dxcompiler.dll finds the loaded module by name and signs with it.
            rt.dxil     = LoadLibraryExW((dir / L"dxil.dll").c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
            rt.compiler = LoadLibraryExW((dir / L"dxcompiler.dll").c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
            if (!rt.compiler || !rt.dxil)
            {
                DE_LOG_ERROR(LogCategory::Render, "DXC: cannot load dxcompiler.dll/dxil.dll from '{}' (error {}). Rebuild so the de_host_exe step copies them.", dir.string(),
                             static_cast<unsigned>(GetLastError()));
                return;
            }
            const auto create = reinterpret_cast<DxcCreateInstanceProc>(reinterpret_cast<void*>(GetProcAddress(rt.compiler, "DxcCreateInstance")));
            if (!create || FAILED(create(CLSID_DxcUtils, IID_PPV_ARGS(&rt.utils))) || FAILED(create(CLSID_DxcCompiler, IID_PPV_ARGS(&rt.compiler3))))
            {
                DE_LOG_ERROR(LogCategory::Render, "DXC: DxcCreateInstance failed in '{}'", dir.string());
                rt.utils.Reset();
                rt.compiler3.Reset();
                return;
            }

            UINT32                  major = 0, minor = 0;
            ComPtr<IDxcVersionInfo> info;
            if (SUCCEEDED(rt.compiler3.As(&info)))
                info->GetVersion(&major, &minor);
            rt.version = std::format("{}.{}", major, minor);
            ComPtr<IDxcVersionInfo2> info2;
            if (SUCCEEDED(rt.compiler3.As(&info2)))
            {
                UINT32 commits = 0;
                char*  hash    = nullptr;
                if (SUCCEEDED(info2->GetCommitInfo(&commits, &hash)) && hash)
                {
                    rt.version += std::format(" ({} {})", commits, hash);
                    CoTaskMemFree(hash);
                }
            }
            ComPtr<IDxcVersionInfo3> info3;
            if (SUCCEEDED(rt.compiler3.As(&info3)))
            {
                char* custom = nullptr;
                if (SUCCEEDED(info3->GetCustomVersionString(&custom)) && custom)
                {
                    rt.version += std::format(" {}", custom);
                    CoTaskMemFree(custom);
                }
            }
            rt.ok = true;
            DE_LOG_INFO(LogCategory::Render, "DXC loaded from '{}': {}", dir.string(), rt.version);
        }

        DxcRuntime& loadedRuntime()
        {
            DxcRuntime& rt = runtime();
            std::call_once(rt.once, [&rt]() { loadRuntime(rt); });
            return rt;
        }

        // Wraps the default include handler and records each opened file with its content hash.
        // Lives on the caller's stack for one Compile call, so reference counting is a no-op.
        class RecordingIncludeHandler final : public IDxcIncludeHandler
        {
        public:
            RecordingIncludeHandler(IDxcUtils* utils, std::vector<ShaderCacheDependency>& deps) :
                m_deps(deps)
            {
                if (FAILED(utils->CreateDefaultIncludeHandler(&m_inner)))
                    m_inner.Reset();
            }

            RecordingIncludeHandler(const RecordingIncludeHandler&)            = delete;
            RecordingIncludeHandler& operator=(const RecordingIncludeHandler&) = delete;

            bool valid() const
            {
                return m_inner != nullptr;
            }

            HRESULT STDMETHODCALLTYPE LoadSource(LPCWSTR filename, IDxcBlob** includeSource) override
            {
                const HRESULT hr = m_inner->LoadSource(filename, includeSource);
                if (SUCCEEDED(hr) && includeSource && *includeSource)
                {
                    ShaderCacheDependency dep;
                    dep.path = narrow(filename);
                    dep.hash = hashBytes64((*includeSource)->GetBufferPointer(), (*includeSource)->GetBufferSize());
                    m_deps.push_back(std::move(dep));
                }
                return hr;
            }

            HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override
            {
                if (!object)
                    return E_POINTER;
                if (riid == __uuidof(IDxcIncludeHandler) || riid == __uuidof(IUnknown))
                {
                    *object = static_cast<IDxcIncludeHandler*>(this);
                    return S_OK;
                }
                *object = nullptr;
                return E_NOINTERFACE;
            }

            ULONG STDMETHODCALLTYPE AddRef() override
            {
                return 1;
            }
            ULONG STDMETHODCALLTYPE Release() override
            {
                return 1;
            }

        private:
            ComPtr<IDxcIncludeHandler>          m_inner;
            std::vector<ShaderCacheDependency>& m_deps;
        };

        void buildArguments(const DxcCompileRequest& request, std::vector<std::wstring>& out)
        {
            out.clear();
            out.push_back(request.path.wstring());
            out.push_back(L"-E");
            out.push_back(widen(request.entry ? request.entry : ""));
            out.push_back(L"-T");
            out.push_back(widen(shaderProfile(request.stage, false)));
            out.push_back(L"-HV");
            out.push_back(L"2021");
            out.push_back(L"-Ges"); // strict mode, as D3DCOMPILE_ENABLE_STRICTNESS was for FXC
            out.push_back(L"-I");
            out.push_back(request.path.parent_path().wstring());
            for (const ShaderDefine& d : request.defines)
            {
                out.push_back(L"-D");
                out.push_back(widen(d.name + "=" + d.value));
            }
            out.push_back(request.optimize ? L"-O3" : L"-Od");
            if (request.debugInfo)
            {
                out.push_back(L"-Zi");
                out.push_back(L"-Qembed_debug");
            }
        }
    } // namespace

    bool dxcAvailable()
    {
        return loadedRuntime().ok;
    }

    const std::string& dxcVersionString()
    {
        return loadedRuntime().version;
    }

    std::string dxcArgumentString(const DxcCompileRequest& request)
    {
        std::vector<std::wstring> args;
        buildArguments(request, args);
        std::string out;
        // Skip the source path (argument 0); the cache key hashes it separately.
        for (size_t i = 1; i < args.size(); ++i)
        {
            out += narrow(args[i].c_str());
            out += ' ';
        }
        return out;
    }

    bool dxcCompile(const DxcCompileRequest& request, ComPtr<ID3DBlob>& outBytecode, std::vector<ShaderCacheDependency>& outDependencies)
    {
        outBytecode.Reset();
        outDependencies.clear();
        DxcRuntime& rt = loadedRuntime();
        if (!rt.ok || !request.source || !request.entry)
            return false;

        std::vector<std::wstring> args;
        buildArguments(request, args);
        std::vector<LPCWSTR> argv;
        argv.reserve(args.size());
        for (const std::wstring& a : args)
            argv.push_back(a.c_str());

        DxcBuffer source{};
        source.Ptr      = request.source->data();
        source.Size     = request.source->size();
        source.Encoding = DXC_CP_UTF8;

        std::lock_guard<std::mutex> lock(rt.compileMutex);
        RecordingIncludeHandler     includes(rt.utils.Get(), outDependencies);
        if (!includes.valid())
        {
            DE_LOG_ERROR(LogCategory::Render, "DXC: CreateDefaultIncludeHandler failed");
            return false;
        }

        ComPtr<IDxcResult> result;
        HRESULT            hr = rt.compiler3->Compile(&source, argv.data(), static_cast<UINT32>(argv.size()), &includes, IID_PPV_ARGS(&result));
        if (SUCCEEDED(hr) && result)
            result->GetStatus(&hr);

        ComPtr<IDxcBlobUtf8> errors;
        if (result)
            result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr);
        const bool  hasText = errors && errors->GetStringLength() > 0;
        const char* text    = hasText ? errors->GetStringPointer() : "";

        if (FAILED(hr))
        {
            DE_LOG_ERROR(LogCategory::Render, "Shader compile failed ({} {} from '{}'): {}", request.entry, shaderProfile(request.stage, false), request.path.string(), hasText ? text : "unknown");
            outDependencies.clear();
            return false;
        }
        if (hasText)
            DE_LOG_WARN(LogCategory::Render, "Shader compile warnings ({} {} from '{}'): {}", request.entry, shaderProfile(request.stage, false), request.path.string(), text);

        ComPtr<IDxcBlob> object;
        if (FAILED(result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&object), nullptr)) || !object || object->GetBufferSize() == 0)
        {
            DE_LOG_ERROR(LogCategory::Render, "DXC: no object for {} from '{}'", request.entry, request.path.string());
            return false;
        }
        // IDxcBlob and ID3DBlob share an IID and a layout.
        if (FAILED(object.As(&outBytecode)))
        {
            DE_LOG_ERROR(LogCategory::Render, "DXC: object blob is not an ID3DBlob");
            return false;
        }
        return true;
    }

    bool dxcReflect(const void* bytecode, size_t size, ComPtr<ID3D12ShaderReflection>& out)
    {
        out.Reset();
        DxcRuntime& rt = loadedRuntime();
        if (!rt.ok || !bytecode || size == 0)
            return false;
        DxcBuffer buffer{};
        buffer.Ptr      = bytecode;
        buffer.Size     = size;
        buffer.Encoding = 0;
        return SUCCEEDED(rt.utils->CreateReflection(&buffer, IID_PPV_ARGS(&out)));
    }

} // namespace Dark
