#include "Render/LoadingScreen.h"
#include "Render/Renderer.h"
#include "Render/ShaderCompile.h"
#include "Core/Application.h"
#include "Core/Log.h"

#include <d3dcompiler.h>

#include <chrono>
#include <filesystem>
#include <vector>

#include "Render/LoadingScreenFont.inl"

namespace Dark
{
    namespace
    {

        // Embedded copy of content/shaders/LoadingScreen.hlsl so create() does not need content/.
        static const char kLoadingScreenHlsl[] = R"HLSL(
#pragma pack_matrix(row_major)

cbuffer LoadingScreenConstants : register(b0)
{
    float  timeSec;
    float  fade;
    float  phase;
    float  reducedMotion;
    float4 background;
    float3 spinnerColor;
    float  drawPass; // C++ LoadingScreenConstants::pass (HLSL 'pass' is reserved)
    float2 resolution;
    float  logoAspect;
    float  spinnerOpacity;
};

Texture2D    gLogo : register(t0);
Texture2D    gFont : register(t1);
SamplerState gSamp : register(s0);

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD0;
};

PSInput VSMain(uint id : SV_VertexID)
{
    PSInput o;
    float2 uv = float2((id << 1) & 2, id & 2);
    o.position = float4(uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    o.uv       = uv;
    return o;
}

float2 letterboxUv(float2 uv, float2 res, float aspect)
{
    float la = max(aspect, 1.0e-4);
    float sa = res.x / max(res.y, 1.0);
    float2 size = (sa > la) ? float2(la / sa, 1.0) : float2(1.0, sa / la);
    return (uv - 0.5) / size + 0.5;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    if (drawPass > 0.5)
    {
        float g = gFont.Sample(gSamp, input.uv).r;
        return float4(1.0, 1.0, 1.0, g * fade);
    }

    float2 logoUv = letterboxUv(input.uv, resolution, logoAspect);
    float  inLogo = (logoUv.x >= 0.0 && logoUv.x <= 1.0 && logoUv.y >= 0.0 && logoUv.y <= 1.0) ? 1.0 : 0.0;
    float4 logo   = gLogo.Sample(gSamp, saturate(logoUv));
    logo.a *= inLogo;

    float4 color = lerp(background, logo, logo.a * fade);

    float2 d = input.uv - 0.5;
    if (reducedMotion > 0.5)
    {
        float breathe = 0.7 + 0.3 * (0.5 + 0.5 * sin(timeSec * 3.14159265));
        float bar     = saturate(1.0 - abs(d.y) / 0.012) * step(abs(d.x), 0.08);
        color.rgb += spinnerColor * bar * spinnerOpacity * breathe * fade;
    }
    else
    {
        float spin = frac(timeSec * 0.4);
        float ring = saturate(1.0 - abs(length(d) - 0.08) / 0.012);
        float arc  = step(frac(atan2(d.y, d.x) / 6.2831853 + spin), 0.65);
        color.rgb += spinnerColor * ring * arc * spinnerOpacity * fade;
    }

    color.a = 1.0;
    return color;
}
)HLSL";

        bool FailedHr(HRESULT hr, const char* what)
        {
            if (SUCCEEDED(hr))
                return false;
            DE_LOG_ERROR(LogCategory::Render, "{} failed (HRESULT 0x{:08X})", what, static_cast<unsigned>(hr));
            return true;
        }

        bool compileEmbedded(const char* entry, const char* target, ComPtr<ID3DBlob>& outBytecode)
        {
            outBytecode.Reset();

            ComPtr<ID3DBlob> errors;
            UINT             flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
            flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

            const HRESULT hr = D3DCompile(
                kLoadingScreenHlsl,
                sizeof(kLoadingScreenHlsl) - 1,
                "LoadingScreen.hlsl",
                nullptr,
                nullptr,
                entry,
                target,
                flags,
                0,
                &outBytecode,
                &errors);

            if (FAILED(hr))
            {
                const char* msg = errors ? static_cast<const char*>(errors->GetBufferPointer()) : "unknown";
                DE_LOG_ERROR(LogCategory::Render, "LoadingScreen compile failed ({} {}): {}", entry, target, msg);
                outBytecode.Reset();
                return false;
            }
            return true;
        }

        float phaseValue(LoadingPhase phase)
        {
            switch (phase)
            {
            case LoadingPhase::Host:
                return 1.0f;
            case LoadingPhase::FadeOut:
            case LoadingPhase::Done:
                return 2.0f;
            case LoadingPhase::Engine:
            default:
                return 0.0f;
            }
        }

        bool nextCodepoint(const std::string& s, size_t& i, uint32_t& cp)
        {
            if (i >= s.size())
                return false;
            const unsigned char c = static_cast<unsigned char>(s[i]);
            if (c < 0x80)
            {
                cp = c;
                ++i;
                return true;
            }
            if ((c & 0xE0) == 0xC0 && i + 1 < s.size())
            {
                const unsigned char c1 = static_cast<unsigned char>(s[i + 1]);
                cp                     = (static_cast<uint32_t>(c & 0x1F) << 6) | static_cast<uint32_t>(c1 & 0x3F);
                i += 2;
                return true;
            }
            if ((c & 0xF0) == 0xE0 && i + 2 < s.size())
            {
                cp = (static_cast<uint32_t>(c & 0x0F) << 12)
                    | (static_cast<uint32_t>(static_cast<unsigned char>(s[i + 1]) & 0x3F) << 6)
                    | static_cast<uint32_t>(static_cast<unsigned char>(s[i + 2]) & 0x3F);
                i += 3;
                return true;
            }
            if ((c & 0xF8) == 0xF0 && i + 3 < s.size())
            {
                cp = '?';
                i += 4;
                return true;
            }
            cp = '?';
            ++i;
            return true;
        }

        int glyphIndex(uint32_t cp)
        {
            if (cp >= 0x20 && cp <= 0x7E)
                return static_cast<int>(cp - 0x20);
            if (cp == 0xA9)
                return 95;
            if (cp == 0xB7)
                return 96;
            return static_cast<int>('?' - 0x20);
        }

        bool rasterizeVersionLine(const std::string& text, std::vector<uint8_t>& rgba, uint32_t& outW, uint32_t& outH)
        {
            std::vector<int> indices;
            size_t           i  = 0;
            uint32_t         cp = 0;
            while (nextCodepoint(text, i, cp))
                indices.push_back(glyphIndex(cp));
            if (indices.empty())
                return false;

            constexpr int kPad = 1;
            const uint32_t w   = static_cast<uint32_t>(indices.size() * (kGlyphW + kPad) + kPad);
            const uint32_t h   = static_cast<uint32_t>(kGlyphH);
            rgba.assign(static_cast<size_t>(w) * static_cast<size_t>(h) * 4u, 0);

            for (size_t n = 0; n < indices.size(); ++n)
            {
                const int gi = indices[n];
                if (gi < 0 || gi >= kGlyphCount)
                    continue;
                const int ox = static_cast<int>(n) * (kGlyphW + kPad) + kPad;
                for (int row = 0; row < kGlyphH; ++row)
                {
                    const uint8_t bits = kGlyphs[gi][row];
                    for (int col = 0; col < kGlyphW; ++col)
                    {
                        if ((bits & static_cast<uint8_t>(0x80 >> col)) == 0)
                            continue;
                        const size_t p = (static_cast<size_t>(row) * w + static_cast<size_t>(ox + col)) * 4u;
                        rgba[p + 0]    = 255;
                        rgba[p + 1]    = 255;
                        rgba[p + 2]    = 255;
                        rgba[p + 3]    = 255;
                    }
                }
            }
            outW = w;
            outH = h;
            return true;
        }

        bool createSplashPso(ID3D12Device* device, ID3D12RootSignature* rs, ID3DBlob* vs, ID3DBlob* ps, ComPtr<ID3D12PipelineState>& outPso)
        {
            outPso.Reset();
            if (!device || !rs || !vs || !ps)
                return false;

            D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
            pso.pRootSignature = rs;
            pso.VS             = { vs->GetBufferPointer(), vs->GetBufferSize() };
            pso.PS             = { ps->GetBufferPointer(), ps->GetBufferSize() };
            pso.SampleMask     = UINT_MAX;

            D3D12_RENDER_TARGET_BLEND_DESC& rt = pso.BlendState.RenderTarget[0];
            rt.BlendEnable                     = TRUE;
            rt.RenderTargetWriteMask           = D3D12_COLOR_WRITE_ENABLE_ALL;
            rt.SrcBlend                        = D3D12_BLEND_SRC_ALPHA;
            rt.DestBlend                       = D3D12_BLEND_INV_SRC_ALPHA;
            rt.BlendOp                         = D3D12_BLEND_OP_ADD;
            rt.SrcBlendAlpha                   = D3D12_BLEND_ONE;
            rt.DestBlendAlpha                  = D3D12_BLEND_INV_SRC_ALPHA;
            rt.BlendOpAlpha                    = D3D12_BLEND_OP_ADD;

            pso.RasterizerState.FillMode        = D3D12_FILL_MODE_SOLID;
            pso.RasterizerState.CullMode        = D3D12_CULL_MODE_NONE;
            pso.RasterizerState.DepthClipEnable = TRUE;

            pso.DepthStencilState.DepthEnable   = FALSE;
            pso.DepthStencilState.StencilEnable = FALSE;

            pso.InputLayout           = { nullptr, 0 };
            pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            pso.NumRenderTargets      = 1;
            pso.RTVFormats[0]         = DXGI_FORMAT_R8G8B8A8_UNORM;
            pso.DSVFormat             = DXGI_FORMAT_UNKNOWN;
            pso.SampleDesc            = { 1, 0 };

            if (FailedHr(device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&outPso)), "CreateGraphicsPipelineState (LoadingScreen)"))
            {
                outPso.Reset();
                return false;
            }
            return true;
        }

        bool loadSplashLogo(Renderer& renderer, const std::string& virtualPath, Texture2D& dest, const wchar_t* resourceName)
        {
            if (virtualPath.empty())
                return false;

            std::filesystem::path path;
            if (!resolveSplashAsset(virtualPath, path))
            {
                DE_LOG_WARN(LogCategory::Render, "LoadingScreen: logo path rejected or missing '{}'", virtualPath);
                return false;
            }

            Texture2D tex;
            if (!tex.createFromFile(renderer, path))
                return false;

            if (tex.width() > 1024 || tex.height() > 1024)
            {
                DE_LOG_WARN(LogCategory::Render, "LoadingScreen: logo '{}' is {}x{} (cap 1024), using 1x1", path.string(), tex.width(), tex.height());
                return false;
            }

            dest = std::move(tex);
            if (dest.resource() && resourceName)
                dest.resource()->SetName(resourceName);
            return true;
        }

    } // namespace

    bool LoadingScreen::create(Renderer& renderer)
    {
        shutdown(renderer);

        if (!renderer.isValid() || !renderer.device())
        {
            DE_LOG_ERROR(LogCategory::Render, "LoadingScreen::create: renderer not initialized");
            return false;
        }

        ID3D12Device* device = renderer.device();

        D3D12_DESCRIPTOR_RANGE srvRange{};
        srvRange.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRange.NumDescriptors                    = 2;
        srvRange.BaseShaderRegister                = 0;
        srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_ROOT_PARAMETER params[2]{};
        params[kRootConstants].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[kRootConstants].ShaderVisibility         = D3D12_SHADER_VISIBILITY_ALL;
        params[kRootConstants].Constants.ShaderRegister = 0;
        params[kRootConstants].Constants.Num32BitValues = 16;

        params[kRootSrv].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[kRootSrv].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;
        params[kRootSrv].DescriptorTable.NumDescriptorRanges = 1;
        params[kRootSrv].DescriptorTable.pDescriptorRanges   = &srvRange;

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

        ComPtr<ID3DBlob> rsBlob;
        ComPtr<ID3DBlob> rsErr;
        if (FailedHr(
                D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &rsErr),
                "D3D12SerializeRootSignature (LoadingScreen)"))
        {
            if (rsErr)
                DE_LOG_ERROR(LogCategory::Render, "LoadingScreen RS: {}", static_cast<const char*>(rsErr->GetBufferPointer()));
            return false;
        }
        if (FailedHr(
                device->CreateRootSignature(
                    0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)),
                "CreateRootSignature (LoadingScreen)"))
        {
            return false;
        }
        m_rootSignature->SetName(L"LoadingScreen.RS");

        ComPtr<ID3DBlob> vs;
        ComPtr<ID3DBlob> ps;
        if (!compileEmbedded("VSMain", "vs_5_0", vs) || !compileEmbedded("PSMain", "ps_5_0", ps))
        {
            shutdown(renderer);
            return false;
        }
        if (!createSplashPso(device, m_rootSignature.Get(), vs.Get(), ps.Get(), m_pso))
        {
            shutdown(renderer);
            return false;
        }
        m_pso->SetName(L"LoadingScreen.PSO");

        D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
        heapDesc.NumDescriptors = kSrvPerFrame * Renderer::kFrameCount;
        heapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FailedHr(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_srvHeap)), "CreateDescriptorHeap (LoadingScreen)"))
        {
            shutdown(renderer);
            return false;
        }
        m_srvHeap->SetName(L"LoadingScreen.Heap");
        m_cpu     = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
        m_gpu     = m_srvHeap->GetGPUDescriptorHandleForHeapStart();
        m_srvIncr = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        const uint8_t logoPx[4] = { 255, 255, 255, 0 }; // transparent: fallback is bg + spinner
        const uint8_t fontPx[4] = { 0, 0, 0, 0 };       // .r = 0 until version atlas is blitted
        if (!m_engineLogo.createFromRGBA(renderer, logoPx, 1, 1, 4) || !m_hostLogo.createFromRGBA(renderer, logoPx, 1, 1, 4)
            || !m_font.createFromRGBA(renderer, fontPx, 1, 1, 4))
        {
            DE_LOG_ERROR(LogCategory::Render, "LoadingScreen::create: 1x1 fallback textures failed");
            shutdown(renderer);
            return false;
        }
        if (m_engineLogo.resource())
            m_engineLogo.resource()->SetName(L"LoadingScreen.EngineLogo");
        if (m_hostLogo.resource())
            m_hostLogo.resource()->SetName(L"LoadingScreen.HostLogo");
        if (m_font.resource())
            m_font.resource()->SetName(L"LoadingScreen.Font");

        DE_LOG_INFO(LogCategory::Render, "LoadingScreen: ready (embedded PSO, 1x1 fallbacks)");
        return true;
    }

    void LoadingScreen::draw(Renderer& renderer, const LoadingDrawState& state)
    {
        if (!isReady() || !m_srvHeap || !m_engineLogo.valid() || !m_hostLogo.valid() || !m_font.valid())
            return;

        ID3D12Device*              device = renderer.device();
        ID3D12GraphicsCommandList* cmd    = renderer.commandList();
        if (!device || !cmd)
            return;

        const UINT w = renderer.width();
        const UINT h = renderer.height();
        if (w == 0 || h == 0)
            return;

        const UINT heapSize = kSrvPerFrame * Renderer::kFrameCount;
        const UINT frame    = renderer.frameIndex() % Renderer::kFrameCount;
        const UINT base     = frame * kSrvPerFrame;
        if (base + 1 >= heapSize)
            return;

        const Texture2D& logo = (state.phase == LoadingPhase::Engine) ? m_engineLogo : m_hostLogo;

        renderer.bindColorTargetOnly();

        D3D12_CPU_DESCRIPTOR_HANDLE destLogo = m_cpu;
        destLogo.ptr += static_cast<SIZE_T>(base) * m_srvIncr;
        D3D12_CPU_DESCRIPTOR_HANDLE destFont = m_cpu;
        destFont.ptr += static_cast<SIZE_T>(base + 1) * m_srvIncr;
        device->CopyDescriptorsSimple(1, destLogo, logo.cpuHandle(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        device->CopyDescriptorsSimple(1, destFont, m_font.cpuHandle(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        D3D12_GPU_DESCRIPTOR_HANDLE gpu = m_gpu;
        gpu.ptr += static_cast<SIZE_T>(base) * m_srvIncr;

        cmd->SetGraphicsRootSignature(m_rootSignature.Get());
        cmd->SetPipelineState(m_pso.Get());
        ID3D12DescriptorHeap* heaps[] = { m_srvHeap.Get() };
        cmd->SetDescriptorHeaps(1, heaps);
        cmd->SetGraphicsRootDescriptorTable(kRootSrv, gpu);
        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        // "none" is a static bar (same GPU path as reducedMotion); fade-out still runs unless reducedMotion.
        const bool reducedCfg = state.reducedMotion || m_config.reducedMotion;
        const bool noneAnim   = (m_config.animation == "none");
        const bool reduced    = reducedCfg || noneAnim;

        LoadingScreenConstants cb{};
        cb.timeSec         = (noneAnim && !reducedCfg) ? 0.0f : state.timeSec;
        cb.fade            = state.fade;
        cb.phase           = phaseValue(state.phase);
        cb.reducedMotion   = reduced ? 1.0f : 0.0f;
        cb.background[0]   = m_config.background[0];
        cb.background[1]   = m_config.background[1];
        cb.background[2]   = m_config.background[2];
        cb.background[3]   = m_config.background[3];
        cb.spinnerColor[0] = m_config.spinnerColor[0];
        cb.spinnerColor[1] = m_config.spinnerColor[1];
        cb.spinnerColor[2] = m_config.spinnerColor[2];
        cb.resolution[0]   = static_cast<float>(w);
        cb.resolution[1]   = static_cast<float>(h);
        cb.logoAspect      = (logo.height() > 0) ? static_cast<float>(logo.width()) / static_cast<float>(logo.height()) : 1.0f;
        cb.spinnerOpacity  = 1.0f;

        cmd->RSSetViewports(1, &renderer.viewport());
        cmd->RSSetScissorRects(1, &renderer.scissor());

        cb.pass = 0.0f;
        cmd->SetGraphicsRoot32BitConstants(kRootConstants, 16, &cb, 0);
        cmd->DrawInstanced(3, 1, 0, 0);

        const UINT fw = m_font.width();
        const UINT fh = m_font.height();
        if (fw > 1 && fh > 0)
        {
            LONG lineH = static_cast<LONG>(h) / 90;
            if (lineH < 14)
                lineH = 14;
            if (lineH > 48)
                lineH = 48;
            const LONG pad = lineH;
            LONG       tw  = static_cast<LONG>(static_cast<float>(lineH) * static_cast<float>(fw) / static_cast<float>(fh));
            const LONG maxW = static_cast<LONG>(w) - pad * 2;
            if (tw > maxW)
                tw = maxW;

            const std::string& anchor = m_config.versionText.anchor;
            const bool         top    = (anchor == "top-left" || anchor == "top-right");
            const bool         right  = (anchor == "bottom-right" || anchor == "top-right");
            const LONG         x      = right ? (static_cast<LONG>(w) - pad - tw) : pad;
            const LONG         y      = top ? pad : (static_cast<LONG>(h) - pad - lineH);
            if (tw > 0 && lineH > 0 && x >= 0 && y >= 0)
            {
                D3D12_VIEWPORT vp{};
                vp.TopLeftX = static_cast<float>(x);
                vp.TopLeftY = static_cast<float>(y);
                vp.Width    = static_cast<float>(tw);
                vp.Height   = static_cast<float>(lineH);
                vp.MinDepth = 0.0f;
                vp.MaxDepth = 1.0f;
                D3D12_RECT sc{ x, y, x + tw, y + lineH };
                cmd->RSSetViewports(1, &vp);
                cmd->RSSetScissorRects(1, &sc);

                cb.pass = 1.0f;
                cmd->SetGraphicsRoot32BitConstants(kRootConstants, 16, &cb, 0);
                cmd->DrawInstanced(3, 1, 0, 0);
            }
        }

        cmd->RSSetViewports(1, &renderer.viewport());
        cmd->RSSetScissorRects(1, &renderer.scissor());
    }

    void LoadingScreen::shutdown(Renderer& renderer)
    {
        if (renderer.device() && renderer.queue())
            renderer.waitForGpu();

        m_pso.Reset();
        m_rootSignature.Reset();
        m_srvHeap.Reset();
        m_engineLogo = Texture2D{};
        m_hostLogo   = Texture2D{};
        m_font       = Texture2D{};
        m_cpu          = {};
        m_gpu          = {};
        m_srvIncr      = 0;
        m_triedAssets  = false;
    }

    bool LoadingScreen::tryLoadConfig(const AppConfig& cfg)
    {
        loadLoadingScreenConfig(cfg, m_config);
        m_versionLine = makeLoadingVersionLine(m_config, cfg.hostName, cfg.hostVersion);
        return true;
    }

    void LoadingScreen::setPhase(LoadingPhase phase)
    {
        m_phase      = phase;
        m_phaseStart = std::chrono::steady_clock::now();
        m_skipDwell  = false;
    }

    void LoadingScreen::skipCurrentPhaseDwell()
    {
        m_skipDwell = true;
    }

    float LoadingScreen::remainingDwell() const
    {
        if (m_skipDwell)
            return 0.0f;

        float minSeconds = 0.0f;
        switch (m_phase)
        {
        case LoadingPhase::Engine:
            minSeconds = m_config.engine.minSeconds;
            break;
        case LoadingPhase::Host:
            minSeconds = m_config.host.minSeconds;
            break;
        default:
            return 0.0f;
        }

        const float elapsed = std::chrono::duration<float>(std::chrono::steady_clock::now() - m_phaseStart).count();
        const float rem     = minSeconds - elapsed;
        return rem > 0.0f ? rem : 0.0f;
    }

    void LoadingScreen::tryLoadAssets(Renderer& renderer)
    {
        if (m_triedAssets || !isReady() || !renderer.isValid() || !renderer.device())
            return;
        m_triedAssets = true;

        renderer.waitForGpu();

        loadSplashLogo(renderer, m_config.engine.image, m_engineLogo, L"LoadingScreen.EngineLogo");
        loadSplashLogo(renderer, m_config.host.image, m_hostLogo, L"LoadingScreen.HostLogo");

        if (m_versionLine.empty())
            m_versionLine = makeLoadingVersionLine(m_config, nullptr, nullptr);

        if (!m_versionLine.empty())
        {
            std::vector<uint8_t> rgba;
            uint32_t             fw = 0;
            uint32_t             fh = 0;
            if (rasterizeVersionLine(m_versionLine, rgba, fw, fh))
            {
                Texture2D font;
                if (font.createFromRGBA(renderer, rgba.data(), fw, fh, fw * 4u))
                {
                    m_font = std::move(font);
                    if (m_font.resource())
                        m_font.resource()->SetName(L"LoadingScreen.Font");
                }
            }
        }

        std::filesystem::path hlsl;
        if (resolveSplashAsset("shaders/LoadingScreen.hlsl", hlsl))
        {
            ComPtr<ID3DBlob> vs;
            ComPtr<ID3DBlob> ps;
            if (compileShaderFromFile(hlsl, "VSMain", "vs_5_0", vs) && compileShaderFromFile(hlsl, "PSMain", "ps_5_0", ps))
            {
                renderer.waitForGpu();
                ComPtr<ID3D12PipelineState> diskPso;
                if (createSplashPso(renderer.device(), m_rootSignature.Get(), vs.Get(), ps.Get(), diskPso))
                {
                    m_pso = std::move(diskPso);
                    m_pso->SetName(L"LoadingScreen.PSO");
                    DE_LOG_INFO(LogCategory::Render, "LoadingScreen: replaced embedded PSO from '{}'", hlsl.string());
                }
            }
        }
    }

} // namespace Dark
