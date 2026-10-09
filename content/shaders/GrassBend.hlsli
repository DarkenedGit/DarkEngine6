#ifndef DE_GRASS_BEND_HLSLI
#define DE_GRASS_BEND_HLSLI

#ifndef GRASS_LOD
#define GRASS_LOD 0
#endif

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

#if GRASS_LOD < 2
float grassShoveFalloff(float dist, float radius)
{
    if (!(radius > 0.15f))
        return dist <= 0.15f ? 1.0f : 0.0f;
    float t = saturate((dist - radius) / (0.15f - radius));
    return t * t * (3.0f - 2.0f * t);
}

float2 grassShoveDirection(float bladeX, float bladeZ, float discX, float discZ, float velX, float velZ, bool bias)
{
    float dx = bladeX - discX;
    float dz = bladeZ - discZ;
    float len2 = dx * dx + dz * dz;
    float2 away = float2(0.0f, 1.0f);
    bool degenerate = !(len2 > 1e-8f);
    if (!degenerate)
        away = float2(dx, dz) * rsqrt(len2);

    float speed2 = velX * velX + velZ * velZ;
    bool moving = bias && speed2 > (0.50f * 0.50f);
    if (!moving)
        return away;

    float2 velDir = float2(velX, velZ) * rsqrt(speed2);
    if (degenerate)
        return velDir;

    float2 mixed = away * 0.75f + velDir * 0.25f;
    float m2 = dot(mixed, mixed);
    if (m2 > 1e-8f)
        return mixed * rsqrt(m2);
    return velDir;
}

float2 grassYawToDir(uint q)
{
    if (q == 0u)
        return float2(0.0f, 1.0f);
    float u = (float(q) - 1.0f) / 65534.0f;
    float yaw = u * 6.28318530717958647692f - 3.14159265358979323846f;
    float s;
    float c;
    sincos(yaw, s, c);
    return float2(s, c);
}

void grassReadPackedYaw(float packed, out float velX, out float velZ, out float prevVelX, out float prevVelZ)
{
    uint bits = asuint(packed);
    uint cur = bits & 0xFFFFu;
    uint prev = bits >> 16u;
    float2 curDir = grassYawToDir(cur);
    float2 prevDir = grassYawToDir(prev);
    velX = cur != 0u ? curDir.x : 0.0f;
    velZ = cur != 0u ? curDir.y : 0.0f;
    prevVelX = prev != 0u ? prevDir.x : 0.0f;
    prevVelZ = prev != 0u ? prevDir.y : 0.0f;
}

// The CB float4 is (x, worldZ, strength, radius). .z is strength, so world Z is .y.
float2 grassGatherShove(float bladeX, float bladeZ, float push, float velX, float velZ, float4 actors[4], float4 prints[8])
{
    float best = 0.0f;
    float winX = 0.0f;
    float winZ = 0.0f;
    bool player = false;
    [loop]
    for (int i = 0; i < 4; ++i)
    {
        float4 disc = actors[i];
        if (disc.z <= 0.0f || disc.w <= 0.0f)
            continue;
        float dx = bladeX - disc.x;
        float dz = bladeZ - disc.y;
        float dist2 = dx * dx + dz * dz;
        if (dist2 > disc.w * disc.w)
            continue;
        float fall = grassShoveFalloff(sqrt(dist2), disc.w) * disc.z;
        if (fall > best)
        {
            best = fall;
            winX = disc.x;
            winZ = disc.y;
            player = (i == 0);
        }
    }
    [loop]
    for (int p = 0; p < 8; ++p)
    {
        float4 disc = prints[p];
        if (disc.z <= 0.0f || disc.w <= 0.0f)
            continue;
        float dx = bladeX - disc.x;
        float dz = bladeZ - disc.y;
        float dist2 = dx * dx + dz * dz;
        if (dist2 > disc.w * disc.w)
            continue;
        float fall = grassShoveFalloff(sqrt(dist2), disc.w) * disc.z;
        if (fall > best)
        {
            best = fall;
            winX = disc.x;
            winZ = disc.y;
            player = false;
        }
    }
    if (!(best > 0.0f) || !(push > 0.0f))
        return float2(0.0f, 0.0f);
    float2 dir = grassShoveDirection(bladeX, bladeZ, winX, winZ, velX, velZ, player);
    return dir * (push * best);
}
#endif

#endif
