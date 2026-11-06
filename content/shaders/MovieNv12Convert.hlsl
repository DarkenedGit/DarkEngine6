// NV12 Y (R8_UNORM) + UV (R8G8_UNORM) -> B8G8R8A8_UNORM UAV.
// Coefficients match Media/MovieColor.h. DarkRender does not include Media/.
// mode: 0 limited 601, 1 limited 709, 2 full 601, 3 full 709.

cbuffer MovieNv12Constants : register(b0)
{
    uint mode;
    uint flip180;
};

Texture2D<float>  gY   : register(t0);
Texture2D<float2> gUv  : register(t1);
RWTexture2D<float4> gOut : register(u0);

float ByteToUnorm(float channel)
{
    // FXC round() is round-to-nearest-even. MovieColor::sample is half away from zero.
    const float rounded = (channel >= 0.0f) ? floor(channel + 0.5f) : ceil(channel - 0.5f);
    return clamp(rounded, 0.0f, 255.0f) / 255.0f;
}

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    uint width;
    uint height;
    gOut.GetDimensions(width, height);
    if (id.x >= width || id.y >= height)
        return;

    uint2 src = id.xy;
    if (flip180 != 0)
        src = uint2(width - 1 - id.x, height - 1 - id.y);

    const float  Y    = gY.Load(int3(src, 0)) * 255.0f;
    const float2 chroma = gUv.Load(int3(src >> 1, 0)) * 255.0f;
    const float  cb   = chroma.x - 128.0f;
    const float  cr   = chroma.y - 128.0f;

    float r;
    float g;
    float b;
    if (mode == 0)
    {
        const float y = 1.16438356f * (Y - 16.0f);
        r = y + 1.59602678f * cr;
        g = y - 0.39176229f * cb - 0.81296764f * cr;
        b = y + 2.01723214f * cb;
    }
    else if (mode == 1)
    {
        const float y = 1.16438356f * (Y - 16.0f);
        r = y + 1.79274106f * cr;
        g = y - 0.21324861f * cb - 0.53290933f * cr;
        b = y + 2.11240179f * cb;
    }
    else if (mode == 2)
    {
        r = Y + 1.402f * cr;
        g = Y - 0.34413629f * cb - 0.71413629f * cr;
        b = Y + 1.772f * cb;
    }
    else
    {
        r = Y + 1.5748f * cr;
        g = Y - 0.18732427f * cb - 0.46812427f * cr;
        b = Y + 1.8556f * cb;
    }

    // Shader rgba. The B8G8R8A8_UNORM view stores that as BGRA.
    gOut[id.xy] = float4(ByteToUnorm(r), ByteToUnorm(g), ByteToUnorm(b), 1.0f);
}
