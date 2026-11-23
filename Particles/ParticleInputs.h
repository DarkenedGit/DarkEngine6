#pragma once

#include "Render/HdrDrawInputs.h"

// What the Particles pass uses. CPU only, so the native graph tests drive the same declaration the
// hosts use.

namespace Dark
{

    // HDR and read-only depth, like the other HDR draws. The GPU sim samples depth and the G-buffer
    // attrib from compute, so both are declared for compute as well.
    inline HdrDrawUses particlesUses()
    {
        HdrDrawUses u;
        u.computeDepth  = true;
        u.computeAttrib = true;
        return u;
    }

} // namespace Dark
