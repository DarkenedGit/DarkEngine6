// GPU billboard integrate. Life is decremented before the Euler step.
// Birth matches ParticleEmitter::spawnOne (Point / Box / Sphere and cone).
// ParticleDepthCollision.h is not on this branch, so the sample lives here.
// Depth is not sampled. Collision is ignored. No groupshared.
#pragma pack_matrix(row_major)

#include "ParticleGpu.hlsli"

cbuffer GpuParticleStep : register(b1)
{
    uint stepSpawnCount;
    uint stepReset;
    uint stepPad0;
    uint stepPad1;
};

StructuredBuffer<GpuParticle>   gPrev : register(t0);
RWStructuredBuffer<GpuParticle> gCurr : register(u0);

uint hashNext(inout uint state)
{
    state = state * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float hash01(inout uint state)
{
    return float(hashNext(state) & 0x00FFFFFFu) * (1.0 / 16777216.0);
}

float3 quatRotate(float4 q, float3 v)
{
    float3 u = q.xyz;
    float3 t = 2.0 * cross(u, v);
    return v + q.w * t + cross(u, t);
}

bool slotInSpawn(uint slot)
{
    uint capacity = sim0.x;
    uint head = sim0.y;
    uint spawnCount = stepSpawnCount;
    if (spawnCount == 0 || capacity == 0 || slot >= capacity)
        return false;
    if (spawnCount >= capacity)
        return true;
    uint dist = (head + capacity - slot) % capacity;
    return dist >= 1u && dist <= spawnCount;
}

GpuParticle birthParticle(uint slot)
{
    uint state = sim0.z ^ (slot * 0x9E3779B9u) ^ (sim0.y * 0x85EBCA6Bu);
    hashNext(state);

    GpuParticle p = (GpuParticle)0;
    p.maxLife = lerp(lifeSpeed.x, lifeSpeed.y, hash01(state));
    if (p.maxLife < 0.05)
        p.maxLife = 0.05;
    p.life = p.maxLife;

    float3 localPos = float3(0, 0, 0);
    if (sim0.w == 1u)
    {
        localPos = float3((hash01(state) * 2.0 - 1.0) * shapeSize.x,
                          (hash01(state) * 2.0 - 1.0) * shapeSize.y,
                          (hash01(state) * 2.0 - 1.0) * shapeSize.z);
    }
    else if (sim0.w == 2u)
    {
        float r = shapeSize.x * pow(hash01(state), 1.0 / 3.0);
        float th = hash01(state) * 6.28318530718;
        float z = hash01(state) * 2.0 - 1.0;
        float s = sqrt(max(0.0, 1.0 - z * z));
        localPos = float3(r * s * cos(th), r * s * sin(th), r * z);
    }

    float3 axis = direction.xyz;
    float axisLenSq = dot(axis, axis);
    if (axisLenSq < 1e-10)
        axis = float3(0, 1, 0);
    else
        axis *= rsqrt(axisLenSq);

    float spread = sim2.z;
    float3 localDir = axis;
    if (spread > 1e-5)
    {
        float cosMax = cos(spread);
        float cosTheta = cosMax + (1.0 - cosMax) * hash01(state);
        float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));
        float phi = hash01(state) * 6.28318530718;
        float3 helper = (abs(axis.y) < 0.99) ? float3(0, 1, 0) : float3(1, 0, 0);
        float3 tangent = cross(axis, helper);
        float tLen = length(tangent);
        tangent = (tLen < 1e-5) ? float3(1, 0, 0) : tangent / tLen;
        float3 bitangent = cross(axis, tangent);
        localDir = axis * cosTheta + tangent * (sinTheta * cos(phi)) + bitangent * (sinTheta * sin(phi));
        float dLen = length(localDir);
        localDir = (dLen < 1e-5) ? axis : localDir / dLen;
    }

    float speed = lerp(lifeSpeed.z, lifeSpeed.w, hash01(state));
    float3 pos = emitterPos.xyz + quatRotate(emitterRot, localPos);
    float3 vel = quatRotate(emitterRot, localDir) * speed;

    p.position0 = pos.x;
    p.position1 = pos.y;
    p.position2 = pos.z;
    p.velocity0 = vel.x;
    p.velocity1 = vel.y;
    p.velocity2 = vel.z;
    p.size0 = lerp(sizes.x, sizes.y, hash01(state));
    p.size1 = lerp(sizes.z, sizes.w, hash01(state));
    p.rotation = hash01(state) * 6.28318530718;
    p.color0 = sim1.x;
    p.color1 = sim1.y;
    p.flags = kGpuParticleAlive;
    p.seed = state;
    p.rest = 0;
    return p;
}

void integrateLoaded(uint slot, GpuParticle p)
{
    if (slotInSpawn(slot))
        p = birthParticle(slot);
    else if ((p.flags & kGpuParticleAlive) == 0u)
    {
        gCurr[slot] = p;
        return;
    }
    else
    {
        float h = sim2.w;
        p.life -= h;
        if (p.life <= 0.0)
            p.flags = 0;
        else
        {
            p.velocity0 += gravity.x * h;
            p.velocity1 += gravity.y * h;
            p.velocity2 += gravity.z * h;
            p.position0 += p.velocity0 * h;
            p.position1 += p.velocity1 * h;
            p.position2 += p.velocity2 * h;
        }
    }
    gCurr[slot] = p;
}

[numthreads(64, 1, 1)]
void CSSimulate(uint3 id : SV_DispatchThreadID)
{
    uint slot = id.x;
    if (slot >= sim0.x)
        return;
    GpuParticle p = (GpuParticle)0;
    if (stepReset == 0u)
        p = gPrev[slot];
    integrateLoaded(slot, p);
}

[numthreads(64, 1, 1)]
void CSInPlace(uint3 id : SV_DispatchThreadID)
{
    uint slot = id.x;
    if (slot >= sim0.x)
        return;
    GpuParticle p = (GpuParticle)0;
    if (stepReset == 0u)
        p = gCurr[slot];
    integrateLoaded(slot, p);
}
