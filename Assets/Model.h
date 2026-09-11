#pragma once

#include "Animation/AnimationSet.h"
#include "Animation/Skeleton.h"
#include "Assets/AssetHandle.h"
#include "Math/AABox3f.h"
#include "Math/Matrix4f.h"
#include "Render/Material.h"
#include "Render/Mesh.h"

#include <optional>
#include <vector>

namespace Dark
{

    class Renderer;
    class AssetManager;
    class MeshPipeline;
    class ShadowSystem;
    struct GltfCpuModel;

    // GPU-resident glTF model. One AssetManager load per path; primitives are
    // split into opaque (G-buffer / deferred) and translucent (forward).
    class Model : public Asset
    {
    public:
        struct Part
        {
            Mesh                 mesh;
            AssetRef<Material>   material;
            Math::Matrix4f       localToRoot;
            bool                 translucent = false;
            bool                 skinned     = false;
            float                roughness   = 1.0f;
            float                metallic    = 0.0f;
        };

        Model();

        bool createFromFile(Renderer& renderer, AssetManager& assets, const std::filesystem::path& path);
        bool createFromParsed(Renderer& renderer, AssetManager& assets, const GltfCpuModel& cpu, const std::filesystem::path& path);

        void setShadowSrv(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE shadowCpu);

        const std::vector<Part>& opaque() const { return m_opaque; }
        const std::vector<Part>& translucent() const { return m_translucent; }
        const Math::AABox3f&     bounds() const { return m_bounds; }

        bool valid() const { return !m_opaque.empty() || !m_translucent.empty(); }
        bool hasOpaque() const { return !m_opaque.empty(); }
        bool hasTranslucent() const { return !m_translucent.empty(); }

        bool             skinned() const;
        const Skeleton*  skeleton() const;
        uint32_t         jointCount() const;
        AssetRef<AnimationSet> animationSet() const { return m_animSet; }
        void             setAnimationSet(AssetRef<AnimationSet> set);
        void             setSkeleton(Skeleton skeleton);

    private:
        std::vector<Part>      m_opaque;
        std::vector<Part>      m_translucent;
        Math::AABox3f          m_bounds = Math::AABox3f::Empty();
        std::optional<Skeleton> m_skeleton;
        AssetRef<AnimationSet> m_animSet;
    };

} // namespace Dark
