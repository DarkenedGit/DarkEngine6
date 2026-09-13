#pragma once

#include "Assets/AssetHandle.h"

#include <unordered_map>

namespace Dark
{

    class AssetManager;

    // Extra strong refs for AssetIDs named by live entities. Does not hook World.
    class AssetPinTable
    {
    public:
        void            pin(AssetRef<Asset> asset);
        void            pin(AssetManager& assets, AssetID id);
        void            unpin(AssetID id);
        AssetRef<Asset> get(AssetID id) const;
        void            clear();

    private:
        struct Entry
        {
            AssetRef<Asset> asset;
            int             count = 0;
        };

        std::unordered_map<AssetID, Entry> m_entries;
    };

} // namespace Dark
