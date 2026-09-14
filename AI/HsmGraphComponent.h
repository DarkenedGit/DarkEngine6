#pragma once

#include "AI/HsmGraph.h"

#include <cstdint>
#include <memory>

namespace Dark
{
    struct HsmGraphComponent
    {
        static constexpr const char* kTypeName = "HsmGraph";

        AssetRef<HsmGraphDef>              def;
        std::unique_ptr<HsmGraphInstance>  instance;
    };

    // Drive a player HSM from motor + life. Unknown event names are ignored.
    void syncPlayerHsm(HsmGraphInstance& graph, uint8_t motorState, bool alive);
} // namespace Dark
