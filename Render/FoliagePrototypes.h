#pragma once

#include "Assets/Model.h"
#include "Terrain/FoliageFile.h"

#include <string>

namespace Dark
{

    class Renderer;
    class AssetManager;

    // Procedural tree / flower / rock, plus an optional glTF stand-in per kind.
    class FoliagePrototypes
    {
    public:
        bool create(Renderer& renderer, AssetManager& assets);
        bool ready() const;

        // Empty path keeps the prototype. A failed or skinned glTF logs once and keeps it too.
        void sync(Renderer& renderer, AssetManager& assets, const Terrain::FoliageDensity& density);

        const AssetRef<Model>& model(Terrain::FoliageKind kind) const;

    private:
        static constexpr int kKinds = static_cast<int>(Terrain::FoliageKind::Count);

        AssetRef<Model> m_proto[kKinds]{};
        AssetRef<Model> m_override[kKinds]{};
        std::string     m_path[kKinds];
        bool            m_logged[kKinds]{};
    };

} // namespace Dark
