#pragma once

#include "Assets/ImageCache.h"

namespace Dark
{
    // GPU intern moved to GpuResourceCache. Key helpers + CPU decode waiters live on ImageCache.
    using TextureCache = ImageCache;
}
