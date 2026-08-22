#include "Render/DebugRenderState.h"

namespace Dark
{

    void DebugRenderState::cycleFill()
    {
        switch (fill)
        {
        case DebugFill::Solid:
            fill = DebugFill::Wireframe;
            break;
        case DebugFill::Wireframe:
            fill = DebugFill::Points;
            break;
        default:
            fill = DebugFill::Solid;
            break;
        }
    }

    const char* toString(DebugFill fill)
    {
        switch (fill)
        {
        case DebugFill::Wireframe:
            return "wireframe";
        case DebugFill::Points:
            return "points";
        default:
            return "solid";
        }
    }

} // namespace Dark
