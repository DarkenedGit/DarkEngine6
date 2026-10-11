#include "Render/GrassPipeline.h"

#include "Core/Log.h"
#include "Render/DepthState.h"
#include "Render/Profile.h"
#include "Render/Renderer.h"
#include "Render/ShaderCompile.h"
#include "Terrain/GrassMesh.h"

#include <cstddef>
#include <cstring>

namespace Dark
{
    namespace
    {
        static_assert(Renderer::kFrameCount == Terrain::kGrassFrameSlots, "grass upload slots");

        constexpr uint64_t kBladeSlice = static_cast<uint64_t>(Terrain::kGrassBladeCap) * sizeof(Terrain::GrassBlade);
        constexpr uint64_t kWindSlice  = static_cast<uint64_t>(Terrain::kGrassWindSlots) * sizeof(Terrain::GrassTileWind);
        constexpr uint64_t kCbStride   = Terrain::kGrassFrameCbStride;

        constexpr uint32_t kLodSlots[4] = {
            Terrain::kGrassLod0Slots,
            Terrain::kGrassLod1Slots,
            Terrain::kGrassLod2Slots,
            Terrain::kGrassLod3Slots,
        };

        bool FailedHr(HRESULT hr, const char* what)
        {
            if (SUCCEEDED(hr))
                return false;
            DE_LOG_ERROR(LogCategory::Render, "{} failed (HRESULT 0x{:08X})", what, static_cast<unsigned>(hr));
            return true;
        }

        ComPtr<ID3D12Resource> createUpload(ID3D12Device* device, UINT64 bytes, const char* what)
        {
            D3D12_HEAP_PROPERTIES heap{};
            heap.Type = D3D12_HEAP_TYPE_UPLOAD;
            D3D12_RESOURCE_DESC desc{};
            desc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
            desc.Width            = bytes;
            desc.Height           = 1;
            desc.DepthOrArraySize = 1;
            desc.MipLevels        = 1;
            desc.SampleDesc       = { 1, 0 };
            desc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            ComPtr<ID3D12Resource> res;
            if (FailedHr(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&res)), what))
                return nullptr;
            return res;
        }

        ComPtr<ID3D12Resource> createUav(ID3D12Device* device, UINT64 bytes, const char* what)
        {
            D3D12_HEAP_PROPERTIES heap{};
            heap.Type = D3D12_HEAP_TYPE_DEFAULT;
            D3D12_RESOURCE_DESC desc{};
            desc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
            desc.Width            = bytes;
            desc.Height           = 1;
            desc.DepthOrArraySize = 1;
            desc.MipLevels        = 1;
            desc.Format           = DXGI_FORMAT_UNKNOWN;
            desc.SampleDesc       = { 1, 0 };
            desc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            desc.Flags            = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            ComPtr<ID3D12Resource> res;
            if (FailedHr(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&res)), what))
                return nullptr;
            return res;
        }

        constexpr uint32_t kBladeIndexCount[4] = { 24u, 12u, 6u, 6u };

        constexpr UINT kCullConstants = 0;
        constexpr UINT kCullBlades    = 1;
        constexpr UINT kCullWind      = 2;
        constexpr UINT kCullIndices   = 3;
        constexpr UINT kCullCounters  = 4;
        constexpr UINT kCullArgs      = 5;

        struct GrassCullConstants
        {
            float    planes[6][4];
            uint32_t resident[4];
            uint32_t indexCount[4];
            uint32_t indexBase[4];
            uint32_t lodIndex;
            uint32_t windCount;
            uint32_t pad1;
            uint32_t pad2;
        };

        static_assert(sizeof(GrassCullConstants) == 160, "grass cull constants");
        static_assert(offsetof(GrassCullConstants, resident) == 96, "grass cull resident");
        static_assert(offsetof(GrassCullConstants, indexCount) == 112, "grass cull index count");
        static_assert(offsetof(GrassCullConstants, indexBase) == 128, "grass cull index base");
        static_assert(offsetof(GrassCullConstants, lodIndex) == 144, "grass cull lod");

        uint32_t lodIndexBase(int lod)
        {
            uint32_t base = 0;
            for (int i = 0; i < lod && i < 4; ++i)
                base += kLodSlots[i];
            return base;
        }

        uint32_t grassIndexCount(int lod, uint32_t meshCount)
        {
            if (meshCount != 0)
                return meshCount;
            if (lod < 0 || lod >= 4)
                return 0;
            return kBladeIndexCount[lod];
        }

        void transitionBuffer(ID3D12GraphicsCommandList* cmd, ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
        {
            if (!cmd || !resource || before == after)
                return;
            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource   = resource;
            barrier.Transition.StateBefore = before;
            barrier.Transition.StateAfter  = after;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            cmd->ResourceBarrier(1, &barrier);
        }

        void uavBarrier(ID3D12GraphicsCommandList* cmd, ID3D12Resource* resource)
        {
            if (!cmd || !resource)
                return;
            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type          = D3D12_RESOURCE_BARRIER_TYPE_UAV;
            barrier.UAV.pResource = resource;
            cmd->ResourceBarrier(1, &barrier);
        }

        bool grassArgWritable(D3D12_RESOURCE_STATES state)
        {
            return state == D3D12_RESOURCE_STATE_UNORDERED_ACCESS || state == D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
        }

        bool grassIndexWritable(D3D12_RESOURCE_STATES state)
        {
            return state == D3D12_RESOURCE_STATE_UNORDERED_ACCESS || state == D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        }

        bool logGrassCullUnavailable()
        {
            static bool logged = false;
            if (!logged)
            {
                logged = true;
                DE_LOG_ERROR(LogCategory::Render, "GrassPipeline: grass cull unavailable; blades stay full draws");
            }
            return false;
        }
    } // namespace

    struct GrassCullGpu
    {
        ComPtr<ID3D12Device>              device;
        ComPtr<ID3D12RootSignature>       root;
        ComPtr<ID3D12PipelineState>       compact;
        ComPtr<ID3D12PipelineState>       argsPso;
        ComPtr<ID3D12CommandSignature>    signature;
        ComPtr<ID3D12DescriptorHeap>      heap;
        ComPtr<ID3D12Resource>            indices[Terrain::kGrassFrameSlots];
        ComPtr<ID3D12Resource>            counters[Terrain::kGrassFrameSlots];
        ComPtr<ID3D12Resource>            args[Terrain::kGrassFrameSlots];
        D3D12_CPU_DESCRIPTOR_HANDLE       cpu[Terrain::kGrassFrameSlots]{};
        D3D12_GPU_DESCRIPTOR_HANDLE       gpu[Terrain::kGrassFrameSlots]{};
        D3D12_RESOURCE_STATES             argState[Terrain::kGrassFrameSlots]   = { D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS };
        D3D12_RESOURCE_STATES             indexState[Terrain::kGrassFrameSlots] = { D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS };
        bool                              ready                                 = false;
    };

    GrassPipeline::GrassPipeline() = default;

    GrassPipeline::~GrassPipeline()
    {
        destroy();
    }

    bool GrassPipeline::fail(const char* why)
    {
        if (why && why[0])
            DE_LOG_ERROR(LogCategory::Render, "GrassPipeline::create: {}", why);
        destroy();
        return false;
    }

    bool GrassPipeline::create(Renderer& renderer)
    {
        destroy();
        ID3D12Device* device = renderer.device();
        if (!device)
        {
            DE_LOG_ERROR(LogCategory::Render, "GrassPipeline::create: null device");
            return false;
        }
        m_renderer = &renderer;

        for (int lod = 0; lod < 3; ++lod)
        {
            MeshData data;
            if (!Terrain::buildGrassBladeMesh(lod, data))
                return fail("blade mesh");
            if (!Mesh::tryCreate(renderer, data, m_mesh[lod]))
                return fail("Mesh::tryCreate");
        }

        D3D12_ROOT_PARAMETER params[5]{};
        params[kRootCb].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[kRootCb].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;
        params[kRootCb].Descriptor.ShaderRegister = 0;
        params[kRootCb].Descriptor.RegisterSpace  = 0;

        params[kRootBlades].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[kRootBlades].ShaderVisibility          = D3D12_SHADER_VISIBILITY_VERTEX;
        params[kRootBlades].Descriptor.ShaderRegister = 0;
        params[kRootBlades].Descriptor.RegisterSpace  = 0;

        params[kRootWind].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[kRootWind].ShaderVisibility          = D3D12_SHADER_VISIBILITY_VERTEX;
        params[kRootWind].Descriptor.ShaderRegister = 1;
        params[kRootWind].Descriptor.RegisterSpace  = 0;

        params[kRootBase].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[kRootBase].ShaderVisibility         = D3D12_SHADER_VISIBILITY_VERTEX;
        params[kRootBase].Constants.ShaderRegister = 1;
        params[kRootBase].Constants.RegisterSpace  = 0;
        params[kRootBase].Constants.Num32BitValues = kRootBaseDwords;

        params[kRootIndices].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[kRootIndices].ShaderVisibility          = D3D12_SHADER_VISIBILITY_VERTEX;
        params[kRootIndices].Descriptor.ShaderRegister = 2;
        params[kRootIndices].Descriptor.RegisterSpace  = 0;

        D3D12_ROOT_SIGNATURE_DESC rootDesc{};
        rootDesc.NumParameters = 5;
        rootDesc.pParameters   = params;
        rootDesc.Flags         = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        ComPtr<ID3DBlob> rsBlob;
        ComPtr<ID3DBlob> rsErr;
        if (FailedHr(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &rsErr), "D3D12SerializeRootSignature (grass)"))
        {
            if (rsErr)
                DE_LOG_ERROR(LogCategory::Render, "Grass root signature: {}", static_cast<const char*>(rsErr->GetBufferPointer()));
            return fail("root signature");
        }
        if (FailedHr(device->CreateRootSignature(0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(), IID_PPV_ARGS(&m_root)), "CreateRootSignature (grass)"))
            return fail("CreateRootSignature");
        m_root->SetName(L"DE.Grass.Root");

        ComPtr<ID3DBlob> ps;
        if (!compileShaderFromContent("shaders/GrassGBuffer.hlsl", "PSMain", "ps_5_0", ps))
            return fail("pixel shader");

        D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };

        const wchar_t* psoNames[4] = {
            L"DE.Grass.PsoLod0",
            L"DE.Grass.PsoLod1",
            L"DE.Grass.PsoLod2",
            L"DE.Grass.PsoLod3",
        };
        const char* lodDefs[4] = { "0", "1", "2", "3" };

        for (int lod = 0; lod < 4; ++lod)
        {
            D3D_SHADER_MACRO macros[2]{};
            macros[0].Name       = "GRASS_LOD";
            macros[0].Definition = lodDefs[lod];

            ComPtr<ID3DBlob> vs;
            if (!compileShaderFromContent("shaders/GrassGBuffer.hlsl", "VSMain", "vs_5_0", vs, macros))
                return fail("vertex shader");

            D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
            psoDesc.pRootSignature                                   = m_root.Get();
            psoDesc.VS                                               = { vs->GetBufferPointer(), vs->GetBufferSize() };
            psoDesc.PS                                               = { ps->GetBufferPointer(), ps->GetBufferSize() };
            psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
            psoDesc.BlendState.RenderTarget[1].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
            psoDesc.BlendState.RenderTarget[2].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
            psoDesc.BlendState.RenderTarget[3].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
            psoDesc.SampleMask                                       = UINT_MAX;
            psoDesc.RasterizerState.FillMode                         = D3D12_FILL_MODE_SOLID;
            psoDesc.RasterizerState.CullMode                         = D3D12_CULL_MODE_NONE;
            psoDesc.RasterizerState.FrontCounterClockwise            = TRUE;
            psoDesc.RasterizerState.DepthClipEnable                  = TRUE;
            psoDesc.DepthStencilState.DepthEnable                    = TRUE;
            psoDesc.DepthStencilState.DepthWriteMask                 = D3D12_DEPTH_WRITE_MASK_ALL;
            psoDesc.DepthStencilState.DepthFunc                      = sceneDepthFunc();
            psoDesc.DepthStencilState.StencilEnable                  = FALSE;
            psoDesc.InputLayout                                      = { inputLayout, _countof(inputLayout) };
            psoDesc.PrimitiveTopologyType                            = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            psoDesc.NumRenderTargets                                 = 4;
            psoDesc.RTVFormats[0]                                    = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
            psoDesc.RTVFormats[1]                                    = DXGI_FORMAT_R8G8B8A8_UNORM;
            psoDesc.RTVFormats[2]                                    = DXGI_FORMAT_R16G16_FLOAT;
            psoDesc.RTVFormats[3]                                    = DXGI_FORMAT_R8_UNORM;
            psoDesc.DSVFormat                                        = DXGI_FORMAT_D32_FLOAT;
            psoDesc.SampleDesc                                       = { 1, 0 };

            if (FailedHr(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pso[lod])), "CreateGraphicsPipelineState (grass)"))
                return fail("graphics PSO");
            m_pso[lod]->SetName(psoNames[lod]);
        }

        m_blades = createUpload(device, kBladeSlice * Renderer::kFrameCount, "CreateCommittedResource grass blades");
        m_wind   = createUpload(device, kWindSlice * Renderer::kFrameCount, "CreateCommittedResource grass wind");
        m_cb     = createUpload(device, kCbStride * Renderer::kFrameCount, "CreateCommittedResource grass constants");
        if (!m_blades || !m_wind || !m_cb)
            return fail("upload buffer");

        if (FailedHr(m_blades->Map(0, nullptr, reinterpret_cast<void**>(&m_bladeMap)), "Map grass blades") ||
            FailedHr(m_wind->Map(0, nullptr, reinterpret_cast<void**>(&m_windMap)), "Map grass wind") || FailedHr(m_cb->Map(0, nullptr, reinterpret_cast<void**>(&m_cbMap)), "Map grass constants"))
            return fail("map upload");

        std::memset(m_bladeMap, 0, static_cast<size_t>(kBladeSlice * Renderer::kFrameCount));
        std::memset(m_windMap, 0, static_cast<size_t>(kWindSlice * Renderer::kFrameCount));
        std::memset(m_cbMap, 0, static_cast<size_t>(kCbStride * Renderer::kFrameCount));
        m_bladeGpu = m_blades->GetGPUVirtualAddress();
        m_windGpu  = m_wind->GetGPUVirtualAddress();
        m_cbGpu    = m_cb->GetGPUVirtualAddress();

        m_blades->SetName(L"DE.Grass.Blades");
        m_wind->SetName(L"DE.Grass.Wind");
        m_cb->SetName(L"DE.Grass.Constants");

        // LOD-local 0,1,2,... inside each span. The vertex shader adds baseIndex, so a global identity is wrong for LOD 1+.
        m_identity = createUpload(device, static_cast<UINT64>(Terrain::kGrassBladeCap) * sizeof(uint32_t), "CreateCommittedResource grass identity");
        if (!m_identity)
            return fail("identity indices");
        uint32_t* identity = nullptr;
        if (FailedHr(m_identity->Map(0, nullptr, reinterpret_cast<void**>(&identity)), "Map grass identity"))
            return fail("identity indices");
        uint32_t cursor = 0;
        for (int lod = 0; lod < 4; ++lod)
        {
            const uint32_t slots = kLodSlots[lod];
            for (uint32_t i = 0; i < slots; ++i)
                identity[cursor + i] = i;
            cursor += slots;
        }
        m_identity->Unmap(0, nullptr);
        DE_ASSERT(cursor == Terrain::kGrassBladeCap);
        m_identityGpu = m_identity->GetGPUVirtualAddress();
        m_identity->SetName(L"DE.Grass.Identity");

        DE_ASSERT(m_mesh[0].indexCount() == 24u);
        DE_ASSERT(m_mesh[1].indexCount() == 12u);
        DE_ASSERT(m_mesh[2].indexCount() == 6u);

        // Cull failure leaves the four full draws. It does not turn grass off.
        createCull(device);

        m_valid = true;
        DE_LOG_INFO(LogCategory::Render, "GrassPipeline: ready ({} blades, {} slices)", Terrain::kGrassBladeCap, Renderer::kFrameCount);
        return true;
    }

    void GrassPipeline::destroy()
    {
        if (m_blades && m_bladeMap)
            m_blades->Unmap(0, nullptr);
        if (m_wind && m_windMap)
            m_wind->Unmap(0, nullptr);
        if (m_cb && m_cbMap)
            m_cb->Unmap(0, nullptr);
        m_bladeMap = nullptr;
        m_windMap  = nullptr;
        m_cbMap    = nullptr;

        if (m_renderer)
        {
            for (int i = 0; i < 3; ++i)
                m_mesh[i].deferRelease(*m_renderer);
            m_renderer->deferRelease(m_root.Get());
            for (int i = 0; i < 4; ++i)
                m_renderer->deferRelease(m_pso[i].Get());
            m_renderer->deferRelease(m_blades.Get());
            m_renderer->deferRelease(m_wind.Get());
            m_renderer->deferRelease(m_cb.Get());
            m_renderer->deferRelease(m_identity.Get());
            if (m_cull)
            {
                m_renderer->deferRelease(m_cull->root.Get());
                m_renderer->deferRelease(m_cull->compact.Get());
                m_renderer->deferRelease(m_cull->argsPso.Get());
                m_renderer->deferRelease(m_cull->signature.Get());
                m_renderer->deferRelease(m_cull->heap.Get());
                for (uint32_t slot = 0; slot < Terrain::kGrassFrameSlots; ++slot)
                {
                    m_renderer->deferRelease(m_cull->indices[slot].Get());
                    m_renderer->deferRelease(m_cull->counters[slot].Get());
                    m_renderer->deferRelease(m_cull->args[slot].Get());
                }
            }
        }
        else
        {
            for (int i = 0; i < 3; ++i)
                m_mesh[i] = Mesh{};
        }

        m_root.Reset();
        for (int i = 0; i < 4; ++i)
            m_pso[i].Reset();
        m_blades.Reset();
        m_wind.Reset();
        m_cb.Reset();
        m_bladeGpu    = 0;
        m_windGpu     = 0;
        m_cbGpu       = 0;
        m_identity.Reset();
        m_identityGpu = 0;
        m_cull.reset();
        m_renderer = nullptr;
        m_valid    = false;
    }

    bool GrassPipeline::createCull(ID3D12Device* device)
    {
        m_cull.reset();
        if (!device)
            return logGrassCullUnavailable();

        std::unique_ptr<GrassCullGpu> cull = std::make_unique<GrassCullGpu>();
        cull->device                        = device;

        D3D12_ROOT_PARAMETER params[6]{};
        params[kCullConstants].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[kCullConstants].ShaderVisibility         = D3D12_SHADER_VISIBILITY_ALL;
        params[kCullConstants].Constants.ShaderRegister = 0;
        params[kCullConstants].Constants.RegisterSpace  = 0;
        params[kCullConstants].Constants.Num32BitValues = static_cast<UINT>(sizeof(GrassCullConstants) / sizeof(uint32_t));

        params[kCullBlades].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[kCullBlades].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;
        params[kCullBlades].Descriptor.ShaderRegister = 0;
        params[kCullBlades].Descriptor.RegisterSpace  = 0;

        params[kCullWind].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[kCullWind].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;
        params[kCullWind].Descriptor.ShaderRegister = 1;
        params[kCullWind].Descriptor.RegisterSpace  = 0;

        params[kCullIndices].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_UAV;
        params[kCullIndices].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;
        params[kCullIndices].Descriptor.ShaderRegister = 0;
        params[kCullIndices].Descriptor.RegisterSpace  = 0;

        params[kCullCounters].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_UAV;
        params[kCullCounters].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;
        params[kCullCounters].Descriptor.ShaderRegister = 1;
        params[kCullCounters].Descriptor.RegisterSpace  = 0;

        params[kCullArgs].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_UAV;
        params[kCullArgs].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;
        params[kCullArgs].Descriptor.ShaderRegister = 2;
        params[kCullArgs].Descriptor.RegisterSpace  = 0;

        D3D12_ROOT_SIGNATURE_DESC rootDesc{};
        rootDesc.NumParameters = 6;
        rootDesc.pParameters   = params;

        ComPtr<ID3DBlob> rsBlob;
        ComPtr<ID3DBlob> rsErr;
        if (FailedHr(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &rsErr), "D3D12SerializeRootSignature (grass cull)"))
        {
            if (rsErr)
                DE_LOG_ERROR(LogCategory::Render, "Grass cull root signature: {}", static_cast<const char*>(rsErr->GetBufferPointer()));
            return logGrassCullUnavailable();
        }
        if (FailedHr(device->CreateRootSignature(0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(), IID_PPV_ARGS(&cull->root)), "CreateRootSignature (grass cull)"))
            return logGrassCullUnavailable();

        ComPtr<ID3DBlob> compact;
        ComPtr<ID3DBlob> args;
        if (!compileShaderFromContent("shaders/GrassCull.hlsl", "CSCompact", "cs_5_0", compact) || !compileShaderFromContent("shaders/GrassCull.hlsl", "CSArgs", "cs_5_0", args))
            return logGrassCullUnavailable();

        D3D12_COMPUTE_PIPELINE_STATE_DESC pso{};
        pso.pRootSignature = cull->root.Get();
        pso.CS             = { compact->GetBufferPointer(), compact->GetBufferSize() };
        if (FailedHr(device->CreateComputePipelineState(&pso, IID_PPV_ARGS(&cull->compact)), "CreateComputePipelineState (grass cull compact)"))
            return logGrassCullUnavailable();
        pso.CS = { args->GetBufferPointer(), args->GetBufferSize() };
        if (FailedHr(device->CreateComputePipelineState(&pso, IID_PPV_ARGS(&cull->argsPso)), "CreateComputePipelineState (grass cull args)"))
            return logGrassCullUnavailable();
        cull->compact->SetName(L"DE.Grass.CullCompact");
        cull->argsPso->SetName(L"DE.Grass.CullArgs");

        D3D12_INDIRECT_ARGUMENT_DESC argument{};
        argument.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;
        D3D12_COMMAND_SIGNATURE_DESC signature{};
        signature.ByteStride       = sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
        signature.NumArgumentDescs = 1;
        signature.pArgumentDescs   = &argument;
        if (FailedHr(device->CreateCommandSignature(&signature, nullptr, IID_PPV_ARGS(&cull->signature)), "CreateCommandSignature (grass)"))
            return logGrassCullUnavailable();
        cull->signature->SetName(L"DE.Grass.CommandSignature");

        // ClearUnorderedAccessViewUint takes a GPU handle in the heap currently bound on this list.
        // The design asked for a non-shader-visible heap; that handle would be invalid there.
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
        heapDesc.NumDescriptors = Terrain::kGrassFrameSlots;
        heapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FailedHr(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&cull->heap)), "CreateDescriptorHeap (grass cull)"))
            return logGrassCullUnavailable();

        const UINT64   indexBytes   = static_cast<UINT64>(Terrain::kGrassBladeCap) * sizeof(uint32_t);
        const UINT64   counterBytes = 4ull * sizeof(uint32_t);
        const UINT64   argBytes     = 4ull * sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
        const wchar_t* indexNames[] = { L"DE.Grass.CullIndices0", L"DE.Grass.CullIndices1" };
        const wchar_t* counterNames[] = { L"DE.Grass.CullCounters0", L"DE.Grass.CullCounters1" };
        const wchar_t* argNames[]     = { L"DE.Grass.CullArgs0", L"DE.Grass.CullArgs1" };

        const UINT                      increment = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        D3D12_CPU_DESCRIPTOR_HANDLE     cpu       = cull->heap->GetCPUDescriptorHandleForHeapStart();
        D3D12_GPU_DESCRIPTOR_HANDLE     gpu       = cull->heap->GetGPUDescriptorHandleForHeapStart();
        for (uint32_t slot = 0; slot < Terrain::kGrassFrameSlots; ++slot)
        {
            cull->indices[slot]  = createUav(device, indexBytes, "CreateCommittedResource (grass cull indices)");
            cull->counters[slot] = createUav(device, counterBytes, "CreateCommittedResource (grass cull counters)");
            cull->args[slot]     = createUav(device, argBytes, "CreateCommittedResource (grass cull args)");
            if (!cull->indices[slot] || !cull->counters[slot] || !cull->args[slot])
                return logGrassCullUnavailable();
            cull->indices[slot]->SetName(indexNames[slot]);
            cull->counters[slot]->SetName(counterNames[slot]);
            cull->args[slot]->SetName(argNames[slot]);

            cull->cpu[slot] = cpu;
            cull->gpu[slot] = gpu;
            D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
            uav.Format              = DXGI_FORMAT_R32_UINT;
            uav.ViewDimension       = D3D12_UAV_DIMENSION_BUFFER;
            uav.Buffer.FirstElement = 0;
            uav.Buffer.NumElements  = 4;
            device->CreateUnorderedAccessView(cull->counters[slot].Get(), nullptr, &uav, cpu);
            cpu.ptr += increment;
            gpu.ptr += increment;
        }

        cull->ready = true;
        m_cull      = std::move(cull);
        return true;
    }

    bool GrassPipeline::cullReady() const
    {
        return m_cull && m_cull->ready;
    }

    ID3D12Resource* GrassPipeline::cullArgumentResource(uint32_t frameSlot) const
    {
        if (!m_cull || frameSlot >= Terrain::kGrassFrameSlots)
            return nullptr;
        return m_cull->args[frameSlot].Get();
    }

    ID3D12Resource* GrassPipeline::cullIndexResource(uint32_t frameSlot) const
    {
        if (!m_cull || frameSlot >= Terrain::kGrassFrameSlots)
            return nullptr;
        return m_cull->indices[frameSlot].Get();
    }

    bool GrassPipeline::recordCull(ID3D12GraphicsCommandList* cmd, uint32_t frameSlot, const GrassLodSpan lods[4], const D3D12_GPU_VIRTUAL_ADDRESS bladeVa[4],
                                   D3D12_GPU_VIRTUAL_ADDRESS windVa, uint32_t windCount, const float planes[6][4])
    {
        if (!cmd || !lods || !bladeVa || !planes || !cullReady() || frameSlot >= Terrain::kGrassFrameSlots)
            return false;

        uint32_t resident[4]{};
        bool     any = false;
        for (int lod = 0; lod < 4; ++lod)
        {
            uint32_t count = lods[lod].count;
            if (count > kLodSlots[lod])
                count = kLodSlots[lod];
            if (count > 0 && bladeVa[lod] == 0)
                count = 0;
            resident[lod] = count;
            any = any || count > 0;
        }
        if (!any)
            return false;

        GrassCullGpu& cull = *m_cull;
        if (!grassArgWritable(cull.argState[frameSlot]) || !grassIndexWritable(cull.indexState[frameSlot]))
        {
            static bool logged = false;
            if (!logged)
            {
                logged = true;
                DE_LOG_ERROR(LogCategory::Render, "GrassPipeline: grass cull skipped; buffer state is not a compute source");
            }
            return false;
        }

        ID3D12Resource* indices  = cull.indices[frameSlot].Get();
        ID3D12Resource* counters = cull.counters[frameSlot].Get();
        ID3D12Resource* args     = cull.args[frameSlot].Get();
        if (cull.argState[frameSlot] != D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
        {
            transitionBuffer(cmd, args, cull.argState[frameSlot], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            cull.argState[frameSlot] = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        }
        if (cull.indexState[frameSlot] != D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
        {
            transitionBuffer(cmd, indices, cull.indexState[frameSlot], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            cull.indexState[frameSlot] = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        }

        ID3D12DescriptorHeap* heaps[] = { cull.heap.Get() };
        cmd->SetDescriptorHeaps(1, heaps);
        cmd->SetComputeRootSignature(cull.root.Get());
        cmd->SetComputeRootUnorderedAccessView(kCullIndices, indices->GetGPUVirtualAddress());
        cmd->SetComputeRootUnorderedAccessView(kCullCounters, counters->GetGPUVirtualAddress());
        cmd->SetComputeRootUnorderedAccessView(kCullArgs, args->GetGPUVirtualAddress());
        if (windVa == 0)
            windCount = 0;
        cmd->SetComputeRootShaderResourceView(kCullWind, windVa);

        const UINT zeros[4] = { 0, 0, 0, 0 };
        cmd->ClearUnorderedAccessViewUint(cull.gpu[frameSlot], cull.cpu[frameSlot], counters, zeros, 0, nullptr);
        uavBarrier(cmd, counters);

        GrassCullConstants constants{};
        for (int plane = 0; plane < 6; ++plane)
        {
            constants.planes[plane][0] = planes[plane][0];
            constants.planes[plane][1] = planes[plane][1];
            constants.planes[plane][2] = planes[plane][2];
            constants.planes[plane][3] = planes[plane][3];
        }
        for (int lod = 0; lod < 4; ++lod)
        {
            const int meshIndex       = lod < 2 ? lod : 2;
            constants.resident[lod]   = resident[lod];
            constants.indexCount[lod] = grassIndexCount(lod, m_mesh[meshIndex].indexCount());
            constants.indexBase[lod]  = lodIndexBase(lod);
        }
        constants.windCount = windCount;

        cmd->SetPipelineState(cull.compact.Get());
        for (int lod = 0; lod < 4; ++lod)
        {
            if (resident[lod] == 0)
                continue;
            constants.lodIndex = static_cast<uint32_t>(lod);
            cmd->SetComputeRoot32BitConstants(kCullConstants, static_cast<UINT>(sizeof(GrassCullConstants) / sizeof(uint32_t)), &constants, 0);
            cmd->SetComputeRootShaderResourceView(kCullBlades, bladeVa[lod]);
            const UINT groups = (resident[lod] + 63u) / 64u;
            cmd->Dispatch(groups, 1, 1);
        }
        uavBarrier(cmd, counters);

        constants.lodIndex = 0;
        cmd->SetComputeRoot32BitConstants(kCullConstants, static_cast<UINT>(sizeof(GrassCullConstants) / sizeof(uint32_t)), &constants, 0);
        cmd->SetPipelineState(cull.argsPso.Get());
        cmd->Dispatch(1, 1, 1);

        D3D12_RESOURCE_BARRIER done[2]{};
        done[0].Type          = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        done[0].UAV.pResource = args;
        done[1].Type          = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        done[1].UAV.pResource = indices;
        cmd->ResourceBarrier(2, done);
        transitionBuffer(cmd, args, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
        transitionBuffer(cmd, indices, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        cull.argState[frameSlot]   = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
        cull.indexState[frameSlot] = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        return true;
    }

    bool GrassPipeline::draw(ID3D12GraphicsCommandList* cmd, Renderer& renderer, const GrassLodSpan lods[4], const Terrain::GrassTileWind* tileWind, uint32_t tileCount,
                             const Terrain::GrassFrameConstants& frame, const Frustum3f& cullFrustum, bool indirect)
    {
        if (!cmd || !m_valid || !lods || !m_bladeMap || !m_windMap || !m_cbMap)
            return false;
        ID3D12Device* device = renderer.device();
        if (!device || FAILED(device->GetDeviceRemovedReason()))
            return false;

        // Whole-LOD upload: removeSpan or append marks both frame slots, and only this fenced slot is copied.
        // A warm frame copies no blades. Wind stays a full-slice copy.
        // A published cull uses ExecuteIndirect. Otherwise DrawIndexedInstanced reads the identity indices.
        const uint32_t slot = renderer.frameIndex() % Terrain::kGrassFrameSlots;

        uint32_t                  counts[4]{};
        D3D12_GPU_VIRTUAL_ADDRESS bladeVa[4]{};
        uint32_t                  bladeCursor = 0;
        for (int lod = 0; lod < 4; ++lod)
        {
            const uint32_t slots = kLodSlots[lod];
            uint32_t       count = 0;
            if (lods[lod].blades)
                count = lods[lod].count;
            if (count > slots)
                count = slots;
            const uint64_t byteOff = static_cast<uint64_t>(bladeCursor) * sizeof(Terrain::GrassBlade);
            const bool     upload  = count > 0 && (lods[lod].staleMask & static_cast<uint8_t>(1u << slot)) != 0;
            if (upload)
                std::memcpy(m_bladeMap + slot * kBladeSlice + byteOff, lods[lod].blades, sizeof(Terrain::GrassBlade) * count);
            counts[lod]  = count;
            bladeVa[lod] = m_bladeGpu + slot * kBladeSlice + byteOff;
            bladeCursor += slots;
        }

        uint32_t windCount = 0;
        uint8_t* windDst   = m_windMap + slot * kWindSlice;
        std::memset(windDst, 0, static_cast<size_t>(kWindSlice));
        if (tileWind && tileCount > 0)
        {
            windCount = tileCount < Terrain::kGrassWindSlots ? tileCount : Terrain::kGrassWindSlots;
            std::memcpy(windDst, tileWind, sizeof(Terrain::GrassTileWind) * windCount);
        }

        std::memcpy(m_cbMap + slot * kCbStride, &frame, sizeof(frame));

        bool any = false;
        for (int lod = 0; lod < 4; ++lod)
            any = any || counts[lod] > 0;

        bool culled = false;
        if (indirect && any && cullReady())
        {
            float planes[6][4]{};
            for (int plane = 0; plane < 6; ++plane)
            {
                const Math::Plane4f& p = cullFrustum.GetPlane(plane);
                planes[plane][0]       = p.x;
                planes[plane][1]       = p.y;
                planes[plane][2]       = p.z;
                planes[plane][3]       = p.w;
            }
            GrassLodSpan cullSpans[4]{};
            for (int lod = 0; lod < 4; ++lod)
                cullSpans[lod].count = counts[lod];
            const GpuScope cullScope(cmd, "Grass Cull", ProfileColor::Grass);
            culled = recordCull(cmd, slot, cullSpans, bladeVa, m_windGpu + slot * kWindSlice, windCount, planes);
        }

        const GpuScope scope(cmd, "Grass", ProfileColor::Grass);
        cmd->SetGraphicsRootSignature(m_root.Get());
        cmd->SetGraphicsRootConstantBufferView(kRootCb, m_cbGpu + slot * kCbStride);
        cmd->SetGraphicsRootShaderResourceView(kRootWind, m_windGpu + slot * kWindSlice);
        const D3D12_GPU_VIRTUAL_ADDRESS indexVa = culled ? m_cull->indices[slot]->GetGPUVirtualAddress() : m_identityGpu;
        cmd->SetGraphicsRootShaderResourceView(kRootIndices, indexVa);

        for (int lod = 0; lod < 4; ++lod)
        {
            if (counts[lod] == 0)
                continue;
            const int      meshIndex                      = lod < 2 ? lod : 2;
            const uint32_t baseWords[kRootBaseDwords]     = { lodIndexBase(lod), 0u, 0u, 0u };
            cmd->SetPipelineState(m_pso[lod].Get());
            cmd->SetGraphicsRootShaderResourceView(kRootBlades, bladeVa[lod]);
            cmd->SetGraphicsRoot32BitConstants(kRootBase, kRootBaseDwords, baseWords, 0);
            if (culled)
                m_mesh[meshIndex].drawIndirect(cmd, m_cull->signature.Get(), m_cull->args[slot].Get(), static_cast<UINT64>(lod) * sizeof(D3D12_DRAW_INDEXED_ARGUMENTS));
            else
                m_mesh[meshIndex].drawInstanced(cmd, counts[lod]);
        }
        return true;
    }

} // namespace Dark
