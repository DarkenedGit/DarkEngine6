#pragma once

#include "ECS/Entity.h"
#include "Math/Vector3f.h"
#include "Weapons/Weapon.h"

#include <cstddef>
#include <cstdint>

namespace Dark
{

    enum class DecalKind : uint8_t
    {
        Footmark = 0,
        Blood,
        Impact,
        Burn,
        Count
    };

    enum class DecalSpace : uint8_t
    {
        World = 0,
        Entity,
        Bone
    };

    enum class DecalFade : uint8_t
    {
        Linear = 0,
        Smoothstep,
        HoldThenLinear
    };

    enum DecalChannel : uint32_t
    {
        DecalChannel_Albedo    = 1u << 0,
        DecalChannel_Normal    = 1u << 1,
        DecalChannel_Roughness = 1u << 2,
        DecalChannel_Metallic  = 1u << 3,
        DecalChannel_Emissive  = 1u << 4,
    };

    enum class DecalDefId : uint8_t
    {
        Footmark = 0,
        BloodHit,
        BloodDeath,
        ImpactBullet,
        ImpactSlash,
        Burn,
        Count
    };

    enum class DecalNormalRecipe : uint8_t
    {
        Flat = 0,
        BloodBump,
        FootDent,
        ImpactBullet,
        ImpactSlash
    };

    inline constexpr float kDecalAngleFadeStart       = 0.20f;
    inline constexpr float kDecalAngleFadeRange       = 0.25f;
    inline constexpr float kDecalShellBiasMeters      = 0.08f;
    inline constexpr float kDecalClosestBoneMeters    = 0.75f;
    inline constexpr float kDecalFootAbovePlaneMeters = 0.02f;
    inline constexpr int   kDecalNormalMapSize        = 64;

    struct DecalId
    {
        uint32_t index  = 0;
        uint32_t serial = 0; // 0 is invalid and is never assigned
    };

    struct DecalSpawnDesc
    {
        DecalKind      kind = DecalKind::Impact;
        Math::Vector3f position{ 0.0f, 0.0f, 0.0f };
        Math::Vector3f axisY{ 0.0f, 1.0f, 0.0f };       // projection axis; spawn normalizes it
        Math::Vector3f axisX{ 1.0f, 0.0f, 0.0f };       // tangent hint; orthonormalized against axisY
        Math::Vector3f halfExtents{ 0.0f, 0.0f, 0.0f }; // 0 uses the definition default
        DecalSpace     space = DecalSpace::World;
        Entity         entity{};
        int            bone          = -1; // -1 and Bone: pick the closest joint
        float          lifetimeScale = 1.0f;
        WeaponKind     weapon        = WeaponKind::Melee; // Projectile -> bullet, Melee -> slash
    };

    // Upload record. HLSL reads the same bytes; do not insert a float3 (16-byte aligned).
    struct alignas(16) DecalGpuInstance
    {
        float    worldFromDecal[16];
        float    decalFromWorld[16];
        float    tint[3];
        float    albedoWeight;
        float    normalScale;
        float    normalWeight;
        float    roughnessTarget;
        float    roughnessWeight;
        float    metallicTarget;
        float    metallicWeight;
        float    emissiveTarget;
        float    emissiveWeight;
        float    uvScaleBias[4];
        float    axisY[3];
        float    angleFadeStart;
        float    angleFadeRange;
        uint32_t channelMask;
        float    clipLocalYMin;
        float    clipLocalYMax;
    };
    static_assert(sizeof(DecalGpuInstance) == 56 * sizeof(float), "decal instance");
    static_assert(offsetof(DecalGpuInstance, channelMask) == 212, "decal channelMask");

    struct DecalFrameStats
    {
        uint32_t alive        = 0;
        uint32_t drawn        = 0;
        uint32_t culled       = 0;
        uint32_t inside       = 0;
        uint32_t triangles    = 0;
        uint32_t recycleCount = 0;
    };

} // namespace Dark
