// Shared GPU particle layout. Keep in lockstep with Dark::GpuParticle (64 bytes)
// and GpuParticleConstants in GpuParticleSystem.cpp. No float3: HLSL pads those to 16.
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

// Offsets: viewProj 0, cameraRight 64, cameraUp 80, baseColor 96, gravity 112,
// emitterPos 128, emitterRot 144, direction 160, shapeSize 176, lifeSpeed 192,
// sizes 208, sim0 224, sim1 240, sim2 256. Total 272. Colors are packed in sim1.
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
};

static const uint kGpuParticleAlive = 1;
