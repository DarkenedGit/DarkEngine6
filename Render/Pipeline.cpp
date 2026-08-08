#include "Render/Pipeline.h"
#include "Core/Log.h"

namespace Dark
{

Pipeline::Pipeline(const PipelineDesc& desc) 
{
    DE_LOG_WARN("Pipeline: stub — PSO creation not yet implemented (vs='{}', ps='{}')",
                desc.vertexShaderPath, desc.pixelShaderPath);
}

Pipeline::~Pipeline() = default;

void Pipeline::bind(ID3D12GraphicsCommandList* cmd) const 
{
    if (!cmd || !m_pso)
        return;

    if (m_rootSignature)
        cmd->SetGraphicsRootSignature(m_rootSignature.Get());
    cmd->SetPipelineState(m_pso.Get());
}

} // namespace DE
