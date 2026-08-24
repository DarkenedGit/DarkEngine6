#include "Render/ShaderCompile.h"
#include "Core/Log.h"
#include "Core/Paths.h"

#include <d3dcompiler.h>

#include <fstream>
#include <vector>

namespace Dark
{

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

        const fs::path exeDir = executableDirectory();
        const fs::path cwd    = fs::current_path();

        const fs::path roots[] = {
            exeDir / "content",
            cwd / "content",
            exeDir / ".." / ".." / ".." / "content", // VS: build/bin/Debug -> repo content
            cwd / ".." / ".." / ".." / "content",
            exeDir / ".." / ".." / "content",
            cwd / ".." / ".." / "content",
        };

        for (const fs::path& root : roots)
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

    bool compileShaderFromFile(const std::filesystem::path& path, const char* entry, const char* target, ComPtr<ID3DBlob>& outBytecode)
    {
        outBytecode.Reset();

        if (!entry || !target)
        {
            DE_LOG_ERROR(LogCategory::Render, "compileShaderFromFile: null entry/target");
            return false;
        }

        std::string source;
        if (!readTextFile(path, source))
            return false;

        // Full path so D3D_COMPILE_STANDARD_FILE_INCLUDE resolves sibling .hlsli files.
        const std::string sourceName = path.string();

        ComPtr<ID3DBlob> errors;
        UINT             flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
        flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

        const HRESULT hr = D3DCompile(
            source.data(),
            source.size(),
            sourceName.c_str(),
            nullptr,
            D3D_COMPILE_STANDARD_FILE_INCLUDE,
            entry,
            target,
            flags,
            0,
            &outBytecode,
            &errors);

        if (FAILED(hr))
        {
            const char* msg = errors ? static_cast<const char*>(errors->GetBufferPointer()) : "unknown";
            DE_LOG_ERROR(LogCategory::Render, "Shader compile failed ({} {} from '{}'): {}", entry, target, path.string(), msg);
            outBytecode.Reset();
            return false;
        }

        return true;
    }

    bool compileShaderFromContent(const char* relativeUnderContent, const char* entry, const char* target, ComPtr<ID3DBlob>& outBytecode)
    {
        if (!relativeUnderContent || relativeUnderContent[0] == '\0')
        {
            DE_LOG_ERROR(LogCategory::Render, "compileShaderFromContent: empty path");
            return false;
        }

        const std::filesystem::path resolved = resolveContentPath(relativeUnderContent);
        if (resolved.empty())
        {
            DE_LOG_ERROR(LogCategory::Render, "compileShaderFromContent: could not find '{}' under content roots "
                         "(exe/content, cwd/content, ...)",
                         relativeUnderContent);
            return false;
        }

        DE_LOG_INFO(LogCategory::Render, "compileShaderFromContent: loading '{}'", resolved.string());
        return compileShaderFromFile(resolved, entry, target, outBytecode);
    }

} // namespace Dark
