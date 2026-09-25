#include "Render/GpuModel.h"

#include <utility>

namespace Dark
{

    GpuModel::GpuModel(std::vector<Part> opaque, std::vector<Part> translucent)
        : m_opaque(std::move(opaque))
        , m_translucent(std::move(translucent))
    {
    }

} // namespace Dark
