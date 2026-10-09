#ifndef DE_GRASS_BEND_HLSLI
#define DE_GRASS_BEND_HLSLI

struct GrassTipIn
{
    float height;
    float flex;
    float windX;
    float windZ;
    float shoveX;
    float shoveZ;
};

struct GrassTip
{
    float x;
    float z;
};

struct GrassLocal
{
    float x;
    float y;
    float z;
};

GrassTip grassTipOffset(GrassTipIn v)
{
    GrassTip tip;
    tip.x = (v.windX + v.shoveX) * v.flex;
    tip.z = (v.windZ + v.shoveZ) * v.flex;
    float maxLen = 0.85f * v.height;
    float len2 = tip.x * tip.x + tip.z * tip.z;
    if (maxLen > 0.0f && len2 > maxLen * maxLen)
    {
        float s = maxLen / sqrt(len2);
        tip.x *= s;
        tip.z *= s;
    }
    return tip;
}

float grassYTip(GrassTip tip, float height)
{
    float tipLen = length(float2(tip.x, tip.z));
    float yFloor = 0.20f * height;
    return sqrt(max(height * height - tipLen * tipLen, yFloor * yFloor));
}

GrassLocal grassBladeLocal(int lod, float t, GrassTip tip, float height)
{
    float yTip = grassYTip(tip, height);
    GrassLocal local;
    if (lod <= 1)
    {
        float u = 1.0f - t;
        float b0 = u * u;
        float b1 = 2.0f * u * t;
        float b2 = t * t;
        float3 p0 = float3(0.0f, 0.0f, 0.0f);
        float3 p1 = float3(tip.x * 0.25f, height * 0.55f, tip.z * 0.25f);
        float3 p2 = float3(tip.x, yTip, tip.z);
        float3 p = b0 * p0 + b1 * p1 + b2 * p2;
        local.x = p.x;
        local.y = p.y;
        local.z = p.z;
        return local;
    }

    local.x = tip.x * t;
    local.y = lerp(0.0f, yTip, t);
    local.z = tip.z * t;
    return local;
}

#endif
