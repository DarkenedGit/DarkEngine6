#include "Ui/RmlUiHost.h"

#include "Assets/Image.h"
#include "Core/ContentRoots.h"
#include "Core/Log.h"
#include "Input/Input.h"
#include "Render/Profile.h"
#include "Render/Renderer.h"
#include "Render/ShaderCompile.h"
#include "Render/Texture2D.h"

#include <d3d12.h>
#include <wrl/client.h>

#pragma warning(push)
#pragma warning(disable : 4100 4127 4189 4201 4244 4267 4458 4701 4702)
#include <RmlUi/Core.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/EventListener.h>
#include <RmlUi/Core/FileInterface.h>
#include <RmlUi/Core/RenderInterface.h>
#include <RmlUi/Core/SystemInterface.h>
#pragma warning(pop)

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace
{

    using Microsoft::WRL::ComPtr;

    constexpr float kStickThreshold = 0.55f;
    constexpr float kNavRepeatDelay = 0.40f;
    constexpr float kNavRepeatRate  = 0.16f;

    struct GpuVertex
    {
        float x, y;
        float r, g, b, a;
        float u, v;
    };

    static_assert(sizeof(GpuVertex) == 32, "RmlUi vertex stride");

    std::string escapeRml(std::string_view text)
    {
        std::string out;
        out.reserve(text.size());
        for (char c : text)
        {
            switch (c)
            {
            case '&':
                out += "&amp;";
                break;
            case '<':
                out += "&lt;";
                break;
            case '>':
                out += "&gt;";
                break;
            default:
                out.push_back(c);
                break;
            }
        }
        return out;
    }

    std::string normalizeContentPath(const std::string& path)
    {
        std::vector<std::string> parts;
        std::string              current;
        for (size_t i = 0; i <= path.size(); ++i)
        {
            const bool end = i == path.size();
            const char c   = end ? '/' : path[i];
            if (c != '/' && c != '\\')
            {
                current.push_back(c);
                continue;
            }
            if (current.empty() || current == ".")
            {
                current.clear();
                continue;
            }
            if (current == "..")
            {
                if (!parts.empty())
                    parts.pop_back();
            }
            else
                parts.push_back(current);
            current.clear();
        }
        std::string out;
        for (size_t i = 0; i < parts.size(); ++i)
        {
            if (i != 0)
                out.push_back('/');
            out += parts[i];
        }
        return out;
    }

    bool failedHr(HRESULT hr, const char* what)
    {
        if (SUCCEEDED(hr))
            return false;
        DE_LOG_ERROR(Dark::LogCategory::Render, "RmlUi: {} failed (HRESULT 0x{:08X})", what, static_cast<unsigned>(hr));
        return true;
    }

    void setIdentity(float* m)
    {
        std::memset(m, 0, sizeof(float) * 16);
        m[0] = m[5] = m[10] = m[15] = 1.0f;
    }

    void setProjection(float* m, float width, float height)
    {
        std::memset(m, 0, sizeof(float) * 16);
        m[0]  = 2.0f / width;
        m[5]  = -2.0f / height;
        m[10] = 1.0f / 20000.0f;
        m[12] = -1.0f;
        m[13] = 1.0f;
        m[14] = 0.5f;
        m[15] = 1.0f;
    }

    // Column-major: index = column * 4 + row. out = a * b.
    void multiply(const float* a, const float* b, float* out)
    {
        float result[16];
        for (int col = 0; col < 4; ++col)
        {
            for (int row = 0; row < 4; ++row)
            {
                float sum = 0.0f;
                for (int k = 0; k < 4; ++k)
                    sum += a[k * 4 + row] * b[col * 4 + k];
                result[col * 4 + row] = sum;
            }
        }
        std::memcpy(out, result, sizeof(result));
    }

    ComPtr<ID3D12Resource> createUpload(ID3D12Device* device, const void* data, UINT size, const char* what)
    {
        ComPtr<ID3D12Resource> resource;
        if (!device || !data || size == 0)
            return resource;

        D3D12_HEAP_PROPERTIES heap{};
        heap.Type                 = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width            = size;
        desc.Height           = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels        = 1;
        desc.SampleDesc.Count = 1;
        desc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        if (failedHr(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&resource)), what))
        {
            resource.Reset();
            return resource;
        }

        void* mapped = nullptr;
        if (failedHr(resource->Map(0, nullptr, &mapped), what) || !mapped)
        {
            resource.Reset();
            return resource;
        }
        std::memcpy(mapped, data, size);
        resource->Unmap(0, nullptr);
        return resource;
    }

    bool isAbsolutePath(const std::string& path)
    {
        if (path.empty())
            return false;
        if (path[0] == '/' || path[0] == '\\')
            return true;
        if (path.size() < 3)
            return false;
        const unsigned char c = static_cast<unsigned char>(path[0]);
        const bool          letter = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
        return letter && path[1] == ':' && (path[2] == '/' || path[2] == '\\');
    }

    std::filesystem::path resolveContentFile(const std::string& path)
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        const fs::path  asGiven(path);
        if (asGiven.is_absolute() && fs::is_regular_file(asGiven, ec))
            return asGiven;
        for (const fs::path& root : Dark::contentRootCandidates())
        {
            const fs::path candidate = root / asGiven;
            if (fs::is_regular_file(candidate, ec))
                return candidate;
        }
        return {};
    }

    bool premultiplyStraight(const Dark::Image& image, std::vector<uint8_t>& out)
    {
        if (image.format() != Dark::ImageFormat::RGBA8 || !image.pixels() || image.width() == 0 || image.height() == 0)
            return false;
        const uint32_t w        = image.width();
        const uint32_t h        = image.height();
        const uint32_t srcPitch = image.rowPitchBytes();
        if (srcPitch < w * 4u)
            return false;
        out.resize(static_cast<size_t>(w) * h * 4u);
        const uint8_t* src = image.pixels();
        uint8_t*       dst = out.data();
        for (uint32_t y = 0; y < h; ++y)
        {
            const uint8_t* row = src + static_cast<size_t>(y) * srcPitch;
            for (uint32_t x = 0; x < w; ++x)
            {
                const uint8_t* p = row + static_cast<size_t>(x) * 4u;
                const unsigned a = p[3];
                dst[0]           = static_cast<uint8_t>((p[0] * a) / 255u);
                dst[1]           = static_cast<uint8_t>((p[1] * a) / 255u);
                dst[2]           = static_cast<uint8_t>((p[2] * a) / 255u);
                dst[3]           = p[3];
                dst += 4;
            }
        }
        return true;
    }

    int keyModifiers(const Dark::Input& input)
    {
        int mods = 0;
        if (input.keyDown(Dark::Key::LeftControl) || input.keyDown(Dark::Key::RightControl))
            mods |= Rml::Input::KM_CTRL;
        if (input.keyDown(Dark::Key::LeftShift) || input.keyDown(Dark::Key::RightShift))
            mods |= Rml::Input::KM_SHIFT;
        if (input.keyDown(Dark::Key::LeftAlt) || input.keyDown(Dark::Key::RightAlt))
            mods |= Rml::Input::KM_ALT;
        return mods;
    }

    Rml::Input::KeyIdentifier keyIdentifier(Dark::Key key)
    {
        const auto vk = static_cast<uint16_t>(key);
        if (vk >= static_cast<uint16_t>('A') && vk <= static_cast<uint16_t>('Z'))
            return static_cast<Rml::Input::KeyIdentifier>(Rml::Input::KI_A + (vk - static_cast<uint16_t>('A')));
        if (vk >= static_cast<uint16_t>('0') && vk <= static_cast<uint16_t>('9'))
            return static_cast<Rml::Input::KeyIdentifier>(Rml::Input::KI_0 + (vk - static_cast<uint16_t>('0')));
        switch (key)
        {
        case Dark::Key::Tab:       return Rml::Input::KI_TAB;
        case Dark::Key::Enter:     return Rml::Input::KI_RETURN;
        case Dark::Key::Space:     return Rml::Input::KI_SPACE;
        case Dark::Key::Backspace: return Rml::Input::KI_BACK;
        case Dark::Key::Delete:    return Rml::Input::KI_DELETE;
        case Dark::Key::Left:      return Rml::Input::KI_LEFT;
        case Dark::Key::Up:        return Rml::Input::KI_UP;
        case Dark::Key::Right:     return Rml::Input::KI_RIGHT;
        case Dark::Key::Down:      return Rml::Input::KI_DOWN;
        case Dark::Key::Escape:    return Rml::Input::KI_ESCAPE;
        default:                   return Rml::Input::KI_UNKNOWN;
        }
    }

    Rml::Input::KeyIdentifier desiredDirection(const Dark::Input& input)
    {
        if (input.buttonDown(Dark::GamepadButton::DPadLeft))
            return Rml::Input::KI_LEFT;
        if (input.buttonDown(Dark::GamepadButton::DPadRight))
            return Rml::Input::KI_RIGHT;
        if (input.buttonDown(Dark::GamepadButton::DPadUp))
            return Rml::Input::KI_UP;
        if (input.buttonDown(Dark::GamepadButton::DPadDown))
            return Rml::Input::KI_DOWN;

        const float x  = input.axis(Dark::GamepadAxis::LeftX);
        const float y  = input.axis(Dark::GamepadAxis::LeftY);
        const float ax = x < 0.0f ? -x : x;
        const float ay = y < 0.0f ? -y : y;
        if (ax < kStickThreshold && ay < kStickThreshold)
            return Rml::Input::KI_UNKNOWN;
        if (ax >= ay)
            return x > 0.0f ? Rml::Input::KI_RIGHT : Rml::Input::KI_LEFT;
        // XInput Y is up-positive.
        return y > 0.0f ? Rml::Input::KI_UP : Rml::Input::KI_DOWN;
    }

    Rml::Input::KeyIdentifier menuDirection(const Dark::Input& input)
    {
        if (input.keyDown(Dark::Key::Left))
            return Rml::Input::KI_LEFT;
        if (input.keyDown(Dark::Key::Right))
            return Rml::Input::KI_RIGHT;
        if (input.keyDown(Dark::Key::Up) || input.keyDown(Dark::Key::W))
            return Rml::Input::KI_UP;
        if (input.keyDown(Dark::Key::Down) || input.keyDown(Dark::Key::S))
            return Rml::Input::KI_DOWN;
        return desiredDirection(input);
    }

    // Pause keeps W, S, the stick, and left/right for the fly camera. Arrows and the D-pad only.
    Rml::Input::KeyIdentifier pauseDirection(const Dark::Input& input)
    {
        if (input.keyDown(Dark::Key::Up))
            return Rml::Input::KI_UP;
        if (input.keyDown(Dark::Key::Down))
            return Rml::Input::KI_DOWN;
        if (input.buttonDown(Dark::GamepadButton::DPadUp))
            return Rml::Input::KI_UP;
        if (input.buttonDown(Dark::GamepadButton::DPadDown))
            return Rml::Input::KI_DOWN;
        return Rml::Input::KI_UNKNOWN;
    }

    struct DirectionRepeat
    {
        Rml::Input::KeyIdentifier key  = Rml::Input::KI_UNKNOWN;
        float                     held = 0.0f;
        float                     next = 0.0f;

        void clear(Rml::Context* context, int mods)
        {
            if (context && key != Rml::Input::KI_UNKNOWN)
                context->ProcessKeyUp(key, mods);
            key  = Rml::Input::KI_UNKNOWN;
            held = 0.0f;
            next = 0.0f;
        }

        void tick(Rml::Context* context, int mods, Rml::Input::KeyIdentifier desired, float dt)
        {
            if (!context)
                return;
            if (desired == Rml::Input::KI_UNKNOWN)
            {
                clear(context, mods);
                return;
            }
            if (desired != key)
            {
                if (key != Rml::Input::KI_UNKNOWN)
                    context->ProcessKeyUp(key, mods);
                key  = desired;
                held = 0.0f;
                next = kNavRepeatDelay;
                context->ProcessKeyDown(key, mods);
                return;
            }
            held += dt;
            while (held >= next)
            {
                context->ProcessKeyDown(key, mods);
                next += kNavRepeatRate;
            }
        }
    };

    // One axis of the settings page. Unlike DirectionRepeat, this does not feed keys to Rml.
    // Left and right step the volume slider by 5. Up and down scroll a list.
    struct AxisRepeat
    {
        int   dir  = 0;
        float held = 0.0f;
        float next = 0.0f;

        void clear()
        {
            dir  = 0;
            held = 0.0f;
            next = 0.0f;
        }

        bool fire(int desired, float dt)
        {
            if (desired == 0)
            {
                clear();
                return false;
            }
            if (desired != dir)
            {
                dir  = desired;
                held = 0.0f;
                next = kNavRepeatDelay;
                return true;
            }
            held += dt;
            if (held < next)
                return false;
            next += kNavRepeatRate;
            return true;
        }
    };

    struct GeoSlot
    {
        bool                   live = false;
        ComPtr<ID3D12Resource> vb;
        ComPtr<ID3D12Resource> ib;
        UINT                   indexCount = 0;
    };

    struct TexSlot
    {
        bool            live  = false;
        float           flipV = 0.0f;
        Dark::Texture2D texture;
    };

    class RmlRender final : public Rml::RenderInterface
    {
    public:
        void setRenderer(Dark::Renderer* renderer) { m_renderer = renderer; }

        bool createPipeline(Dark::Renderer& renderer)
        {
            releaseGpu(renderer);
            ID3D12Device* device = renderer.device();
            if (!device)
            {
                DE_LOG_ERROR(Dark::LogCategory::Render, "RmlUi: renderer has no device");
                return false;
            }

            D3D12_DESCRIPTOR_RANGE srvRange{};
            srvRange.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            srvRange.NumDescriptors                    = 1;
            srvRange.BaseShaderRegister                = 0;
            srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

            D3D12_ROOT_PARAMETER params[2]{};
            params[0].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
            params[0].ShaderVisibility         = D3D12_SHADER_VISIBILITY_ALL;
            params[0].Constants.ShaderRegister = 0;
            params[0].Constants.Num32BitValues = 17;

            params[1].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            params[1].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;
            params[1].DescriptorTable.NumDescriptorRanges = 1;
            params[1].DescriptorTable.pDescriptorRanges   = &srvRange;

            D3D12_STATIC_SAMPLER_DESC samp{};
            samp.Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
            samp.AddressU         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            samp.AddressV         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            samp.AddressW         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            samp.MaxLOD           = D3D12_FLOAT32_MAX;
            samp.ShaderRegister   = 0;
            samp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

            D3D12_ROOT_SIGNATURE_DESC rsDesc{};
            rsDesc.NumParameters     = 2;
            rsDesc.pParameters       = params;
            rsDesc.NumStaticSamplers = 1;
            rsDesc.pStaticSamplers   = &samp;
            rsDesc.Flags             = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

            ComPtr<ID3DBlob> rsBlob;
            ComPtr<ID3DBlob> rsErr;
            if (failedHr(D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &rsErr), "D3D12SerializeRootSignature"))
            {
                if (rsErr)
                    DE_LOG_ERROR(Dark::LogCategory::Render, "RmlUi root signature: {}", static_cast<const char*>(rsErr->GetBufferPointer()));
                return false;
            }
            if (failedHr(device->CreateRootSignature(0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(), IID_PPV_ARGS(&m_root)), "CreateRootSignature"))
                return false;

            ComPtr<ID3DBlob> vs;
            ComPtr<ID3DBlob> ps;
            if (!Dark::compileShaderFromContent("shaders/RmlUi.hlsl", "VSMain", Dark::ShaderStage::Vertex, vs)
                || !Dark::compileShaderFromContent("shaders/RmlUi.hlsl", "PSMain", Dark::ShaderStage::Pixel, ps))
            {
                m_root.Reset();
                return false;
            }

            D3D12_INPUT_ELEMENT_DESC layout[3]{};
            layout[0].SemanticName      = "POSITION";
            layout[0].Format            = DXGI_FORMAT_R32G32_FLOAT;
            layout[0].InputSlotClass    = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
            layout[1].SemanticName      = "COLOR";
            layout[1].Format            = DXGI_FORMAT_R32G32B32A32_FLOAT;
            layout[1].AlignedByteOffset = 8;
            layout[1].InputSlotClass    = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
            layout[2].SemanticName      = "TEXCOORD";
            layout[2].Format            = DXGI_FORMAT_R32G32_FLOAT;
            layout[2].AlignedByteOffset = 24;
            layout[2].InputSlotClass    = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;

            D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
            pso.pRootSignature                        = m_root.Get();
            pso.VS                                    = { vs->GetBufferPointer(), vs->GetBufferSize() };
            pso.PS                                    = { ps->GetBufferPointer(), ps->GetBufferSize() };
            pso.SampleMask                            = UINT_MAX;
            pso.BlendState.RenderTarget[0].BlendEnable           = TRUE;
            pso.BlendState.RenderTarget[0].SrcBlend              = D3D12_BLEND_ONE;
            pso.BlendState.RenderTarget[0].DestBlend             = D3D12_BLEND_INV_SRC_ALPHA;
            pso.BlendState.RenderTarget[0].BlendOp               = D3D12_BLEND_OP_ADD;
            pso.BlendState.RenderTarget[0].SrcBlendAlpha         = D3D12_BLEND_ONE;
            pso.BlendState.RenderTarget[0].DestBlendAlpha        = D3D12_BLEND_INV_SRC_ALPHA;
            pso.BlendState.RenderTarget[0].BlendOpAlpha          = D3D12_BLEND_OP_ADD;
            pso.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
            pso.RasterizerState.FillMode              = D3D12_FILL_MODE_SOLID;
            pso.RasterizerState.CullMode              = D3D12_CULL_MODE_NONE;
            pso.RasterizerState.DepthClipEnable       = TRUE;
            pso.DepthStencilState.DepthEnable         = FALSE;
            pso.DepthStencilState.DepthWriteMask      = D3D12_DEPTH_WRITE_MASK_ZERO;
            pso.InputLayout                           = { layout, 3 };
            pso.PrimitiveTopologyType                 = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            pso.NumRenderTargets                      = 1;
            pso.RTVFormats[0]                         = DXGI_FORMAT_R8G8B8A8_UNORM;
            pso.DSVFormat                             = DXGI_FORMAT_UNKNOWN;
            pso.SampleDesc                            = { 1, 0 };

            if (failedHr(device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_pso)), "CreateGraphicsPipelineState"))
            {
                m_root.Reset();
                return false;
            }
            m_root->SetName(L"DE.RmlUiRoot");
            m_pso->SetName(L"DE.RmlUiPso");

            if (!m_white.createSolidColor(renderer, 255, 255, 255, 255, Dark::Color::TextureUsage::Font))
            {
                DE_LOG_ERROR(Dark::LogCategory::Render, "RmlUi: white texture failed");
                releaseGpu(renderer);
                return false;
            }
            setIdentity(m_transform);
            DE_LOG_INFO(Dark::LogCategory::Render, "RmlUi pipeline ready");
            return true;
        }

        void releaseGpu(Dark::Renderer& renderer)
        {
            for (GeoSlot& slot : m_geometry)
                releaseGeo(renderer, slot);
            for (TexSlot& slot : m_textures)
                releaseTex(renderer, slot);
            m_geometry.clear();
            m_textures.clear();
            if (m_white.valid())
                m_white.deferRelease(renderer);
            renderer.deferRelease(m_pso.Get());
            renderer.deferRelease(m_root.Get());
            m_pso.Reset();
            m_root.Reset();
        }

        void begin(Dark::Renderer& renderer)
        {
            m_renderer = &renderer;
            m_cmd      = renderer.commandList();
            m_viewW    = renderer.width() > 0 ? static_cast<int>(renderer.width()) : 1;
            m_viewH    = renderer.height() > 0 ? static_cast<int>(renderer.height()) : 1;
            setProjection(m_projection, static_cast<float>(m_viewW), static_cast<float>(m_viewH));
            m_scissorEnabled = false;
            if (!m_cmd)
                return;
            renderer.bindBackBuffer();
            const D3D12_VIEWPORT viewport = renderer.viewport();
            m_cmd->RSSetViewports(1, &viewport);
            applyScissor();
        }

        void end()
        {
            m_scissorEnabled = false;
            applyScissor();
            m_cmd = nullptr;
        }

        Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices) override
        {
            if (!m_renderer || !m_renderer->device() || vertices.size() == 0 || indices.size() == 0)
                return 0;

            std::vector<GpuVertex> gpu(vertices.size());
            constexpr float        scale = 1.0f / 255.0f;
            for (size_t i = 0; i < vertices.size(); ++i)
            {
                const Rml::Vertex& src = vertices[i];
                GpuVertex&         dst = gpu[i];
                dst.x                  = src.position.x;
                dst.y                  = src.position.y;
                dst.r                  = static_cast<float>(src.colour.red) * scale;
                dst.g                  = static_cast<float>(src.colour.green) * scale;
                dst.b                  = static_cast<float>(src.colour.blue) * scale;
                dst.a                  = static_cast<float>(src.colour.alpha) * scale;
                dst.u                  = src.tex_coord.x;
                dst.v                  = src.tex_coord.y;
            }
            std::vector<uint32_t> index32(indices.size());
            for (size_t i = 0; i < indices.size(); ++i)
                index32[i] = static_cast<uint32_t>(indices[i]);

            ComPtr<ID3D12Resource> vb = createUpload(m_renderer->device(), gpu.data(), static_cast<UINT>(gpu.size() * sizeof(GpuVertex)), "vertex buffer");
            ComPtr<ID3D12Resource> ib = createUpload(m_renderer->device(), index32.data(), static_cast<UINT>(index32.size() * sizeof(uint32_t)), "index buffer");
            if (!vb || !ib)
                return 0;

            const Rml::CompiledGeometryHandle handle = allocGeo();
            GeoSlot&                          slot   = m_geometry[static_cast<size_t>(handle - 1)];
            slot.vb                                  = std::move(vb);
            slot.ib                                  = std::move(ib);
            slot.indexCount                          = static_cast<UINT>(index32.size());
            return handle;
        }

        void RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation, Rml::TextureHandle texture) override
        {
            if (!m_cmd || !m_pso || !m_root || geometry == 0 || geometry > m_geometry.size())
                return;
            GeoSlot& slot = m_geometry[static_cast<size_t>(geometry - 1)];
            if (!slot.live || !slot.vb || !slot.ib || slot.indexCount == 0)
                return;

            float flipV = 0.0f;
            const Dark::Texture2D* sampled = &m_white;
            if (texture != 0 && texture <= m_textures.size())
            {
                TexSlot& tex = m_textures[static_cast<size_t>(texture - 1)];
                if (tex.live && tex.texture.valid())
                {
                    sampled = &tex.texture;
                    flipV   = tex.flipV;
                }
            }
            if (!sampled->valid())
                return;

            float translate[16];
            setIdentity(translate);
            translate[12] = translation.x;
            translate[13] = translation.y;
            float world[16];
            multiply(m_transform, translate, world);
            float finalM[16];
            multiply(m_projection, world, finalM);

            float constants[17];
            std::memcpy(constants, finalM, sizeof(finalM));
            constants[16] = flipV;

            m_cmd->SetGraphicsRootSignature(m_root.Get());
            m_cmd->SetPipelineState(m_pso.Get());
            m_cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            m_cmd->SetGraphicsRoot32BitConstants(0, 17, constants, 0);
            sampled->bind(m_cmd, 1);

            D3D12_VERTEX_BUFFER_VIEW vbv{};
            vbv.BufferLocation = slot.vb->GetGPUVirtualAddress();
            vbv.SizeInBytes    = static_cast<UINT>(slot.vb->GetDesc().Width);
            vbv.StrideInBytes  = sizeof(GpuVertex);
            D3D12_INDEX_BUFFER_VIEW ibv{};
            ibv.BufferLocation = slot.ib->GetGPUVirtualAddress();
            ibv.SizeInBytes    = slot.indexCount * static_cast<UINT>(sizeof(uint32_t));
            ibv.Format         = DXGI_FORMAT_R32_UINT;
            m_cmd->IASetVertexBuffers(0, 1, &vbv);
            m_cmd->IASetIndexBuffer(&ibv);
            m_cmd->DrawIndexedInstanced(slot.indexCount, 1, 0, 0, 0);
        }

        void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override
        {
            if (!m_renderer || geometry == 0 || geometry > m_geometry.size())
                return;
            releaseGeo(*m_renderer, m_geometry[static_cast<size_t>(geometry - 1)]);
        }

        Rml::TextureHandle LoadTexture(Rml::Vector2i& texture_dimensions, const Rml::String& source) override
        {
            texture_dimensions = Rml::Vector2i(0, 0);
            if (!m_renderer)
                return 0;
            const std::filesystem::path path = resolveContentFile(source);
            if (path.empty())
            {
                DE_LOG_WARN(Dark::LogCategory::Render, "RmlUi: texture not found: {}", source);
                return 0;
            }
            Dark::Image image;
            if (!image.createFromFile(path))
            {
                DE_LOG_WARN(Dark::LogCategory::Render, "RmlUi: texture decode failed: {}", source);
                return 0;
            }
            std::vector<uint8_t> premul;
            if (!premultiplyStraight(image, premul))
            {
                DE_LOG_WARN(Dark::LogCategory::Render, "RmlUi: texture is not RGBA8: {}", source);
                return 0;
            }
            const Rml::TextureHandle handle = allocTex(0.0f);
            TexSlot&                 slot   = m_textures[static_cast<size_t>(handle - 1)];
            if (!slot.texture.createFromRGBA(*m_renderer, premul.data(), image.width(), image.height(), image.width() * 4u, Dark::Color::TextureUsage::Font))
            {
                DE_LOG_WARN(Dark::LogCategory::Render, "RmlUi: texture upload failed: {}", source);
                slot.live = false;
                return 0;
            }
            texture_dimensions = Rml::Vector2i(static_cast<int>(image.width()), static_cast<int>(image.height()));
            return handle;
        }

        Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i source_dimensions) override
        {
            if (!m_renderer || source_dimensions.x <= 0 || source_dimensions.y <= 0)
                return 0;
            const size_t need = static_cast<size_t>(source_dimensions.x) * static_cast<size_t>(source_dimensions.y) * 4u;
            if (source.size() < need || !source.data())
                return 0;
            // Font rows are stored top to bottom, matching this UNORM upload. Do not flip V.
            const Rml::TextureHandle handle = allocTex(0.0f);
            TexSlot&                 slot   = m_textures[static_cast<size_t>(handle - 1)];
            if (!slot.texture.createFromRGBA(*m_renderer, source.data(), static_cast<uint32_t>(source_dimensions.x), static_cast<uint32_t>(source_dimensions.y),
                    static_cast<uint32_t>(source_dimensions.x) * 4u, Dark::Color::TextureUsage::Font))
            {
                DE_LOG_WARN(Dark::LogCategory::Render, "RmlUi: generated texture upload failed");
                slot.live = false;
                return 0;
            }
            return handle;
        }

        void ReleaseTexture(Rml::TextureHandle texture) override
        {
            if (!m_renderer || texture == 0 || texture > m_textures.size())
                return;
            releaseTex(*m_renderer, m_textures[static_cast<size_t>(texture - 1)]);
        }

        void EnableScissorRegion(bool enable) override
        {
            m_scissorEnabled = enable;
            applyScissor();
        }

        void SetScissorRegion(Rml::Rectanglei region) override
        {
            m_region = region;
            if (m_scissorEnabled)
                applyScissor();
        }

        void SetTransform(const Rml::Matrix4f* transform) override
        {
            if (!transform)
                setIdentity(m_transform);
            else
                std::memcpy(m_transform, transform->data(), sizeof(m_transform));
        }

    private:
        Rml::CompiledGeometryHandle allocGeo()
        {
            for (size_t i = 0; i < m_geometry.size(); ++i)
            {
                if (!m_geometry[i].live)
                {
                    m_geometry[i]      = GeoSlot{};
                    m_geometry[i].live = true;
                    return static_cast<Rml::CompiledGeometryHandle>(i + 1);
                }
            }
            m_geometry.push_back(GeoSlot{});
            m_geometry.back().live = true;
            return static_cast<Rml::CompiledGeometryHandle>(m_geometry.size());
        }

        Rml::TextureHandle allocTex(float flipV)
        {
            for (size_t i = 0; i < m_textures.size(); ++i)
            {
                if (!m_textures[i].live)
                {
                    m_textures[i]       = TexSlot{};
                    m_textures[i].live  = true;
                    m_textures[i].flipV = flipV;
                    return static_cast<Rml::TextureHandle>(i + 1);
                }
            }
            m_textures.push_back(TexSlot{});
            m_textures.back().live  = true;
            m_textures.back().flipV = flipV;
            return static_cast<Rml::TextureHandle>(m_textures.size());
        }

        static void releaseGeo(Dark::Renderer& renderer, GeoSlot& slot)
        {
            renderer.deferRelease(slot.vb.Get());
            renderer.deferRelease(slot.ib.Get());
            slot.vb.Reset();
            slot.ib.Reset();
            slot.live       = false;
            slot.indexCount = 0;
        }

        static void releaseTex(Dark::Renderer& renderer, TexSlot& slot)
        {
            if (slot.texture.valid())
                slot.texture.deferRelease(renderer);
            slot.live  = false;
            slot.flipV = 0.0f;
        }

        void applyScissor()
        {
            if (!m_cmd)
                return;
            D3D12_RECT rect{};
            if (!m_scissorEnabled)
            {
                rect.right  = m_viewW;
                rect.bottom = m_viewH;
            }
            else
            {
                int left   = m_region.Left();
                int top    = m_region.Top();
                int right  = m_region.Right();
                int bottom = m_region.Bottom();
                if (left < 0)
                    left = 0;
                if (top < 0)
                    top = 0;
                if (right > m_viewW)
                    right = m_viewW;
                if (bottom > m_viewH)
                    bottom = m_viewH;
                if (right < left)
                    right = left;
                if (bottom < top)
                    bottom = top;
                rect.left   = left;
                rect.top    = top;
                rect.right  = right;
                rect.bottom = bottom;
            }
            m_cmd->RSSetScissorRects(1, &rect);
        }

        Dark::Renderer*        m_renderer       = nullptr;
        ID3D12GraphicsCommandList* m_cmd        = nullptr;
        ComPtr<ID3D12RootSignature> m_root;
        ComPtr<ID3D12PipelineState> m_pso;
        Dark::Texture2D        m_white;
        std::vector<GeoSlot>   m_geometry;
        std::vector<TexSlot>   m_textures;
        float                  m_projection[16]{};
        float                  m_transform[16]{};
        Rml::Rectanglei        m_region{};
        int                    m_viewW          = 1;
        int                    m_viewH          = 1;
        bool                   m_scissorEnabled = false;
    };

    class RmlSystem final : public Rml::SystemInterface
    {
    public:
        RmlSystem()
            : m_start(std::chrono::steady_clock::now())
        {
        }

        double GetElapsedTime() override
        {
            return std::chrono::duration<double>(std::chrono::steady_clock::now() - m_start).count();
        }

        void JoinPath(Rml::String& translated_path, const Rml::String& document_path, const Rml::String& path) override
        {
            if (path.empty() || isAbsolutePath(path))
            {
                translated_path = path;
                return;
            }
            std::string combined;
            const std::size_t slash = document_path.find_last_of("/\\");
            if (slash == Rml::String::npos)
                combined = path;
            else
            {
                combined.assign(document_path, 0, slash + 1);
                combined += path;
            }
            translated_path = normalizeContentPath(combined);
        }

        bool LogMessage(Rml::Log::Type type, const Rml::String& message) override
        {
            switch (type)
            {
            case Rml::Log::LT_ERROR:
            case Rml::Log::LT_ASSERT:
                DE_LOG_ERROR("RmlUi: {}", message);
                break;
            case Rml::Log::LT_WARNING:
                DE_LOG_WARN("RmlUi: {}", message);
                break;
            case Rml::Log::LT_DEBUG:
                DE_LOG_TRACE("RmlUi: {}", message);
                break;
            default:
                DE_LOG_INFO("RmlUi: {}", message);
                break;
            }
            return true;
        }

    private:
        std::chrono::steady_clock::time_point m_start;
    };

    class RmlFiles final : public Rml::FileInterface
    {
    public:
        Rml::FileHandle Open(const Rml::String& path) override
        {
            const std::filesystem::path resolved = resolveContentFile(path);
            if (resolved.empty())
            {
                DE_LOG_WARN("RmlUi: file not found: {}", path);
                return 0;
            }
            std::FILE* file = nullptr;
            if (_wfopen_s(&file, resolved.wstring().c_str(), L"rb") != 0 || !file)
            {
                DE_LOG_WARN("RmlUi: could not open {}", path);
                return 0;
            }
            return static_cast<Rml::FileHandle>(reinterpret_cast<uintptr_t>(file));
        }

        void Close(Rml::FileHandle file) override
        {
            if (std::FILE* handle = filePointer(file))
                std::fclose(handle);
        }

        size_t Read(void* buffer, size_t size, Rml::FileHandle file) override
        {
            std::FILE* handle = filePointer(file);
            if (!handle || !buffer || size == 0)
                return 0;
            return std::fread(buffer, 1, size, handle);
        }

        bool Seek(Rml::FileHandle file, long offset, int origin) override
        {
            std::FILE* handle = filePointer(file);
            if (!handle)
                return false;
            return std::fseek(handle, offset, origin) == 0;
        }

        size_t Tell(Rml::FileHandle file) override
        {
            std::FILE* handle = filePointer(file);
            if (!handle)
                return 0;
            const long position = std::ftell(handle);
            if (position < 0)
                return 0;
            return static_cast<size_t>(position);
        }

    private:
        static std::FILE* filePointer(Rml::FileHandle file)
        {
            if (file == 0)
                return nullptr;
            return reinterpret_cast<std::FILE*>(static_cast<uintptr_t>(file));
        }
    };

    std::string entryId(Rml::Element* element)
    {
        for (int depth = 0; element && depth < 8; ++depth, element = element->GetParentNode())
        {
            if (!element->HasAttribute("data-id"))
                continue;
            const Rml::String id = element->GetAttribute<Rml::String>("data-id", Rml::String());
            if (!id.empty())
                return std::string(id);
        }
        return {};
    }

    struct FrontEvents
    {
        bool        loading    = false;
        bool        activate   = false;
        bool        cancel     = false;
        std::string hoverId;
        std::string focusId;
        std::string activateId;
        std::string selectedId;
    };

    class FrontListener final : public Rml::EventListener
    {
    public:
        FrontEvents* events = nullptr;

        void ProcessEvent(Rml::Event& event) override
        {
            if (!events || events->loading)
                return;
            const std::string id = entryId(event.GetTargetElement());
            if (id.empty())
                return;
            if (event == "click")
            {
                events->activate   = true;
                events->activateId = id;
            }
            else if (event == "mouseover")
                events->hoverId = id;
        }
    };

    struct SettingsEvents
    {
        bool  loading       = false;
        bool  back          = false;
        bool  cancel        = false;
        bool  volumeChanged = false;
        float masterVolume  = 1.0f;
    };

    void writeVolumeReadout(Rml::ElementDocument* document, float percent, int* shown)
    {
        if (!document)
            return;
        int text = static_cast<int>(percent + 0.5f);
        if (text < 0)
            text = 0;
        if (text > 100)
            text = 100;
        if (shown && *shown == text)
            return;
        if (shown)
            *shown = text;
        Rml::Element* readout = document->GetElementById("volume-value");
        if (!readout)
            return;
        char buf[8];
        std::snprintf(buf, sizeof(buf), "%d%%", text);
        readout->SetInnerRML(buf);
    }

    float readVolumePercent(Rml::Element* slider)
    {
        if (!slider)
            return 100.0f;
        const Rml::Variant* value = slider->GetAttribute("value");
        if (!value)
            return 100.0f;
        return value->Get(100.0f);
    }

    bool elementHasId(Rml::Element* element, const char* id)
    {
        for (int depth = 0; element && depth < 12; element = element->GetParentNode(), ++depth)
        {
            if (element->GetId() == id)
                return true;
        }
        return false;
    }

    class SettingsListener final : public Rml::EventListener
    {
    public:
        SettingsEvents*       events   = nullptr;
        Rml::ElementDocument* document = nullptr;
        bool*                 applying = nullptr;
        int*                  shown    = nullptr;

        void ProcessEvent(Rml::Event& event) override
        {
            if (!events || events->loading)
                return;
            if (applying && *applying)
                return;
            if (event == "click")
            {
                if (entryId(event.GetTargetElement()) == "back")
                    events->back = true;
                return;
            }
            if (event != "change" || !elementHasId(event.GetTargetElement(), "volume"))
                return;
            float percent = event.GetParameter<float>("value", -1.0f);
            if (percent < 0.0f)
                percent = readVolumePercent(document ? document->GetElementById("volume") : nullptr);
            if (percent < 0.0f)
                percent = 0.0f;
            if (percent > 100.0f)
                percent = 100.0f;
            events->volumeChanged = true;
            events->masterVolume  = percent / 100.0f;
            writeVolumeReadout(document, percent, shown);
        }
    };

    struct PauseEvents
    {
        bool        resume  = false;
        bool        step    = false;
        std::string hoverId;
    };

    class PauseListener final : public Rml::EventListener
    {
    public:
        PauseEvents* events = nullptr;

        void ProcessEvent(Rml::Event& event) override
        {
            if (!events)
                return;
            const std::string id = entryId(event.GetTargetElement());
            if (id != "resume" && id != "step")
                return;
            if (event == "click")
            {
                events->resume = id == "resume";
                events->step   = id == "step";
            }
            else if (event == "mouseover")
                events->hoverId = id;
        }
    };

} // namespace

struct RmlUiHost::Impl
{
    RmlRender             render;
    RmlSystem             system;
    RmlFiles              files;
    FrontListener         listener;
    FrontEvents           events;
    SettingsListener      settingsListener;
    SettingsEvents        settingsEvents;
    PauseListener         pauseListener;
    PauseEvents           pauseEvents;
    struct HudModel
    {
        float       suspicion    = 0.0f;
        int         nearbyAlerts = 0;
        bool        hidden       = false;
        Rml::String prompt;
    };
    HudModel              hud;
    Rml::Context*         context     = nullptr;
    Rml::ElementDocument* document    = nullptr;
    Rml::ElementDocument* settings    = nullptr;
    Rml::ElementDocument* pause       = nullptr;
    Rml::ElementDocument* stealth     = nullptr;
    bool                  initialised     = false;
    bool                  frontFailed     = false;
    bool                  settingsFailed  = false;
    bool                  pauseFailed     = false;
    bool                  stealthFailed   = false;
    bool                  armOpen         = false;
    bool                  armSettings     = false;
    bool                  armPause        = false;
    bool                  armStealth      = false;
    bool                  suppressCancel  = false;
    bool                  loadingOn       = false;
    bool                  applyingVolume  = false;
    float                 dpi             = 1.0f;
    int                   volumeShown     = -1;
    std::string           rowKey;
    std::string           titleShown;
    std::string           loadingShown;
    std::string           settingsKey;
    std::string           settingsTitle;
    std::string           actionFocusId;
    std::string           levelFocusId;
    DirectionRepeat       direction;
    AxisRepeat            volumeAxis;
    AxisRepeat            scrollAxis;
};

namespace
{

    void forwardMenuKeys(Rml::Context* context, const Dark::Input& input, int mods)
    {
        const Dark::Key keys[] = { Dark::Key::Tab, Dark::Key::Enter, Dark::Key::Space };
        for (Dark::Key key : keys)
        {
            const Rml::Input::KeyIdentifier id = keyIdentifier(key);
            if (input.keyPressed(key))
                context->ProcessKeyDown(id, mods);
            if (input.keyReleased(key))
                context->ProcessKeyUp(id, mods);
        }
        if (input.buttonPressed(Dark::GamepadButton::A))
            context->ProcessKeyDown(Rml::Input::KI_RETURN, mods);
        if (input.buttonReleased(Dark::GamepadButton::A))
            context->ProcessKeyUp(Rml::Input::KI_RETURN, mods);
    }

    // Enter and gamepad A only. Space climbs the fly camera, so it stays out of this card.
    void forwardPauseKeys(Rml::Context* context, const Dark::Input& input, int mods, bool* ateAction)
    {
        if (!context)
            return;
        if (input.keyPressed(Dark::Key::Enter))
            context->ProcessKeyDown(Rml::Input::KI_RETURN, mods);
        if (input.keyReleased(Dark::Key::Enter))
            context->ProcessKeyUp(Rml::Input::KI_RETURN, mods);
        if (input.buttonPressed(Dark::GamepadButton::A))
        {
            context->ProcessKeyDown(Rml::Input::KI_RETURN, mods);
            if (ateAction)
                *ateAction = true;
        }
        if (input.buttonReleased(Dark::GamepadButton::A))
            context->ProcessKeyUp(Rml::Input::KI_RETURN, mods);
    }

    Rml::Element* findEntry(Rml::ElementDocument* document, const std::string& id)
    {
        if (!document || id.empty())
            return nullptr;
        Rml::ElementList buttons;
        document->GetElementsByTagName(buttons, "button");
        for (Rml::Element* element : buttons)
        {
            if (entryId(element) == id)
                return element;
        }
        return nullptr;
    }

    bool elementIsLevel(Rml::Element* element)
    {
        for (int depth = 0; element && depth < 8; element = element->GetParentNode(), ++depth)
        {
            if (element->IsClassSet("level"))
                return true;
        }
        return false;
    }

    Rml::Element* columnTarget(Rml::ElementDocument* document, bool level, const std::string& preferId)
    {
        if (Rml::Element* preferred = findEntry(document, preferId))
        {
            if (preferred->IsVisible() && preferred->IsClassSet("level") == level)
                return preferred;
        }
        Rml::ElementList buttons;
        document->GetElementsByTagName(buttons, "button");
        for (Rml::Element* button : buttons)
        {
            if (!button || !button->IsVisible())
                continue;
            if (button->IsClassSet("level") == level)
                return button;
        }
        return nullptr;
    }

    std::string rowFingerprint(const RmlFrontMenu& menu)
    {
        std::string out;
        for (const RmlFrontRow& row : menu.rows)
        {
            out.push_back(row.level ? 'L' : 'A');
            out.push_back('\n');
            out += row.id;
            out.push_back('\n');
            out += row.title;
            out.push_back('\n');
            out += row.subtitle;
            out.push_back('\n');
        }
        return out;
    }

    void clearChildren(Rml::Element* parent)
    {
        if (!parent)
            return;
        while (parent->HasChildNodes())
        {
            Rml::Element* child = parent->GetChild(0);
            if (!child)
                break;
            parent->RemoveChild(child);
        }
    }

} // namespace

void RmlUiHost::focusFrontSelection()
{
    if (!m_impl || !m_impl->document)
        return;
    if (Rml::Element* selected = findEntry(m_impl->document, m_impl->events.selectedId))
    {
        selected->Focus(true);
        return;
    }
    Rml::ElementList buttons;
    m_impl->document->GetElementsByTagName(buttons, "button");
    if (!buttons.empty() && buttons[0])
        buttons[0]->Focus(true);
}

void RmlUiHost::setFrontOpen(bool open)
{
    if (!m_impl || !m_impl->document)
        return;
    if (Rml::Element* stage = m_impl->document->GetElementById("stage"))
        stage->SetClass("open", open);
}

void RmlUiHost::rebuildFront(const RmlFrontMenu& menu)
{
    if (!m_impl || !m_impl->document)
        return;
    Rml::Element* actions = m_impl->document->GetElementById("actions");
    Rml::Element* levels  = m_impl->document->GetElementById("level-list");
    Rml::Element* column  = m_impl->document->GetElementById("levels");
    clearChildren(actions);
    clearChildren(levels);
    bool anyLevel = false;
    for (const RmlFrontRow& row : menu.rows)
    {
        Rml::Element* parent = row.level ? levels : actions;
        if (!parent)
            continue;
        if (row.level)
            anyLevel = true;
        Rml::ElementPtr button = m_impl->document->CreateElement("button");
        if (!button)
            continue;
        button->SetClass("row", true);
        if (row.level)
            button->SetClass("level", true);
        button->SetAttribute("data-id", row.id);
        if (row.subtitle.empty())
            button->SetInnerRML(escapeRml(row.title));
        else
        {
            const std::string inner = std::string("<div class=\"label\">") + escapeRml(row.title) + "</div><div class=\"sub\">" + escapeRml(row.subtitle) + "</div>";
            button->SetInnerRML(inner);
        }
        parent->AppendChild(std::move(button));
    }
    if (column)
        column->SetClass("hidden", !anyLevel);
}

void RmlUiHost::rememberFrontFocus()
{
    if (!m_impl || !m_impl->context)
        return;
    Rml::Element* focused = m_impl->context->GetFocusElement();
    const std::string id = entryId(focused);
    if (id.empty())
        return;
    if (elementIsLevel(focused))
        m_impl->levelFocusId = id;
    else
        m_impl->actionFocusId = id;
}

void RmlUiHost::switchFrontColumn(int key)
{
    if (!m_impl || !m_impl->document || !m_impl->context)
        return;
    if (key != Rml::Input::KI_LEFT && key != Rml::Input::KI_RIGHT)
        return;
    rememberFrontFocus();
    const bool onLevel = elementIsLevel(m_impl->context->GetFocusElement());
    Rml::Element* target = nullptr;
    if (key == Rml::Input::KI_RIGHT && !onLevel)
        target = columnTarget(m_impl->document, true, m_impl->levelFocusId);
    else if (key == Rml::Input::KI_LEFT && onLevel)
        target = columnTarget(m_impl->document, false, m_impl->actionFocusId);
    if (!target || !target->Focus(true))
        return;
    target->ScrollIntoView(Rml::ScrollAlignment::Adaptive);
}

namespace
{

    void setDocumentClass(Rml::ElementDocument* document, const char* name, bool on)
    {
        if (!document || !name)
            return;
        Rml::ElementList bodies;
        document->GetElementsByTagName(bodies, "body");
        if (!bodies.empty() && bodies[0])
            bodies[0]->SetClass(name, on);
    }

    std::string settingsFingerprint(const RmlSettingsMenu& menu)
    {
        std::string out = menu.controlsLoaded ? "1\n" : "0\n";
        auto        append = [&](const std::vector<RmlControlRow>& rows, char tag) {
            for (const RmlControlRow& row : rows)
            {
                out.push_back(tag);
                out.push_back('\n');
                out += row.keys;
                out.push_back('\n');
                out += row.what;
                out.push_back('\n');
            }
        };
        append(menu.game, 'G');
        append(menu.engine, 'E');
        return out;
    }

    void fillControlList(Rml::ElementDocument* document, Rml::Element* parent, const std::vector<RmlControlRow>& rows)
    {
        clearChildren(parent);
        if (!document || !parent)
            return;
        for (const RmlControlRow& row : rows)
        {
            Rml::ElementPtr block = document->CreateElement("div");
            if (!block)
                continue;
            block->SetClass("control", true);
            const std::string inner = std::string("<div class=\"keys\">") + escapeRml(row.keys) + "</div><div class=\"what\">" + escapeRml(row.what) + "</div>";
            block->SetInnerRML(inner);
            parent->AppendChild(std::move(block));
        }
    }

} // namespace

void RmlUiHost::loadSettings()
{
    if (!m_impl || !m_impl->context || m_impl->settings || m_impl->settingsFailed)
        return;
    m_impl->settingsListener.events   = &m_impl->settingsEvents;
    m_impl->settingsListener.applying = &m_impl->applyingVolume;
    m_impl->settingsListener.shown    = &m_impl->volumeShown;
    m_impl->settings                  = m_impl->context->LoadDocument("ui/menus/settings.rml");
    if (!m_impl->settings)
    {
        m_impl->settingsFailed = true;
        DE_LOG_WARN("RmlUi: settings document failed to load (ui/menus/settings.rml)");
        return;
    }
    m_impl->settingsListener.document = m_impl->settings;
    m_impl->settings->AddEventListener("click", &m_impl->settingsListener);
    m_impl->settings->AddEventListener("change", &m_impl->settingsListener);
    DE_LOG_INFO("RmlUi settings menu ready");
}

void RmlUiHost::loadPause()
{
    if (!m_impl || !m_impl->context || m_impl->pause || m_impl->pauseFailed)
        return;
    m_impl->pauseListener.events = &m_impl->pauseEvents;
    m_impl->pause                = m_impl->context->LoadDocument("ui/menus/pause.rml");
    if (!m_impl->pause)
    {
        m_impl->pauseFailed = true;
        DE_LOG_WARN("RmlUi: pause document failed to load (ui/menus/pause.rml)");
        return;
    }
    m_impl->pause->AddEventListener("click", &m_impl->pauseListener);
    m_impl->pause->AddEventListener("mouseover", &m_impl->pauseListener);
    DE_LOG_INFO("RmlUi pause menu ready");
}

void RmlUiHost::loadStealth()
{
    if (!m_impl || !m_impl->context || m_impl->stealth || m_impl->stealthFailed)
        return;
    Rml::DataModelConstructor ctor = m_impl->context->CreateDataModel("hud");
    if (!ctor)
    {
        m_impl->stealthFailed = true;
        DE_LOG_WARN("RmlUi: stealth hud data model failed");
        return;
    }
    ctor.RegisterTransformFunc("percent", [](const Rml::VariantList& arguments) -> Rml::Variant {
        if (arguments.empty())
            return {};
        float value = arguments[0].Get<float>(0.0f);
        if (value < 0.0f)
            value = 0.0f;
        if (value > 1.0f)
            value = 1.0f;
        const int pct = static_cast<int>(value * 100.0f + 0.5f);
        char      buf[8];
        std::snprintf(buf, sizeof(buf), "%d%%", pct);
        return Rml::Variant(Rml::String(buf));
    });
    const bool bound = ctor.Bind("suspicion", &m_impl->hud.suspicion) && ctor.Bind("nearby_alerts", &m_impl->hud.nearbyAlerts)
        && ctor.Bind("hidden", &m_impl->hud.hidden) && ctor.Bind("prompt", &m_impl->hud.prompt);
    if (!bound)
    {
        m_impl->stealthFailed = true;
        DE_LOG_WARN("RmlUi: stealth hud data bind failed");
        return;
    }
    m_impl->stealth = m_impl->context->LoadDocument("ui/hud/stealth.rml");
    if (!m_impl->stealth)
    {
        m_impl->stealthFailed = true;
        DE_LOG_WARN("RmlUi: stealth hud document failed to load (ui/hud/stealth.rml)");
        return;
    }
    DE_LOG_INFO("RmlUi stealth hud ready");
}

void RmlUiHost::dirtyStealth()
{
    if (!m_impl || !m_impl->context)
        return;
    Rml::DataModelConstructor ctor = m_impl->context->GetDataModel("hud");
    if (!ctor)
        return;
    Rml::DataModelHandle handle = ctor.GetModelHandle();
    handle.DirtyVariable("suspicion");
    handle.DirtyVariable("nearby_alerts");
    handle.DirtyVariable("hidden");
    handle.DirtyVariable("prompt");
}

void RmlUiHost::setStealthOpen(bool open)
{
    if (!m_impl || !m_impl->stealth)
        return;
    if (Rml::Element* hud = m_impl->stealth->GetElementById("hud"))
        hud->SetClass("open", open);
}

void RmlUiHost::setPauseOpen(bool open)
{
    if (!m_impl || !m_impl->pause)
        return;
    if (Rml::Element* stage = m_impl->pause->GetElementById("stage"))
        stage->SetClass("open", open);
}

void RmlUiHost::setSettingsOpen(bool open)
{
    if (!m_impl || !m_impl->settings)
        return;
    if (Rml::Element* stage = m_impl->settings->GetElementById("stage"))
        stage->SetClass("open", open);
}

void RmlUiHost::rebuildSettings(const RmlSettingsMenu& menu)
{
    if (!m_impl || !m_impl->settings)
        return;
    fillControlList(m_impl->settings, m_impl->settings->GetElementById("game-list"), menu.game);
    fillControlList(m_impl->settings, m_impl->settings->GetElementById("engine-list"), menu.engine);
}

void RmlUiHost::stepSettingsVolume(float deltaPercent)
{
    if (!m_impl || !m_impl->settings)
        return;
    Rml::Element* slider = m_impl->settings->GetElementById("volume");
    if (!slider)
        return;
    float percent = readVolumePercent(slider) + deltaPercent;
    if (percent < 0.0f)
        percent = 0.0f;
    if (percent > 100.0f)
        percent = 100.0f;
    slider->SetAttribute("value", percent);
}

void RmlUiHost::updateSettingsInput(Dark::Input& input, float dt)
{
    if (!m_impl || !m_impl->context || !m_impl->settings)
        return;
    const Rml::Input::KeyIdentifier desired = menuDirection(input);
    int                             volumeDir = 0;
    int                             scrollDir = 0;
    if (desired == Rml::Input::KI_LEFT)
        volumeDir = -1;
    else if (desired == Rml::Input::KI_RIGHT)
        volumeDir = 1;
    else if (desired == Rml::Input::KI_UP)
        scrollDir = -1;
    else if (desired == Rml::Input::KI_DOWN)
        scrollDir = 1;

    if (volumeDir != 0)
        m_impl->scrollAxis.clear();
    else
        m_impl->volumeAxis.clear();

    if (m_impl->volumeAxis.fire(volumeDir, dt))
        stepSettingsVolume(volumeDir < 0 ? -5.0f : 5.0f);
    if (!m_impl->scrollAxis.fire(scrollDir, dt))
        return;

    Rml::Element* list  = m_impl->settings->GetElementById("game-list");
    Rml::Element* hover = m_impl->context->GetHoverElement();
    for (int depth = 0; hover && depth < 12; hover = hover->GetParentNode(), ++depth)
    {
        const Rml::String& id = hover->GetId();
        if (id == "engine-list" || id == "game-list")
        {
            list = hover;
            break;
        }
    }
    if (!list)
        return;
    const float line  = 22.0f * (m_impl->dpi > 0.1f ? m_impl->dpi : 1.0f);
    const float delta = (scrollDir < 0 ? -3.0f : 3.0f) * line;
    list->SetScrollTop(list->GetScrollTop() + delta);
}

RmlUiHost::~RmlUiHost()
{
    if (m_ready || m_impl)
        DE_LOG_ERROR("RmlUiHost destroyed without shutdown(Renderer&)");
    delete m_impl;
    m_impl = nullptr;
}

bool RmlUiHost::init(Dark::Renderer& renderer)
{
    if (m_ready || m_impl)
        shutdown(renderer);
    if (!renderer.device())
    {
        DE_LOG_ERROR(Dark::LogCategory::Render, "RmlUi: renderer has no device");
        return false;
    }

    m_impl = new Impl;
    m_impl->listener.events = &m_impl->events;
    m_impl->render.setRenderer(&renderer);
    if (!m_impl->render.createPipeline(renderer))
    {
        delete m_impl;
        m_impl = nullptr;
        return false;
    }

    Rml::SetRenderInterface(&m_impl->render);
    Rml::SetSystemInterface(&m_impl->system);
    Rml::SetFileInterface(&m_impl->files);
    if (!Rml::Initialise())
    {
        DE_LOG_ERROR("RmlUi: Initialise failed");
        m_impl->render.releaseGpu(renderer);
        delete m_impl;
        m_impl = nullptr;
        return false;
    }
    m_impl->initialised = true;

    const std::string version = Rml::GetVersion();
    DE_LOG_INFO("RmlUi {}", version);
    if (version.rfind("6.3", 0) != 0)
        DE_LOG_ERROR("RmlUi version {} is not 6.3", version);

    const int w = renderer.width() > 0 ? static_cast<int>(renderer.width()) : 1;
    const int h = renderer.height() > 0 ? static_cast<int>(renderer.height()) : 1;
    m_impl->context = Rml::CreateContext("player", Rml::Vector2i(w, h));
    if (!m_impl->context)
    {
        DE_LOG_ERROR("RmlUi: CreateContext failed");
        shutdown(renderer);
        return false;
    }

    if (!Rml::LoadFontFace("fonts/Roboto-Medium.ttf", "Roboto", Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Normal))
        DE_LOG_WARN("RmlUi: font fonts/Roboto-Medium.ttf failed to load");

    loadSettings();
    loadPause();
    loadStealth();
    m_ready = true;
    DE_LOG_INFO("RmlUi ready");
    return true;
}

void RmlUiHost::shutdown(Dark::Renderer& renderer)
{
    if (!m_impl && !m_ready)
        return;
    renderer.waitForGpu();
    if (m_impl)
    {
        m_impl->render.setRenderer(&renderer);
        if (m_impl->initialised)
        {
            Rml::Shutdown();
            m_impl->initialised = false;
        }
        m_impl->context  = nullptr;
        m_impl->document = nullptr;
        m_impl->settings = nullptr;
        m_impl->pause    = nullptr;
        m_impl->stealth  = nullptr;
        m_impl->render.releaseGpu(renderer);
        delete m_impl;
        m_impl = nullptr;
    }
    m_ready      = false;
    m_nav        = false;
    m_mouseOver  = false;
    m_ateCancel  = false;
    m_ateAction  = false;
}

bool RmlUiHost::frontActive() const
{
    return m_ready && m_impl && m_impl->document && m_impl->document->IsVisible();
}

void RmlUiHost::presentFront(const RmlFrontMenu& menu)
{
    if (!m_ready || !m_impl || !m_impl->context)
        return;

    m_impl->events.loading    = menu.loading;
    m_impl->events.selectedId = menu.selectedId;
    if (!menu.show)
    {
        if (!(m_impl->settings && m_impl->settings->IsVisible()))
            m_nav = false;
        if (m_impl->document && m_impl->document->IsVisible())
        {
            setFrontOpen(false);
            m_impl->document->Hide();
        }
        m_impl->armOpen = false;
        return;
    }

    if (!m_impl->document && !m_impl->frontFailed)
    {
        m_impl->listener.events = &m_impl->events;
        m_impl->document        = m_impl->context->LoadDocument("ui/menus/front.rml");
        if (!m_impl->document)
        {
            m_impl->frontFailed = true;
            DE_LOG_WARN("RmlUi: front document failed to load (ui/menus/front.rml)");
            return;
        }
        m_impl->document->AddEventListener("click", &m_impl->listener);
        m_impl->document->AddEventListener("mouseover", &m_impl->listener);
        DE_LOG_INFO("RmlUi front menu ready");
    }
    if (!m_impl->document)
        return;

    const float dpi   = menu.dpi > 0.1f ? menu.dpi : 1.0f;
    const float delta = dpi - m_impl->dpi;
    if (delta > 0.01f || delta < -0.01f)
    {
        m_impl->context->SetDensityIndependentPixelRatio(dpi);
        m_impl->dpi = dpi;
    }

    if (menu.title != m_impl->titleShown)
    {
        if (Rml::Element* title = m_impl->document->GetElementById("title"))
            title->SetInnerRML(escapeRml(menu.title.empty() ? std::string("CRISPER") : menu.title));
        m_impl->titleShown = menu.title;
    }

    if (menu.loading != m_impl->loadingOn)
    {
        Rml::ElementList bodies;
        m_impl->document->GetElementsByTagName(bodies, "body");
        if (!bodies.empty() && bodies[0])
            bodies[0]->SetClass("loading", menu.loading);
        if (Rml::Element* status = m_impl->document->GetElementById("status"))
            status->SetClass("on", menu.loading);
        m_impl->loadingOn = menu.loading;
    }
    if (menu.loadingLabel != m_impl->loadingShown)
    {
        if (Rml::Element* name = m_impl->document->GetElementById("status-name"))
            name->SetInnerRML(escapeRml(menu.loadingLabel));
        m_impl->loadingShown = menu.loadingLabel;
    }

    const std::string key = rowFingerprint(menu);
    const bool        rebuilt = key != m_impl->rowKey;
    if (rebuilt)
    {
        rebuildFront(menu);
        m_impl->rowKey = key;
    }

    const bool wasVisible = m_impl->document->IsVisible();
    if (!wasVisible)
    {
        setFrontOpen(false);
        m_impl->document->Show(Rml::ModalFlag::None, Rml::FocusFlag::None, Rml::ScrollFlag::None);
        m_impl->context->Update();
        m_impl->armOpen        = true;
        m_impl->suppressCancel = true;
    }
    else if (rebuilt && !m_impl->armOpen)
        focusFrontSelection();
    m_nav = true;
}

RmlFrontEvent RmlUiHost::pollFront()
{
    RmlFrontEvent event;
    if (!m_impl)
        return event;
    event.activate   = m_impl->events.activate;
    event.cancel     = m_impl->events.cancel;
    event.focusId    = m_impl->events.focusId;
    event.activateId = m_impl->events.activateId;
    m_impl->events.activate = false;
    m_impl->events.cancel   = false;
    m_impl->events.activateId.clear();
    return event;
}

void RmlUiHost::presentSettings(const RmlSettingsMenu& menu)
{
    if (!m_ready || !m_impl || !m_impl->context)
        return;

    loadSettings();
    m_impl->settingsEvents.loading = menu.loading;
    if (!menu.show)
    {
        if (m_impl->settings && m_impl->settings->IsVisible())
        {
            setSettingsOpen(false);
            m_impl->settings->Hide();
        }
        m_impl->armSettings = false;
        if (!(m_impl->document && m_impl->document->IsVisible()))
            m_nav = false;
        return;
    }
    if (!m_impl->settings)
        return;

    const float dpi   = menu.dpi > 0.1f ? menu.dpi : 1.0f;
    const float delta = dpi - m_impl->dpi;
    if (delta > 0.01f || delta < -0.01f)
    {
        m_impl->context->SetDensityIndependentPixelRatio(dpi);
        m_impl->dpi = dpi;
    }

    if (menu.title != m_impl->settingsTitle)
    {
        if (Rml::Element* title = m_impl->settings->GetElementById("title"))
            title->SetInnerRML(escapeRml(menu.title.empty() ? std::string("CRISPER") : menu.title));
        m_impl->settingsTitle = menu.title;
    }

    setDocumentClass(m_impl->settings, "loading", menu.loading);
    setDocumentClass(m_impl->settings, "no-controls", !menu.controlsLoaded);

    const std::string key = settingsFingerprint(menu);
    if (key != m_impl->settingsKey)
    {
        rebuildSettings(menu);
        m_impl->settingsKey = key;
    }

    if (Rml::Element* slider = m_impl->settings->GetElementById("volume"))
    {
        float want = menu.masterVolume * 100.0f;
        if (want < 0.0f)
            want = 0.0f;
        if (want > 100.0f)
            want = 100.0f;
        const float have = readVolumePercent(slider);
        const float diff = want - have;
        if ((diff < 0.0f ? -diff : diff) > 0.75f)
        {
            m_impl->applyingVolume = true;
            slider->SetAttribute("value", want);
            m_impl->applyingVolume = false;
        }
        writeVolumeReadout(m_impl->settings, readVolumePercent(slider), &m_impl->volumeShown);
    }

    if (!m_impl->settings->IsVisible())
    {
        setSettingsOpen(false);
        m_impl->settings->Show(Rml::ModalFlag::None, Rml::FocusFlag::None, Rml::ScrollFlag::None);
        m_impl->context->Update();
        m_impl->armSettings    = true;
        m_impl->suppressCancel = true;
    }
    m_nav = true;
}

bool RmlUiHost::settingsActive() const
{
    return m_ready && m_impl && m_impl->settings && m_impl->settings->IsVisible();
}

RmlSettingsEvent RmlUiHost::pollSettings()
{
    RmlSettingsEvent event;
    if (!m_impl)
        return event;
    event.back          = m_impl->settingsEvents.back;
    event.cancel        = m_impl->settingsEvents.cancel;
    event.volumeChanged = m_impl->settingsEvents.volumeChanged;
    event.masterVolume  = m_impl->settingsEvents.masterVolume;
    m_impl->settingsEvents.back          = false;
    m_impl->settingsEvents.cancel        = false;
    m_impl->settingsEvents.volumeChanged = false;
    return event;
}

void RmlUiHost::presentPause(const RmlPauseMenu& menu)
{
    if (!m_ready || !m_impl || !m_impl->context)
        return;

    loadPause();
    if (!menu.show)
    {
        if (m_impl->pause && m_impl->pause->IsVisible())
        {
            setPauseOpen(false);
            // Hide leaves the modal flag set. A hidden modal document still blocks focus on the front page.
            m_impl->pause->Show(Rml::ModalFlag::None, Rml::FocusFlag::None, Rml::ScrollFlag::None);
            m_impl->pause->Hide();
        }
        m_impl->armPause = false;
        return;
    }
    if (!m_impl->pause)
        return;

    const float dpi   = menu.dpi > 0.1f ? menu.dpi : 1.0f;
    const float delta = dpi - m_impl->dpi;
    if (delta > 0.01f || delta < -0.01f)
    {
        m_impl->context->SetDensityIndependentPixelRatio(dpi);
        m_impl->dpi = dpi;
    }

    if (!m_impl->pause->IsVisible())
    {
        setPauseOpen(false);
        m_impl->pause->Show(Rml::ModalFlag::Modal, Rml::FocusFlag::None, Rml::ScrollFlag::None);
        m_impl->context->Update();
        m_impl->armPause = true;
    }
}

bool RmlUiHost::pauseActive() const
{
    return m_ready && m_impl && m_impl->pause && m_impl->pause->IsVisible();
}

RmlPauseEvent RmlUiHost::pollPause()
{
    RmlPauseEvent event;
    if (!m_impl)
        return event;
    event.resume = m_impl->pauseEvents.resume;
    event.step   = m_impl->pauseEvents.step;
    m_impl->pauseEvents.resume = false;
    m_impl->pauseEvents.step   = false;
    return event;
}

void RmlUiHost::presentStealth(const RmlStealthHud& hud)
{
    if (!m_ready || !m_impl || !m_impl->context)
        return;

    loadStealth();
    if (!hud.show)
    {
        if (m_impl->stealth && m_impl->stealth->IsVisible())
        {
            setStealthOpen(false);
            m_impl->stealth->Hide();
        }
        m_impl->armStealth = false;
        return;
    }
    if (!m_impl->stealth)
        return;

    const float dpi   = hud.dpi > 0.1f ? hud.dpi : 1.0f;
    const float delta = dpi - m_impl->dpi;
    if (delta > 0.01f || delta < -0.01f)
    {
        m_impl->context->SetDensityIndependentPixelRatio(dpi);
        m_impl->dpi = dpi;
    }

    float suspicion = hud.suspicion;
    if (suspicion < 0.0f)
        suspicion = 0.0f;
    if (suspicion > 1.0f)
        suspicion = 1.0f;
    m_impl->hud.suspicion    = suspicion;
    m_impl->hud.nearbyAlerts = hud.nearbyAlerts < 0 ? 0 : hud.nearbyAlerts;
    m_impl->hud.hidden       = hud.hidden;
    m_impl->hud.prompt       = hud.prompt;
    dirtyStealth();

    if (!m_impl->stealth->IsVisible())
    {
        setStealthOpen(false);
        m_impl->stealth->Show(Rml::ModalFlag::None, Rml::FocusFlag::None, Rml::ScrollFlag::None);
        m_impl->context->Update();
        m_impl->armStealth = true;
        return;
    }
    m_impl->context->Update();
}

bool RmlUiHost::stealthActive() const
{
    return m_ready && m_impl && m_impl->stealth && m_impl->stealth->IsVisible();
}

void RmlUiHost::update(Dark::Renderer& renderer, Dark::Input& input, bool imguiMouse, bool imguiKeyboard, float dt)
{
    m_ateCancel = false;
    m_ateAction = false;
    if (!m_ready || !m_impl || !m_impl->context)
        return;

    m_impl->render.setRenderer(&renderer);
    const int w = renderer.width() > 0 ? static_cast<int>(renderer.width()) : 1;
    const int h = renderer.height() > 0 ? static_cast<int>(renderer.height()) : 1;
    const Rml::Vector2i dims = m_impl->context->GetDimensions();
    if (dims.x != w || dims.y != h)
        m_impl->context->SetDimensions(Rml::Vector2i(w, h));

    const bool front    = m_impl->document && m_impl->document->IsVisible();
    const bool settings = m_impl->settings && m_impl->settings->IsVisible();
    const bool pause    = m_impl->pause && m_impl->pause->IsVisible();
    const bool stealth  = m_impl->stealth && m_impl->stealth->IsVisible();
    const bool loading  = settings ? m_impl->settingsEvents.loading : m_impl->events.loading;
    const int  mods     = keyModifiers(input);
    m_impl->events.hoverId.clear();
    m_impl->pauseEvents.hoverId.clear();
    m_nav = front || settings;

    if (imguiMouse)
    {
        if (m_mouseOver)
            m_impl->context->ProcessMouseLeave();
        m_mouseOver = false;
    }
    else
    {
        const bool outside = m_impl->context->ProcessMouseMove(input.mouseX(), input.mouseY(), mods);
        const bool menu    = front || settings;
        m_mouseOver        = menu && !outside;
        if ((menu && !loading) || pause)
        {
            const Dark::MouseButton buttons[] = { Dark::MouseButton::Left, Dark::MouseButton::Right, Dark::MouseButton::Middle };
            for (int i = 0; i < 3; ++i)
            {
                if (input.mousePressed(buttons[i]))
                    m_impl->context->ProcessMouseButtonDown(i, mods);
                if (input.mouseReleased(buttons[i]))
                    m_impl->context->ProcessMouseButtonUp(i, mods);
            }
            const float wheel = input.mouseWheel();
            if (wheel != 0.0f)
                m_impl->context->ProcessMouseWheel(Rml::Vector2f(0.0f, -wheel), mods);
            const bool mouseMoved = input.mouseDeltaX() != 0.0 || input.mouseDeltaY() != 0.0;
            if (front && mouseMoved && !m_impl->events.hoverId.empty())
            {
                if (Rml::Element* hovered = findEntry(m_impl->document, m_impl->events.hoverId))
                {
                    if (m_impl->context->GetFocusElement() != hovered)
                        hovered->Focus(true);
                }
            }
            else if (pause && mouseMoved && !m_impl->pauseEvents.hoverId.empty())
            {
                if (Rml::Element* hovered = findEntry(m_impl->pause, m_impl->pauseEvents.hoverId))
                {
                    if (m_impl->context->GetFocusElement() != hovered)
                        hovered->Focus(true);
                }
            }
        }
    }

    const bool suppress = m_impl->suppressCancel;
    m_impl->suppressCancel = false;
    const bool cancel = (front || settings) && !loading && !suppress && !imguiKeyboard
        && (input.keyPressed(Dark::Key::Escape) || input.buttonPressed(Dark::GamepadButton::B) || input.buttonPressed(Dark::GamepadButton::Back));
    if (m_impl->armOpen && front)
    {
        setFrontOpen(true);
        focusFrontSelection();
        m_impl->armOpen = false;
    }
    if (m_impl->armSettings && settings)
    {
        setSettingsOpen(true);
        if (Rml::Element* volume = m_impl->settings->GetElementById("volume"))
            volume->Focus(true);
        m_impl->armSettings = false;
    }
    if (m_impl->armPause && pause)
    {
        setPauseOpen(true);
        if (Rml::Element* resume = m_impl->pause->GetElementById("resume"))
            resume->Focus(true);
        m_impl->armPause = false;
    }
    if (m_impl->armStealth && stealth)
    {
        setStealthOpen(true);
        m_impl->armStealth = false;
    }

    if (cancel)
    {
        m_impl->direction.clear(m_impl->context, mods);
        m_impl->volumeAxis.clear();
        m_impl->scrollAxis.clear();
        if (settings)
            m_impl->settingsEvents.cancel = true;
        else
            m_impl->events.cancel = true;
        m_ateCancel = true;
    }
    else if (settings && !loading && !imguiKeyboard)
    {
        m_impl->direction.clear(m_impl->context, mods);
        updateSettingsInput(input, dt);
    }
    else if (front && !loading && !imguiKeyboard)
    {
        m_impl->volumeAxis.clear();
        m_impl->scrollAxis.clear();
        const Rml::Input::KeyIdentifier desired = menuDirection(input);
        if (desired != m_impl->direction.key)
            switchFrontColumn(static_cast<int>(desired));
        forwardMenuKeys(m_impl->context, input, mods);
        m_impl->direction.tick(m_impl->context, mods, desired, dt);
    }
    else if (pause && !imguiKeyboard)
    {
        m_impl->volumeAxis.clear();
        m_impl->scrollAxis.clear();
        const Rml::Input::KeyIdentifier desired = pauseDirection(input);
        m_impl->direction.tick(m_impl->context, mods, desired, dt);
        forwardPauseKeys(m_impl->context, input, mods, &m_ateAction);
    }
    else
    {
        m_impl->direction.clear(m_impl->context, mods);
        m_impl->volumeAxis.clear();
        m_impl->scrollAxis.clear();
    }

    m_impl->context->Update();
    if (front && m_impl->document)
    {
        m_impl->events.focusId = entryId(m_impl->context->GetFocusElement());
        rememberFrontFocus();
    }
    else
        m_impl->events.focusId.clear();
}

void RmlUiHost::render(Dark::Renderer& renderer)
{
    if (!m_ready || !m_impl || !m_impl->context)
        return;
    const bool frontOn    = m_impl->document && m_impl->document->IsVisible();
    const bool settingsOn = m_impl->settings && m_impl->settings->IsVisible();
    const bool pauseOn    = m_impl->pause && m_impl->pause->IsVisible();
    const bool stealthOn  = m_impl->stealth && m_impl->stealth->IsVisible();
    if (!frontOn && !settingsOn && !pauseOn && !stealthOn)
        return;
    m_impl->render.setRenderer(&renderer);
    m_impl->render.begin(renderer);
    {
        const Dark::GpuScope scope(renderer.commandList(), "RmlUi", Dark::ProfileColor::Hud);
        m_impl->context->Render();
    }
    m_impl->render.end();
}
