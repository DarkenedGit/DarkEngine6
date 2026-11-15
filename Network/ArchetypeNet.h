#pragma once

#include "Content/ContentDb.h"
#include "Network/NetTypes.h"

namespace Dark
{

    // Maps an archetype onto the legacy prefab and the replication table.
    // A known prefab with index 0 is the legacy SpawnPayload. Replicate adds the table index.
    inline bool archetypeNetRegistration(std::string_view archetypeId, NetPrefab& prefab, uint16_t& archetypeIndex)
    {
        prefab          = NetPrefab::Unknown;
        archetypeIndex  = 0;
        const Archetype* arch = findArchetype(archetypeId);
        if (!arch)
            return false;
        prefab = prefabFromName(arch->netPrefab);
        if (arch->replicate)
            archetypeIndex = replicationIndex(arch->name);
        return prefab != NetPrefab::Unknown || arch->replicate;
    }

} // namespace Dark
