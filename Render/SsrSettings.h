#pragma once

namespace Dark
{

    struct SsrSettings
    {
        bool  enabled      = true;
        float maxRoughness = 0.4f;
        float thickness    = 0.2f;
        float stride       = 2.0f;
        float edgeFade     = 0.1f;
    };

} // namespace Dark
