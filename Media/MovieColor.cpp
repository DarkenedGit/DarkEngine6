#include "Media/MovieColor.h"

#include <cmath>

namespace Dark::MovieColor
{

    namespace
    {

        uint8_t quantize(float v)
        {
            if (!std::isfinite(v))
                return 0;
            const float rounded = std::round(v);
            if (rounded <= 0.f)
                return 0;
            if (rounded >= 255.f)
                return 255;
            return static_cast<uint8_t>(rounded);
        }

    } // namespace

    Bgra sample(float Y, float Cb, float Cr, uint32_t codedHeight, Range range)
    {
        const float cr   = Cr - 128.0f;
        const float cb   = Cb - 128.0f;
        const float luma = (range == Range::Full) ? Y : (1.16438356f * (Y - 16.0f));
        const bool  bt709 = codedHeight > 576u;

        float r = 0.f;
        float g = 0.f;
        float b = 0.f;
        if (range == Range::Full)
        {
            if (bt709)
            {
                r = luma + 1.5748f * cr;
                g = luma - 0.18732427f * cb - 0.46812427f * cr;
                b = luma + 1.8556f * cb;
            }
            else
            {
                r = luma + 1.402f * cr;
                g = luma - 0.34413629f * cb - 0.71413629f * cr;
                b = luma + 1.772f * cb;
            }
        }
        else if (bt709)
        {
            r = luma + 1.79274106f * cr;
            g = luma - 0.21324861f * cb - 0.53290933f * cr;
            b = luma + 2.11240179f * cb;
        }
        else
        {
            r = luma + 1.59602678f * cr;
            g = luma - 0.39176229f * cb - 0.81296764f * cr;
            b = luma + 2.01723214f * cb;
        }

        Bgra out;
        out.b = quantize(b);
        out.g = quantize(g);
        out.r = quantize(r);
        out.a = 255;
        return out;
    }

} // namespace Dark::MovieColor
