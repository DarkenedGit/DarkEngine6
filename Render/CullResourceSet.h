#pragma once

#include <cstdint>

namespace Dark
{

// Depth passes use a second args/index/counter set so a shadow worker can
// transition those buffers while the main thread still owns the camera set.
enum class CullResourceSet : uint8_t
{
    Camera = 0,
    Depth  = 1,
};

inline CullResourceSet cullResourceSetForDepth(bool depthView)
{
    return depthView ? CullResourceSet::Depth : CullResourceSet::Camera;
}

inline bool usesCameraArgs(CullResourceSet set)
{
    return set == CullResourceSet::Camera;
}

} // namespace Dark
