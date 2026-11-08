// Shared GPU particle layout. Keep in lockstep with Dark::GpuParticle (64 bytes),
// Dark::GpuSpawnEvent (32 bytes), and GpuParticleConstants. No float3: HLSL pads those to 16.
#pragma pack_matrix(row_major)

struct GpuParticle
{
    float position0, position1, position2;
    float life;
    float velocity0, velocity1, velocity2;
    float maxLife;
    float size0, size1, rotation, rest;
    uint  color0, color1, flags, seed;
};

struct GpuSpawnEvent
{
    float position0, position1, position2;
    float velocity0, velocity1, velocity2;
    uint  packedNormal;
    uint  seed;
};

// Offsets: viewProj 0, cameraRight 64, cameraUp 80, baseColor 96, gravity 112,
// emitterPos 128, emitterRot 144, direction 160, shapeSize 176, lifeSpeed 192,
// sizes 208, sim0 224, sim1 240, sim2 256, invViewProj 272, view 336,
// cameraPos 400, cameraLook 416, collide0 432, collide1 448, collide2 464,
// child0 480, child1 496. Total 512.
cbuffer GpuParticleCb : register(b0)
{
    float4x4 viewProj;
    float4   cameraRight;
    float4   cameraUp;
    float4   baseColor;
    float4   gravity;
    float4   emitterPos;
    float4   emitterRot; // xyzw
    float4   direction;
    float4   shapeSize;
    float4   lifeSpeed; // life min/max, speed min/max
    float4   sizes; // start min/max, end min/max
    uint4    sim0; // capacity, head, seed, shape
    uint4    sim1; // packed color0, color1
    float4   sim2; // emissive, emissiveGain, spreadRad, substepDt
    float4x4 invViewProj;
    float4x4 view;
    float4   cameraPos;
    float4   cameraLook;
    float4   collide0; // nearZ, width, height, bias
    float4   collide1; // thickness, restitution, friction, parent restLifetime
    uint4    collide2; // reaction, useAttrib, collideEnable, maxSecondary
    float4   child0; // life min/max, start size min/max
    uint4    child1; // childCapacity, child rest bits, pad, pad
};

static const uint kGpuParticleAlive = 1;
static const uint kGpuParticleRest  = 2;

static const uint kReactNone  = 0;
static const uint kReactBounce = 1;
static const uint kReactStop  = 2;
static const uint kReactSpawn = 3;
