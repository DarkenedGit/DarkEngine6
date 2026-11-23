#pragma once

#include "Assets/Model.h"
#include "Terrain/FoliageFile.h"

#include <string>
#include <vector>

namespace Dark
{

    class Renderer;
    class AssetManager;

    // One planted instance from a showcase glTF. Drops parked LOD1+ meshes, keeps the
    // cluster nearest the origin (bark and leaves together), recenters it, and seats
    // the lowest vertex on Y=0. Blend materials on the kept parts become alpha mask.
    bool makeFoliageShowcaseParts(const std::vector<Model::Part>& parts, std::vector<Model::Part>& out);

    // Every origin cluster, same filters as makeFoliageShowcaseParts. Parts more than 0.25 m apart split.
    bool makeFoliageShowcaseClusters(const std::vector<Model::Part>& parts, std::vector<std::vector<Model::Part>>& out);

    // Procedural tree / flower / rock / grass, plus one glTF prototype per species slot.
    class FoliagePrototypes
    {
    public:
        bool create(Renderer& renderer, AssetManager& assets);
        bool ready() const;

        // Empty path keeps the prototype. A failed or skinned glTF logs once and keeps it too.
        void sync(Renderer& renderer, AssetManager& assets, const Terrain::FoliageDensity& density);

        const AssetRef<Model>& model(Terrain::FoliageKind kind) const;
        const AssetRef<Model>& model(Terrain::FoliageKind kind, uint8_t variant) const;
        uint32_t loadedCount(Terrain::FoliageKind kind) const;
        bool variantReady(Terrain::FoliageKind kind, uint8_t variant) const;
        bool worldScale(Terrain::FoliageKind kind, uint8_t variant) const;

    private:
        static constexpr int kKinds     = static_cast<int>(Terrain::FoliageKind::Count);
        static constexpr int kVariants  = static_cast<int>(Terrain::kMaxFoliageVariants);

        AssetRef<Model> m_proto[kKinds]{};
        AssetRef<Model> m_variant[kKinds][kVariants]{};
        bool            m_ready[kKinds][kVariants]{};
        bool            m_worldScale[kKinds][kVariants]{};
        bool            m_logged[kKinds][kVariants]{};
        uint8_t         m_count[kKinds]{};
        std::string     m_sig[kKinds];
    };

} // namespace Dark
