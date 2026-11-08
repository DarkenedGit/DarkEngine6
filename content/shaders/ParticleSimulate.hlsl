// GPU billboard integrate. Life is decremented before the Euler step.
// Birth hash stream matches particleBirthSample (xorshift32). The seed mix is
// hash(emitterSeed, head, slot); the header stores that mix and starts the
// stream at 1 when the mix is 0.
// Depth is this frame's R32_FLOAT scene depth, point Load. No groupshared.
#pragma pack_matrix(row_major)

#include "ParticleGpu.hlsli"
#include "ParticleDepth.hlsli"

cbuffer GpuParticleStep : register(b1)
{
    uint stepSpawnCount;
    uint stepReset;
    uint stepCapacity;
    uint stepReaction;
    uint stepUseAttrib;
    uint stepCollide;
    uint stepFlags; // bit 0: rest lifetime is child1.y, not collide1.w
    uint stepPad;
};

StructuredBuffer<GpuParticle>          gPrev     : register(t0);
Texture2D<float>                       gDepth    : register(t1);
Texture2D<float4>                      gAttrib   : register(t2);
RWStructuredBuffer<GpuParticle>        gCurr     : register(u0);
RWStructuredBuffer<GpuSpawnEvent>      gEvents   : register(u1);
RWStructuredBuffer<uint>               gCounter  : register(u2);
RWStructuredBuffer<uint>               gChildHead : register(u3);

// particleBirthSample's xorshift32. A zero state stays zero, so callers start at 1.
float birth01(inout uint state)
{
    uint x = state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    state = x;
    return float(x >> 8) * (1.0 / 16777216.0);
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

uint birthSeed(uint slot)
{
    return sim0.z ^ (slot * 0x9E3779B9u) ^ (sim0.y * 0x85EBCA6Bu);
}

GpuParticle birthParticle(uint slot)
{
    uint seed = birthSeed(slot);
    uint state = seed == 0u ? 1u : seed;

    GpuParticle p = (GpuParticle)0;
    p.maxLife = lerp(lifeSpeed.x, lifeSpeed.y, birth01(state));
    if (p.maxLife < 0.05)
        p.maxLife = 0.05;
    p.life = p.maxLife;

    float3 localPos = float3(0, 0, 0);
    if (sim0.w == 1u)
    {
        localPos = float3((birth01(state) * 2.0 - 1.0) * shapeSize.x,
                          (birth01(state) * 2.0 - 1.0) * shapeSize.y,
                          (birth01(state) * 2.0 - 1.0) * shapeSize.z);
    }
    else if (sim0.w == 2u)
    {
        float r = shapeSize.x * pow(birth01(state), 1.0 / 3.0);
        float th = birth01(state) * 6.28318530718;
        float z = birth01(state) * 2.0 - 1.0;
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
        float cosTheta = cosMax + (1.0 - cosMax) * birth01(state);
        float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));
        float phi = birth01(state) * 6.28318530718;
        float3 helper = (abs(axis.y) < 0.99) ? float3(0, 1, 0) : float3(1, 0, 0);
        float3 tangent = cross(axis, helper);
        float tLen = length(tangent);
        tangent = (tLen < 1e-5) ? float3(1, 0, 0) : tangent / tLen;
        float3 bitangent = cross(axis, tangent);
        localDir = axis * cosTheta + tangent * (sinTheta * cos(phi)) + bitangent * (sinTheta * sin(phi));
        float dLen = length(localDir);
        localDir = (dLen < 1e-5) ? axis : localDir / dLen;
    }

    float speed = lerp(lifeSpeed.z, lifeSpeed.w, birth01(state));
    float3 pos = emitterPos.xyz + quatRotate(emitterRot, localPos);
    float3 vel = quatRotate(emitterRot, localDir) * speed;

    p.position0 = pos.x;
    p.position1 = pos.y;
    p.position2 = pos.z;
    p.velocity0 = vel.x;
    p.velocity1 = vel.y;
    p.velocity2 = vel.z;
    p.size0 = lerp(sizes.x, sizes.y, birth01(state));
    p.size1 = lerp(sizes.z, sizes.w, birth01(state));
    p.rotation = birth01(state) * 6.28318530718;
    p.color0 = sim1.x;
    p.color1 = sim1.y;
    p.flags = kGpuParticleAlive;
    p.seed = seed;
    p.rest = 0;
    return p;
}

float dispatchRest()
{
    if ((stepFlags & 1u) != 0u)
        return asfloat(child1.y);
    return collide1.w;
}

void writePos(inout GpuParticle p, float3 pos)
{
    p.position0 = pos.x;
    p.position1 = pos.y;
    p.position2 = pos.z;
}

void writeVel(inout GpuParticle p, float3 vel)
{
    p.velocity0 = vel.x;
    p.velocity1 = vel.y;
    p.velocity2 = vel.z;
}

void applyStop(inout GpuParticle p, float3 pos)
{
    writePos(p, pos);
    writeVel(p, float3(0, 0, 0));
    float restLife = dispatchRest();
    if (restLife <= 0.0)
    {
        p.flags = 0;
        p.rest = 0;
        return;
    }
    p.flags = kGpuParticleAlive | kGpuParticleRest;
    p.rest = restLife;
}

void applyCollision(inout GpuParticle p)
{
    if (stepCollide == 0u)
        return;
    float3 pos = float3(p.position0, p.position1, p.position2);
    float3 vel = float3(p.velocity0, p.velocity1, p.velocity2);
    ParticleUv uv = particleProjectUv(pos, viewProj);
    if (!uv.inside)
        return;
    int width = (int)collide0.y;
    int height = (int)collide0.z;
    if (width < 1 || height < 1)
        return;
    int2 pix = int2(uv.u * collide0.y, uv.v * collide0.z);
    if (pix.x < 0 || pix.y < 0 || pix.x >= width || pix.y >= height)
        return;

    // Stop and SpawnSecondary are one depth tap. Bounce adds a normal.
    float d = gDepth.Load(int3(pix, 0));
    if (IsSkyDepth(d))
        return;
    float nearZ = collide0.x;
    float surfZ = LinearizeViewZ(d, nearZ);
    float particleViewZ = mul(float4(pos, 1), view).z;
    float speed = length(vel);
    if (!particleDepthHit(particleViewZ, surfZ, nearZ, collide0.w, collide1.x, speed, sim2.w))
        return;

    float resolvedZ = particleResolveViewZ(surfZ, collide0.w);
    float3 resolved = pos - cameraLook.xyz * (particleViewZ - resolvedZ);
    float3 impactVel = vel;

    if (stepReaction == kReactStop)
    {
        applyStop(p, resolved);
        return;
    }

    if (stepReaction == kReactSpawn)
    {
        writePos(p, resolved);
        writeVel(p, float3(0, 0, 0));
        p.flags = 0;
        p.rest = 0;
        uint maxSec = collide2.w;
        if (maxSec == 0u)
            return;
        uint index = 0;
        InterlockedAdd(gCounter[0], 1, index);
        uint cap = maxSec;
        if (cap > 8192u)
            cap = 8192u;
        // Index stays inside this frame's half. Do not write the other slot.
        if (index < cap)
        {
            GpuSpawnEvent e;
            e.position0 = resolved.x;
            e.position1 = resolved.y;
            e.position2 = resolved.z;
            e.velocity0 = impactVel.x;
            e.velocity1 = impactVel.y;
            e.velocity2 = impactVel.z;
            e.packedNormal = particlePackOct(cameraLook.xyz);
            e.seed = p.seed;
            gEvents[index] = e;
        }
        return;
    }

    if (stepReaction != kReactBounce)
    {
        writePos(p, resolved);
        return;
    }

    bool gotNormal = false;
    float3 n = float3(0, 0, 1);
    if (stepUseAttrib != 0u)
    {
        float4 attrib = gAttrib.Load(int3(pix, 0));
        n = DecodeOct(attrib.rg);
        if (dot(n, cameraPos.xyz - resolved) < 0)
            n = -n;
        gotNormal = true;
    }
    else
        gotNormal = particleFiniteNormal(gDepth, pix, width, height, d, invViewProj, cameraPos.xyz, n);

    if (!gotNormal)
    {
        applyStop(p, resolved);
        return;
    }

    float3 bounced = particleReflectVelocity(vel, n, collide1.y, collide1.z);
    // Below 0.15 m/s, Bounce becomes Stop for the rest of this particle's life.
    if (length(bounced) < 0.15)
    {
        applyStop(p, resolved);
        return;
    }
    writePos(p, resolved);
    writeVel(p, bounced);
    p.flags = kGpuParticleAlive;
    p.rest = 0;
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
        else if ((p.flags & kGpuParticleRest) != 0u)
        {
            p.rest -= h;
            if (p.rest <= 0.0)
                p.flags = 0;
        }
        else
        {
            p.velocity0 += gravity.x * h;
            p.velocity1 += gravity.y * h;
            p.velocity2 += gravity.z * h;
            p.position0 += p.velocity0 * h;
            p.position1 += p.velocity1 * h;
            p.position2 += p.velocity2 * h;
            applyCollision(p);
        }
    }
    gCurr[slot] = p;
}

[numthreads(64, 1, 1)]
void CSSimulate(uint3 id : SV_DispatchThreadID)
{
    uint slot = id.x;
    if (slot >= stepCapacity)
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
    if (slot >= stepCapacity)
        return;
    GpuParticle p = (GpuParticle)0;
    if (stepReset == 0u)
        p = gCurr[slot];
    integrateLoaded(slot, p);
}

// One thread, one InterlockedAdd. No groupshared: cs_5_0 cannot publish a base across groups.
[numthreads(64, 1, 1)]
void CSConsume(uint3 id : SV_DispatchThreadID)
{
    uint childCap = child1.x;
    if (childCap == 0u)
        return;
    uint maxSec = collide2.w;
    if (maxSec > 8192u)
        maxSec = 8192u;
    uint count = gCounter[0];
    uint limit = count;
    if (maxSec < limit)
        limit = maxSec;
    if (childCap < limit)
        limit = childCap;
    if (id.x >= limit)
        return;

    uint slot = 0;
    InterlockedAdd(gChildHead[0], 1, slot);
    slot %= childCap;

    GpuSpawnEvent e = gEvents[id.x];
    uint state = e.seed == 0u ? 1u : e.seed;
    float maxLife = lerp(child0.x, child0.y, birth01(state));
    if (maxLife < 0.05)
        maxLife = 0.05;
    float size = lerp(child0.z, child0.w, birth01(state));

    GpuParticle p = (GpuParticle)0;
    p.position0 = e.position0;
    p.position1 = e.position1;
    p.position2 = e.position2;
    p.velocity0 = e.velocity0;
    p.velocity1 = e.velocity1;
    p.velocity2 = e.velocity2;
    p.life = maxLife;
    p.maxLife = maxLife;
    p.size0 = size;
    p.size1 = size;
    p.color0 = sim1.x;
    p.color1 = sim1.y;
    p.flags = kGpuParticleAlive;
    p.seed = e.seed;
    gCurr[slot] = p;
}
