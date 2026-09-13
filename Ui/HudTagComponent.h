#pragma once

#include <cstdint>

namespace Dark
{

    enum class HudKind : uint8_t
    {
        HealthBar = 0,
        Crosshair,
        Nameplate,
    };

    struct HudTagComponent
    {
        static constexpr const char* kTypeName = "HudTag";
        HudKind                      kind      = HudKind::HealthBar;
    };

} // namespace Dark
