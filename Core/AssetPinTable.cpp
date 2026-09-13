#include "Core/AssetPinTable.h"

#include "Assets/AssetManager.h"
#include "Core/Log.h"

namespace Dark
{

    void AssetPinTable::pin(AssetRef<Asset> asset)
    {
        if (!asset || asset->id == NULL_ASSET)
            return;

        Entry& e = m_entries[asset->id];
        if (!e.asset)
            e.asset = std::move(asset);
        ++e.count;
    }

    void AssetPinTable::pin(AssetManager& assets, AssetID id)
    {
        if (id == NULL_ASSET)
            return;
        AssetRef<Asset> asset = assets.get(id);
        if (!asset)
        {
            DE_LOG_ERROR("AssetPinTable: pin missing id={}", id);
            return;
        }
        pin(std::move(asset));
    }

    void AssetPinTable::unpin(AssetID id)
    {
        if (id == NULL_ASSET)
            return;
        const auto it = m_entries.find(id);
        if (it == m_entries.end())
        {
            DE_LOG_ERROR("AssetPinTable: unpin unknown id={}", id);
            DE_ASSERT(false);
            return;
        }
        --it->second.count;
        if (it->second.count <= 0)
            m_entries.erase(it);
    }

    AssetRef<Asset> AssetPinTable::get(AssetID id) const
    {
        if (id == NULL_ASSET)
            return {};
        const auto it = m_entries.find(id);
        if (it == m_entries.end())
            return {};
        return it->second.asset;
    }

    void AssetPinTable::clear()
    {
        m_entries.clear();
    }

} // namespace Dark
