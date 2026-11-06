#include "Media/MoviePresentMath.h"

namespace Dark::MoviePresentMath
{

    bool letterbox(uint32_t windowWidth, uint32_t windowHeight, uint32_t videoWidth, uint32_t videoHeight, MovieLetterbox& out)
    {
        out = {};
        if (windowWidth == 0 || windowHeight == 0 || videoWidth == 0 || videoHeight == 0)
            return false;

        // Cross-multiply picks the fit axis without dividing first. scale = min(window/video).
        const uint64_t widthFit  = static_cast<uint64_t>(windowWidth) * static_cast<uint64_t>(videoHeight);
        const uint64_t heightFit = static_cast<uint64_t>(windowHeight) * static_cast<uint64_t>(videoWidth);
        const double   windowW   = static_cast<double>(windowWidth);
        const double   windowH   = static_cast<double>(windowHeight);

        if (widthFit <= heightFit)
        {
            const double drawnH = static_cast<double>(videoHeight) * windowW / static_cast<double>(videoWidth);
            const double y      = (windowH - drawnH) * 0.5;
            out.x0              = 0.f;
            out.x1              = 1.f;
            out.y0              = static_cast<float>(y / windowH);
            out.y1              = static_cast<float>((y + drawnH) / windowH);
            return true;
        }

        const double drawnW = static_cast<double>(videoWidth) * windowH / static_cast<double>(videoHeight);
        const double x      = (windowW - drawnW) * 0.5;
        out.y0              = 0.f;
        out.y1              = 1.f;
        out.x0              = static_cast<float>(x / windowW);
        out.x1              = static_cast<float>((x + drawnW) / windowW);
        return true;
    }

} // namespace Dark::MoviePresentMath
