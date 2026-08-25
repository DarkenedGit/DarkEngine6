#include "Ui/ImGuiHost.h"
#include "Core/Window.h"
#include "Core/Log.h"
#include "Render/Renderer.h"

#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx12.h>

#include <d3d12.h>
#include <wrl/client.h>

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

bool ImGuiHost::init(Dark::Window& window, Dark::Renderer& renderer, const char* iniFilename, bool docking)
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
    io.IniFilename = iniFilename ? iniFilename : "imgui.ini";

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 4.0f;
    style.FrameRounding  = 3.0f;

    const float dpiScale = window.dpiScale();
    if (dpiScale > 1.01f)
        style.ScaleAllSizes(dpiScale);
    ImFontConfig fontCfg{};
    fontCfg.SizePixels = 13.0f * (dpiScale > 0.1f ? dpiScale : 1.0f);
    io.Fonts->AddFontDefault(&fontCfg);
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
    initInfo.Device                           = renderer.device();
    initInfo.CommandQueue                     = renderer.queue();
    initInfo.NumFramesInFlight                = static_cast<int>(Dark::Renderer::kFrameCount);
    initInfo.RTVFormat                        = DXGI_FORMAT_R8G8B8A8_UNORM;
    initInfo.DSVFormat                        = DXGI_FORMAT_D32_FLOAT;
    initInfo.SrvDescriptorHeap                = m_impl->srvHeap.Get();
    initInfo.LegacySingleSrvCpuDescriptor     = m_impl->srvHeap->GetCPUDescriptorHandleForHeapStart();
    initInfo.LegacySingleSrvGpuDescriptor     = m_impl->srvHeap->GetGPUDescriptorHandleForHeapStart();

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

    ImGui::Render();
    ID3D12GraphicsCommandList* cmd = renderer.commandList();
    if (!cmd || !m_impl || !m_impl->srvHeap)
        return;

    ID3D12DescriptorHeap* heaps[] = {m_impl->srvHeap.Get()};
    cmd->SetDescriptorHeaps(1, heaps);
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), cmd);
}

void ImGuiHost::endFrame()
{
}

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
