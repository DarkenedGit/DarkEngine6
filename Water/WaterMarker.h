#pragma once

namespace Dark
{

    // Placed water volume. The world water sheet is a scene block, not this component.
    // Drawing and foliage footprints key off this marker. Scale and height stay on the transform.
    struct WaterMarkerComponent
    {
        static constexpr const char* kTypeName = "WaterMarker";
    };

} // namespace Dark
