#include "Ui/ImGuiHost.h"
#include "Ui/ImGuiTheme.h"
#include "Core/Window.h"
#include "Core/Log.h"
#include "Core/ContentRoots.h"
#include "Render/Renderer.h"

#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx12.h>

#include <d3d12.h>
#include <wrl/client.h>

#include <filesystem>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

using Microsoft::WRL::ComPtr;

struct ImGuiHost::Impl
{
    ComPtr<ID3D12DescriptorHeap> srvHeap;
    UINT                         srvDescriptorSize = 0;
};

namespace
{

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

    const ImWchar* iconGlyphRanges()
    {
        static const ImWchar kRanges[] = {
            0xf03a, 0xf03a, 0xf04b, 0xf04c, 0xf051, 0xf051, 0xf055, 0xf055, 0xf06e, 0xf06e, 0xf07c, 0xf07c, 0xf0c7, 0xf0c7, 0xf0e7, 0xf0e7,
            0xf0eb, 0xf0eb, 0xf111, 0xf111, 0xf15b, 0xf15b, 0xf188, 0xf188, 0xf1b2, 0xf1b2, 0xf1e6, 0xf1e6, 0xf1f8, 0xf1f8, 0xf233, 0xf233,
            0xf2f5, 0xf2f5, 0xf2f9, 0xf2f9, 0xf51a, 0xf51a, 0xf538, 0xf538, 0xf5fd, 0xf5fd, 0xf625, 0xf625, 0xf6ff, 0xf6ff, 0,
        };
        return kRanges;
    }

    void loadUiFonts(ImGuiIO& io, float dpiScale)
    {
        const float  size = 15.0f * (dpiScale > 0.1f ? dpiScale : 1.0f);
        ImFontConfig cfg{};
        cfg.OversampleH = 2;
        cfg.OversampleV = 1;
        cfg.PixelSnapH  = true;

        ImFont*                     font   = nullptr;
        const std::filesystem::path roboto = findContentFile("fonts/Roboto-Medium.ttf");
        if (!roboto.empty())
            font = io.Fonts->AddFontFromFileTTF(roboto.string().c_str(), size, &cfg);
        if (!font)
            font = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", size, &cfg);
        if (!font)
        {
            ImFontConfig def{};
            def.SizePixels = 13.0f * (dpiScale > 0.1f ? dpiScale : 1.0f);
            io.Fonts->AddFontDefault(&def);
            DE_LOG_WARN("ImGuiHost: using ProggyClean (no UI TTF found)");
            return;
        }

        const std::filesystem::path icons = findContentFile("fonts/fa-solid-900.ttf");
        if (icons.empty())
        {
            DE_LOG_WARN("ImGuiHost: icon font missing (content/fonts/fa-solid-900.ttf)");
            return;
        }
        ImFontConfig ic{};
        ic.MergeMode     = true;
        ic.PixelSnapH    = true;
        ic.GlyphOffset.y = 0.5f;
        io.Fonts->AddFontFromFileTTF(icons.string().c_str(), size * 0.88f, &ic, iconGlyphRanges());
        DE_LOG_INFO("ImGuiHost: UI font {:.1f}px + icons", size);
    }

} // namespace

bool ImGuiHost::createSrvHeap(Dark::Renderer& renderer)
{
    D3D12_DESCRIPTOR_HEAP_DESC desc{};
    desc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.NumDescriptors = 64;
    desc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(renderer.device()->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_impl->srvHeap))))
    {
        DE_LOG_ERROR("ImGuiHost: CreateDescriptorHeap failed");
        return false;
    }
    m_impl->srvDescriptorSize = renderer.device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    return true;
}

bool ImGuiHost::init(Dark::Window& window, Dark::Renderer& renderer, const char* iniFilename, bool docking, Dark::UiAccent accent)
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
    io.ConfigWindowsMoveFromTitleBarOnly = true;
    io.IniFilename                       = iniFilename ? iniFilename : "imgui.ini";

    ImGuiStyle& style = ImGui::GetStyle();
    Dark::applyImGuiTheme(accent, &style);

    const float dpiScale = window.dpiScale();
    if (dpiScale > 1.01f)
        style.ScaleAllSizes(dpiScale);
    loadUiFonts(io, dpiScale);
    DE_LOG_INFO("ImGuiHost: dpi scale {:.2f}", dpiScale);

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

    if (!ImGui_ImplDX12_Init(&initInfo))
    {
        DE_LOG_ERROR("ImGuiHost: DX12 init failed");
        return false;
    }

    window.setMessageHook(&MessageHook, this);
    m_ready = true;
    DE_LOG_INFO("ImGuiHost: ready");
    return true;
}

void ImGuiHost::shutdown(Dark::Renderer& renderer)
{
    if (!m_ready)
        return;

    renderer.waitForGpu();
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    if (m_impl)
    {
        m_impl->srvHeap.Reset();
        delete m_impl;
        m_impl = nullptr;
    }
    m_ready = false;
}

void ImGuiHost::beginFrame()
{
    if (!m_ready)
        return;
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

    ID3D12DescriptorHeap* heaps[] = { m_impl->srvHeap.Get() };
    cmd->SetDescriptorHeaps(1, heaps);
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), cmd);
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
