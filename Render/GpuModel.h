#pragma once

#include "Assets/AssetHandle.h"
#include "Math/Matrix4f.h"
#include "Render/Mesh.h"

#include <vector>

namespace Dark
{

    class GpuResourceCache;

    class GpuModel
    {
    public:
        struct Part
        {
            Mesh           mesh;
            AssetID        materialId = NULL_ASSET;
            Math::Matrix4f localToRoot;
            bool           translucent = false;
            bool           skinned     = false;
        };

        const std::vector<Part>& opaque() const { return m_opaque; }
        const std::vector<Part>& translucent() const { return m_translucent; }
        bool valid() const { return !m_opaque.empty() || !m_translucent.empty(); }
        bool hasOpaque() const { return !m_opaque.empty(); }
        bool hasTranslucent() const { return !m_translucent.empty(); }

    private:
        friend class GpuResourceCache;
        std::vector<Part> m_opaque;
        std::vector<Part> m_translucent;
    };

} // namespace Dark
