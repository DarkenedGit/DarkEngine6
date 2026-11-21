#include "Render/Renderer.h"

#include "Core/Log.h"

#include <atomic>
#include <d3d12sdklayers.h>
#include <string>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

// Debug layer, GPU-based validation and DRED. Render graph plan Phase 0 (§3.12).
//
//   DE_D3D12_DEBUG=0  Debug build: no debug layer (also off when PIX or Nsight is attached)
//   DE_D3D12_GBV=1    Debug build: GPU-based validation on top of the debug layer (slow)
//   DE_D3D12_DRED=0   Debug build: no DRED breadcrumbs / page-fault tracking (also off when PIX or Nsight is attached)
//   DE_D3D12_DRED=1   Any build: turn DRED on, even under PIX or Nsight
//
// With the debug layer on, every debug-layer / GBV message of severity WARNING or worse is
// copied into DarkEngine6.log as "D3D12 [ERROR #id] ..." (first 4 of each id), and the totals
// are logged at shutdown. That log is the Phase 0 "GBV findings" source (docs/perf/README.md).

namespace Dark
{
    namespace
    {
        bool moduleLoaded(const wchar_t* name)
        {
            return name && GetModuleHandleW(name) != nullptr;
        }

        // PIX and Nsight Graphics intercept CreateDevice. The debug layer crashes
        // those captures and perturbs Nsight Systems timings. They inject before main.
        bool captureToolAttached()
        {
            return moduleLoaded(L"WinPixGpuCapturer.dll") || moduleLoaded(L"Nvda.Graphics.Interception.dll") || moduleLoaded(L"nvtx64_1.dll");
        }

        // First character of the variable, or 0 when it is unset or empty.
        wchar_t envFlag(const wchar_t* name)
        {
            wchar_t value[8]{};
            if (GetEnvironmentVariableW(name, value, 8) == 0)
                return 0;
            return value[0];
        }

        std::string narrow(const wchar_t* w, const char* a)
        {
            if (a && a[0])
                return a;
            if (!w || !w[0])
                return "(unnamed)";
            char out[256]{};
            if (WideCharToMultiByte(CP_UTF8, 0, w, -1, out, sizeof(out), nullptr, nullptr) <= 0)
                return "(unnamed)";
            return out;
        }

        const char* breadcrumbOpName(D3D12_AUTO_BREADCRUMB_OP op)
        {
            static const char* const kNames[] = {
                "SetMarker", "BeginEvent", "EndEvent", "DrawInstanced", "DrawIndexedInstanced", "ExecuteIndirect", "Dispatch",
                "CopyBufferRegion", "CopyTextureRegion", "CopyResource", "CopyTiles", "ResolveSubresource", "ClearRenderTargetView",
                "ClearUnorderedAccessView", "ClearDepthStencilView", "ResourceBarrier", "ExecuteBundle", "Present", "ResolveQueryData",
                "BeginSubmission", "EndSubmission",
            };
            const auto index = static_cast<unsigned>(op);
            return index < sizeof(kNames) / sizeof(kNames[0]) ? kNames[index] : "Other";
        }

        constexpr uint32_t        kTrackedIds = 4096;
        std::atomic<uint32_t>     g_msgIdCount[kTrackedIds]{};
        std::atomic<uint32_t>     g_msgErrors{ 0 };
        std::atomic<uint32_t>     g_msgWarnings{ 0 };
        std::atomic<uint32_t>     g_msgDistinct{ 0 };

        const char* severityName(D3D12_MESSAGE_SEVERITY s)
        {
            switch (s)
            {
            case D3D12_MESSAGE_SEVERITY_CORRUPTION:
                return "CORRUPTION";
            case D3D12_MESSAGE_SEVERITY_ERROR:
                return "ERROR";
            case D3D12_MESSAGE_SEVERITY_WARNING:
                return "WARNING";
            default:
                return "INFO";
            }
        }

        void __stdcall onDebugMessage(D3D12_MESSAGE_CATEGORY, D3D12_MESSAGE_SEVERITY severity, D3D12_MESSAGE_ID id, const char* description, void*)
        {
            if (severity != D3D12_MESSAGE_SEVERITY_CORRUPTION && severity != D3D12_MESSAGE_SEVERITY_ERROR && severity != D3D12_MESSAGE_SEVERITY_WARNING)
                return;
            if (severity == D3D12_MESSAGE_SEVERITY_WARNING)
                g_msgWarnings.fetch_add(1, std::memory_order_relaxed);
            else
                g_msgErrors.fetch_add(1, std::memory_order_relaxed);
            const uint32_t index = static_cast<uint32_t>(id) < kTrackedIds ? static_cast<uint32_t>(id) : kTrackedIds - 1;
            const uint32_t seen  = g_msgIdCount[index].fetch_add(1, std::memory_order_relaxed);
            if (seen == 0)
                g_msgDistinct.fetch_add(1, std::memory_order_relaxed);
            const LogLevel level = severity == D3D12_MESSAGE_SEVERITY_WARNING ? LogLevel::Warn : LogLevel::Error;
            if (seen < 4)
                Log::log(level, LogCategory::Render, "D3D12 [{} #{}] {}", severityName(severity), static_cast<int>(id), description ? description : "");
            else if (seen == 4)
                Log::log(level, LogCategory::Render, "D3D12 [{} #{}] repeats; further copies are only counted", severityName(severity), static_cast<int>(id));
        }

        const char* removedReasonName(HRESULT hr)
        {
            switch (hr)
            {
            case DXGI_ERROR_DEVICE_HUNG:
                return "DEVICE_HUNG";
            case DXGI_ERROR_DEVICE_REMOVED:
                return "DEVICE_REMOVED";
            case DXGI_ERROR_DEVICE_RESET:
                return "DEVICE_RESET";
            case DXGI_ERROR_DRIVER_INTERNAL_ERROR:
                return "DRIVER_INTERNAL_ERROR";
            case DXGI_ERROR_INVALID_CALL:
                return "INVALID_CALL";
            default:
                return "other";
            }
        }
    } // namespace

    void Renderer::configureDiagnostics()
    {
        m_debugLayerEnabled = false;
        m_dredEnabled       = false;
        const bool gbvRequested = envFlag(L"DE_D3D12_GBV") == L'1';

        const bool    toolAttached = captureToolAttached();
        const wchar_t dredFlag     = envFlag(L"DE_D3D12_DRED");
#if defined(_DEBUG)
        const bool wantDebug = envFlag(L"DE_D3D12_DEBUG") != L'0' && !toolAttached;
        // DRED's breadcrumb writes would end up in a PIX/Nsight capture; DE_D3D12_DRED=1 still forces it on.
        const bool wantDred = dredFlag == L'1' || (dredFlag != L'0' && !toolAttached);
#else
        const bool wantDebug = false;
        const bool wantDred  = dredFlag == L'1';
#endif
        if (toolAttached)
            DE_LOG_INFO(LogCategory::Render, "D3D12: PIX/Nsight attached; debug layer off, DRED {}", wantDred ? "forced on (DE_D3D12_DRED=1)" : "off");

        if (wantDebug)
        {
            ComPtr<ID3D12Debug> debug;
            if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
            {
                debug->EnableDebugLayer();
                m_debugLayerEnabled = true;
                DE_LOG_INFO(LogCategory::Render, "D3D12 debug layer enabled");
                if (gbvRequested)
                {
                    ComPtr<ID3D12Debug1> debug1;
                    if (SUCCEEDED(debug.As(&debug1)))
                    {
                        debug1->SetEnableGPUBasedValidation(TRUE);
                        DE_LOG_WARN(LogCategory::Render, "D3D12 GPU-based validation enabled (DE_D3D12_GBV=1). Expect a much slower frame.");
                    }
                    else
                        DE_LOG_WARN(LogCategory::Render, "DE_D3D12_GBV=1: ID3D12Debug1 is not available, GPU-based validation stays off");
                }
            }
            else
                DE_LOG_WARN(LogCategory::Render, "D3D12 debug layer requested but not installed (Graphics Tools optional feature)");
        }
        else
        {
#if defined(_DEBUG)
            DE_LOG_INFO(LogCategory::Render, "D3D12 debug layer skipped (PIX/Nsight attached, or DE_D3D12_DEBUG=0)");
#endif
            if (gbvRequested)
                DE_LOG_WARN(LogCategory::Render, "DE_D3D12_GBV=1 ignored: GPU-based validation needs the debug layer (Debug build, no PIX/Nsight, DE_D3D12_DEBUG not 0)");
        }

        if (wantDred)
        {
            ComPtr<ID3D12DeviceRemovedExtendedDataSettings> dred;
            if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dred))))
            {
                dred->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
                dred->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
                m_dredEnabled = true;
                DE_LOG_INFO(LogCategory::Render, "D3D12 DRED enabled (auto-breadcrumbs + page faults)");
            }
            else
                DE_LOG_WARN(LogCategory::Render, "D3D12 DRED settings are not available on this runtime");
        }
    }

    void Renderer::attachDebugMessageLog()
    {
        if (!m_debugLayerEnabled || !m_device)
            return;
#if defined(__ID3D12InfoQueue1_INTERFACE_DEFINED__)
        ComPtr<ID3D12InfoQueue1> queue;
        if (FAILED(m_device.As(&queue)))
        {
            DE_LOG_WARN(LogCategory::Render, "D3D12 debug messages stay in the debugger output: ID3D12InfoQueue1 needs Windows 11 or the Agility SDK");
            return;
        }
        DWORD cookie = 0;
        if (FAILED(queue->RegisterMessageCallback(&onDebugMessage, D3D12_MESSAGE_CALLBACK_FLAG_NONE, nullptr, &cookie)))
        {
            DE_LOG_WARN(LogCategory::Render, "D3D12 debug messages: RegisterMessageCallback failed");
            return;
        }
        m_infoQueue       = queue;
        m_infoQueueCookie = cookie;
        DE_LOG_INFO(LogCategory::Render, "D3D12 debug-layer messages (warning and worse) are copied to the log");
#else
        DE_LOG_WARN(LogCategory::Render, "D3D12 debug messages stay in the debugger output: this Windows SDK has no ID3D12InfoQueue1");
#endif
    }

    void Renderer::detachDebugMessageLog()
    {
#if defined(__ID3D12InfoQueue1_INTERFACE_DEFINED__)
        if (m_infoQueue)
        {
            ComPtr<ID3D12InfoQueue1> queue;
            if (SUCCEEDED(m_infoQueue.As(&queue)))
                queue->UnregisterMessageCallback(m_infoQueueCookie);
            m_infoQueue.Reset();
            m_infoQueueCookie = 0;
            DE_LOG_INFO(LogCategory::Render, "D3D12 debug layer totals: {} errors, {} warnings, {} distinct message ids", g_msgErrors.load(), g_msgWarnings.load(),
                        g_msgDistinct.load());
        }
#endif
    }

    void Renderer::logDeviceRemoved(const char* where)
    {
        if (m_removedLogged || !m_device)
            return;
        const HRESULT reason = m_device->GetDeviceRemovedReason();
        if (SUCCEEDED(reason))
            return;
        m_removedLogged = true;
        DE_LOG_ERROR(LogCategory::Render, "{}: D3D12 device removed, reason {} (HRESULT 0x{:08X})", where ? where : "Renderer", removedReasonName(reason),
                     static_cast<unsigned>(reason));
        if (!m_dredEnabled)
        {
            DE_LOG_ERROR(LogCategory::Render, "DRED was off for this run. A Debug build, or DE_D3D12_DRED=1 in Release, records breadcrumbs.");
            return;
        }

        ComPtr<ID3D12DeviceRemovedExtendedData> dred;
        if (FAILED(m_device.As(&dred)))
        {
            DE_LOG_ERROR(LogCategory::Render, "DRED: ID3D12DeviceRemovedExtendedData is not available");
            return;
        }

        D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT crumbs{};
        if (SUCCEEDED(dred->GetAutoBreadcrumbsOutput(&crumbs)))
        {
            uint32_t lists    = 0;
            uint32_t finished = 0;
            for (const D3D12_AUTO_BREADCRUMB_NODE* node = crumbs.pHeadAutoBreadcrumbNode; node && lists < 256; node = node->pNext, ++lists)
            {
                const uint32_t count = node->BreadcrumbCount;
                const uint32_t done  = node->pLastBreadcrumbValue ? *node->pLastBreadcrumbValue : 0u;
                if (done >= count)
                {
                    ++finished;
                    continue;
                }
                DE_LOG_ERROR(LogCategory::Render, "DRED: list '{}' on queue '{}' stopped after {} of {} ops",
                             narrow(node->pCommandListDebugNameW, node->pCommandListDebugNameA), narrow(node->pCommandQueueDebugNameW, node->pCommandQueueDebugNameA),
                             done, count);
                const uint32_t first = done > 6 ? done - 6 : 0u;
                const uint32_t last  = (done + 6 < count) ? done + 6 : count;
                for (uint32_t i = first; node->pCommandHistory && i < last; ++i)
                    DE_LOG_ERROR(LogCategory::Render, "DRED:   [{}] {}{}", i, breadcrumbOpName(node->pCommandHistory[i]), i == done ? "   <-- first op not completed" : "");
            }
            DE_LOG_ERROR(LogCategory::Render, "DRED: {} command lists tracked, {} finished", lists, finished);
        }
        else
            DE_LOG_ERROR(LogCategory::Render, "DRED: no breadcrumb output");

        D3D12_DRED_PAGE_FAULT_OUTPUT fault{};
        if (SUCCEEDED(dred->GetPageFaultAllocationOutput(&fault)))
        {
            DE_LOG_ERROR(LogCategory::Render, "DRED: page fault VA 0x{:016X}", static_cast<unsigned long long>(fault.PageFaultVA));
            uint32_t n = 0;
            for (const D3D12_DRED_ALLOCATION_NODE* a = fault.pHeadExistingAllocationNode; a && n < 16; a = a->pNext, ++n)
                DE_LOG_ERROR(LogCategory::Render, "DRED:   live at VA: '{}' (type {})", narrow(a->ObjectNameW, a->ObjectNameA), static_cast<int>(a->AllocationType));
            n = 0;
            for (const D3D12_DRED_ALLOCATION_NODE* a = fault.pHeadRecentFreedAllocationNode; a && n < 16; a = a->pNext, ++n)
                DE_LOG_ERROR(LogCategory::Render, "DRED:   recently freed at VA: '{}' (type {})", narrow(a->ObjectNameW, a->ObjectNameA), static_cast<int>(a->AllocationType));
        }
    }

} // namespace Dark
