#include "Ui/ImGuiHost.h"
#include "Ui/ImGuiTheme.h"
#include "Ui/Icons.h"
#include "Core/Window.h"
#include "Core/Log.h"
#include "Core/ContentRoots.h"
#include "Render/Profile.h"
#include "Render/Renderer.h"
#include "Render/Texture2D.h"

#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx12.h>

#include <d3d12.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <vector>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

using Microsoft::WRL::ComPtr;

struct ImGuiHost::Impl
{
    ComPtr<ID3D12DescriptorHeap> srvHeap;
    UINT                         srvDescriptorSize = 0;
    UINT                         nextSrvSlot       = 1; // slot 0 = font atlas
    ImGuiStyle                   baseStyle{};
    float                        dpiScale  = 1.0f;
    Dark::Window*                window    = nullptr;
    Dark::Renderer*              renderer  = nullptr;
};

namespace
{
    constexpr UINT kSrvHeapSize = 64; // slot 0 = font atlas; 1.. = addTexture()

    bool MessageHook(void* hwnd, unsigned msg, unsigned long long wParam, long long lParam, void*)
    {
        if (ImGui_ImplWin32_WndProcHandler(static_cast<HWND>(hwnd), static_cast<UINT>(msg), static_cast<WPARAM>(wParam), static_cast<LPARAM>(lParam)))
            return true;
        return false;
    }

    std::filesystem::path findContentFile(const char* relative)
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        for (const fs::path& root : Dark::contentRootCandidates())
        {
            const fs::path p = root / relative;
            if (fs::is_regular_file(p, ec))
                return p;
        }
        return {};
    }

    void appendUtf8Codepoint(const char* utf8, std::vector<ImWchar>& out)
    {
        if (!utf8)
            return;
        const auto* p = reinterpret_cast<const unsigned char*>(utf8);
        while (*p)
        {
            ImWchar cp = 0;
            if ((*p & 0x80u) == 0)
            {
                cp = *p++;
            }
            else if ((*p & 0xE0u) == 0xC0u && p[1])
            {
                cp = static_cast<ImWchar>(((*p & 0x1Fu) << 6) | (p[1] & 0x3Fu));
                p += 2;
            }
            else if ((*p & 0xF0u) == 0xE0u && p[1] && p[2])
            {
                cp = static_cast<ImWchar>(((*p & 0x0Fu) << 12) | ((p[1] & 0x3Fu) << 6) | (p[2] & 0x3Fu));
                p += 3;
            }
            else
            {
                ++p;
                continue;
            }
            if (cp != 0)
                out.push_back(cp);
        }
    }

    const ImWchar* iconGlyphRanges()
    {
        static std::vector<ImWchar> ranges;
        if (!ranges.empty())
            return ranges.data();

        std::vector<ImWchar> codepoints;
#define DE_UI_PUSH_ICON(icon) appendUtf8Codepoint(icon, codepoints);
        DE_UI_ICON_STRINGS(DE_UI_PUSH_ICON)
#undef DE_UI_PUSH_ICON
        std::sort(codepoints.begin(), codepoints.end());
        codepoints.erase(std::unique(codepoints.begin(), codepoints.end()), codepoints.end());

        for (ImWchar cp : codepoints)
        {
            if (!ranges.empty() && cp == static_cast<ImWchar>(ranges.back() + 1))
                ranges.back() = cp;
            else
            {
                ranges.push_back(cp);
                ranges.push_back(cp);
            }
        }
        ranges.push_back(0);
        return ranges.data();
    }

    ImFontConfig fontConfig()
    {
        ImFontConfig cfg{};
#ifdef IMGUI_ENABLE_FREETYPE
        cfg.PixelSnapH = false;
#else
        cfg.OversampleH = 2;
        cfg.OversampleV = 1;
        cfg.PixelSnapH  = true;
#endif
        return cfg;
    }

    void mergeIconFont(ImGuiIO& io, float size)
    {
        const std::filesystem::path icons = findContentFile("fonts/fa-solid-900.ttf");
        if (icons.empty())
        {
            DE_LOG_WARN("ImGuiHost: icon font missing (content/fonts/fa-solid-900.ttf)");
            return;
        }
        ImFontConfig ic = fontConfig();
        ic.MergeMode     = true;
        ic.GlyphOffset.y = 0.5f;
        io.Fonts->AddFontFromFileTTF(icons.string().c_str(), size * 0.88f, &ic, iconGlyphRanges());
    }

    ImFont* addFace(ImGuiIO& io, const char* contentRelative, const char* windowsFallback, float size)
    {
        ImFontConfig cfg = fontConfig();
        ImFont*      font = nullptr;
        if (contentRelative)
        {
            const std::filesystem::path face = findContentFile(contentRelative);
            if (!face.empty())
                font = io.Fonts->AddFontFromFileTTF(face.string().c_str(), size, &cfg);
        }
        if (!font && windowsFallback)
            font = io.Fonts->AddFontFromFileTTF(windowsFallback, size, &cfg);
        return font;
    }

    UiFonts loadUiFonts(ImGuiIO& io, float dpiScale)
    {
        const float dpi  = dpiScale > 0.1f ? dpiScale : 1.0f;
        const float body = kUiFontBodyPx * dpi;
        UiFonts     fonts{};

        fonts.body = addFace(io, "fonts/Roboto-Medium.ttf", "C:\\Windows\\Fonts\\segoeui.ttf", body);
        if (!fonts.body)
        {
            ImFontConfig def = fontConfig();
            def.SizePixels   = 13.0f * dpi;
            fonts.body       = io.Fonts->AddFontDefault(&def);
            fonts.heading    = fonts.body;
            fonts.mono       = fonts.body;
            DE_LOG_WARN("ImGuiHost: using ProggyClean (no UI TTF found)");
            return fonts;
        }
        mergeIconFont(io, body);
        io.FontDefault = fonts.body;

#ifdef IMGUI_ENABLE_FREETYPE
        const float heading = kUiFontHeadingPx * dpi;
        const float mono    = kUiFontMonoPx * dpi;
        fonts.heading       = addFace(io, "fonts/Roboto-Bold.ttf", nullptr, heading);
        if (fonts.heading)
            mergeIconFont(io, heading);
        else
        {
            fonts.heading = fonts.body;
            DE_LOG_WARN("ImGuiHost: Roboto-Bold missing, heading uses body");
        }
        fonts.mono = addFace(io, "fonts/Cousine-Regular.ttf", "C:\\Windows\\Fonts\\consola.ttf", mono);
        if (!fonts.mono)
        {
            fonts.mono = fonts.body;
            DE_LOG_WARN("ImGuiHost: mono face missing, mono uses body");
        }
#else
        fonts.heading = fonts.body;
        fonts.mono    = fonts.body;
#endif
#ifdef IMGUI_ENABLE_FREETYPE
        const float logHeading = heading;
        const float logMono    = mono;
#else
        const float logHeading = body;
        const float logMono    = body;
#endif
        DE_LOG_INFO("ImGuiHost: UI font body {:.1f}px + heading {:.1f}px + mono {:.1f}px + icons", body, logHeading, logMono);
        return fonts;
    }

} // namespace

uint64_t ImGuiHost::addTexture(Dark::Renderer& renderer, const Dark::Texture2D& texture)
{
    if (!m_ready || !m_impl || !m_impl->srvHeap || !renderer.device())
    {
        DE_LOG_WARN("ImGuiHost: addTexture before init");
        return 0;
    }
    if (!texture.valid())
    {
        DE_LOG_WARN("ImGuiHost: addTexture with invalid texture");
        return 0;
    }
    if (m_impl->nextSrvSlot >= kSrvHeapSize)
    {
        DE_LOG_WARN("ImGuiHost: SRV heap full ({} slots)", kSrvHeapSize);
        return 0;
    }

    const UINT slot   = m_impl->nextSrvSlot;
    const UINT offset = slot * m_impl->srvDescriptorSize; // each slot is srvDescriptorSize bytes

    D3D12_CPU_DESCRIPTOR_HANDLE dstCpu = m_impl->srvHeap->GetCPUDescriptorHandleForHeapStart();
    dstCpu.ptr += offset;
    D3D12_GPU_DESCRIPTOR_HANDLE dstGpu = m_impl->srvHeap->GetGPUDescriptorHandleForHeapStart();
    dstGpu.ptr += offset;

    renderer.device()->CopyDescriptorsSimple(1, dstCpu, texture.cpuHandle(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    ++m_impl->nextSrvSlot;
    return static_cast<uint64_t>(dstGpu.ptr);
}

bool ImGuiHost::createSrvHeap(Dark::Renderer& renderer)
{
    D3D12_DESCRIPTOR_HEAP_DESC desc{};
    desc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.NumDescriptors = kSrvHeapSize;
    desc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(renderer.device()->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_impl->srvHeap))))
    {
        DE_LOG_ERROR("ImGuiHost: CreateDescriptorHeap failed");
        return false;
    }
    m_impl->srvDescriptorSize = renderer.device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    return true;
}

bool ImGuiHost::init(Dark::Window& window, Dark::Renderer& renderer, const char* iniFilename, bool docking, Dark::UiAccent accent, bool viewports)
{
    if (m_ready)
        return true;

    m_impl = new Impl();
    m_hwnd = window.nativeHandle();
    if (!m_hwnd || !renderer.device())
    {
        DE_LOG_ERROR("ImGuiHost: missing window/device");
        return false;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    if (docking)
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    if (viewports)
    {
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
        m_viewports = true;
    }
    io.ConfigWindowsMoveFromTitleBarOnly = true;
    io.IniFilename                       = iniFilename ? iniFilename : "imgui.ini";

    ImGuiStyle& style = ImGui::GetStyle();
    Dark::applyImGuiTheme(accent, &style);
    if (m_viewports)
    {
        style.WindowRounding              = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    m_impl->baseStyle = style;
    m_impl->window    = &window;
    m_impl->renderer  = &renderer;
    const float dpiScale = window.dpiScale();
    m_impl->dpiScale     = dpiScale > 0.1f ? dpiScale : 1.0f;
    if (m_impl->dpiScale > 1.01f)
        style.ScaleAllSizes(m_impl->dpiScale);
    m_fonts = loadUiFonts(io, m_impl->dpiScale);
    DE_LOG_INFO("ImGuiHost: dpi scale {:.2f}", m_impl->dpiScale);

    if (!ImGui_ImplWin32_Init(m_hwnd))
    {
        DE_LOG_ERROR("ImGuiHost: Win32 init failed");
        return false;
    }

    if (!createSrvHeap(renderer))
        return false;

    if (!renderer.queue())
    {
        DE_LOG_ERROR("ImGuiHost: renderer has no command queue");
        return false;
    }

    ImGui_ImplDX12_InitInfo initInfo{};
    initInfo.Device                       = renderer.device();
    initInfo.CommandQueue                 = renderer.queue();
    initInfo.NumFramesInFlight            = static_cast<int>(Dark::Renderer::kFrameCount);
    initInfo.RTVFormat                    = DXGI_FORMAT_R8G8B8A8_UNORM;
    initInfo.DSVFormat                    = DXGI_FORMAT_D32_FLOAT;
    initInfo.SrvDescriptorHeap            = m_impl->srvHeap.Get();
    initInfo.LegacySingleSrvCpuDescriptor = m_impl->srvHeap->GetCPUDescriptorHandleForHeapStart();
    initInfo.LegacySingleSrvGpuDescriptor = m_impl->srvHeap->GetGPUDescriptorHandleForHeapStart();
    // Pin slot 0. The legacy one-shot free flag asserts on the second atlas rebuild.
    initInfo.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* outCpu, D3D12_GPU_DESCRIPTOR_HANDLE* outGpu) {
        *outCpu = info->LegacySingleSrvCpuDescriptor;
        *outGpu = info->LegacySingleSrvGpuDescriptor;
    };
    initInfo.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE) {};

    if (!ImGui_ImplDX12_Init(&initInfo))
    {
        DE_LOG_ERROR("ImGuiHost: DX12 init failed");
        return false;
    }

    window.setMessageHook(&MessageHook, this);
    window.takeDpiChanged();
    m_ready = true;
    DE_LOG_INFO("ImGuiHost: ready (docking={} viewports={})", docking, m_viewports);
    return true;
}

void ImGuiHost::shutdown(Dark::Renderer& renderer)
{
    if (!m_ready)
        return;

    renderer.waitForGpu();
    if (m_viewports)
        ImGui::DestroyPlatformWindows();
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    if (m_impl)
    {
        m_impl->srvHeap.Reset();
        delete m_impl;
        m_impl = nullptr;
    }
    m_viewports = false;
    m_ready     = false;
}

void ImGuiHost::syncDpi()
{
    if (!m_impl || !m_impl->window || !m_impl->renderer)
        return;
    if (!m_impl->window->takeDpiChanged())
        return;
    const float next = m_impl->window->dpiScale();
    if (next < 0.1f || std::fabs(next - m_impl->dpiScale) < 0.005f)
        return;

    const float prev = m_impl->dpiScale;
    m_impl->dpiScale = next;

    ImGuiStyle& style = ImGui::GetStyle();
    style             = m_impl->baseStyle;
    if (next > 1.01f)
        style.ScaleAllSizes(next);

    m_impl->renderer->waitForGpu();
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    m_fonts = loadUiFonts(io, next);
    ImGui_ImplDX12_InvalidateDeviceObjects();
    if (!ImGui_ImplDX12_CreateDeviceObjects())
        DE_LOG_ERROR(Dark::LogCategory::Render, "ImGuiHost: font rebuild failed");
    DE_LOG_INFO("ImGuiHost: dpi scale {:.2f} -> {:.2f}, fonts rebuilt", prev, next);
}

void ImGuiHost::beginFrame()
{
    if (!m_ready)
        return;
    syncDpi();
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

void ImGuiHost::render(Dark::Renderer& renderer)
{
    if (!m_ready)
        return;

    Dark::drawImGuiDecorations();
    ImGui::Render();
    ID3D12GraphicsCommandList* cmd = renderer.commandList();
    if (!cmd || !m_impl || !m_impl->srvHeap)
        return;

    const Dark::GpuScope imgui(cmd, "ImGui", Dark::ProfileColor::ImGui);
    ID3D12DescriptorHeap* heaps[] = { m_impl->srvHeap.Get() };
    cmd->SetDescriptorHeaps(1, heaps);
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), cmd);

    if (m_viewports)
    {
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
    }
}

void ImGuiHost::endFrame() {}

bool ImGuiHost::wantCaptureMouse() const
{
    if (!m_ready)
        return false;
    return ImGui::GetIO().WantCaptureMouse;
}

bool ImGuiHost::wantCaptureKeyboard() const
{
    if (!m_ready)
        return false;
    return ImGui::GetIO().WantCaptureKeyboard;
}

bool ImGuiHost::wantTextInput() const
{
    if (!m_ready)
        return false;
    return ImGui::GetIO().WantTextInput;
}
