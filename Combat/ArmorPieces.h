#pragma once

#include <string_view>

#include "Combat/DamageEvent.h"
#include "Combat/DamageTypes.h"
#include "Combat/HitPart.h"
#include "ECS/Entity.h"
#include "Math/Quaternion.h"
#include "Math/Vector3f.h"

namespace Dark::Combat
{

    struct ArmorPiece
    {
        const char*    name        = "";
        HitPart        part        = HitPart::None;
        Math::Vector3f offset      { 0.0f, 0.0f, 0.0f };
        Math::Vector3f halfExtents { 0.1f, 0.1f, 0.1f };
        float          maxHp       = 0.0f;
        float          hp          = 0.0f;
        float          absorb      = 0.8f;
        bool           attached    = true;
        Entity         visual{};
    };

    struct ArmorPiecesComponent
    {
        static constexpr const char* kTypeName = "ArmorPieces";
        static constexpr int         kMaxPieces = 8;

        ArmorPiece pieces[kMaxPieces]{};
        int        count = 0;
    };

    struct ArmorBreak
    {
        int            index = -1;
        Math::Vector3f position{};
        Math::Vector3f push{};
    };

    inline ArmorPiecesComponent makeHunterArmor()
    {
        ArmorPiecesComponent armor;

        ArmorPiece& helmet = armor.pieces[armor.count++];
        helmet.name        = "helmet";
        helmet.part        = HitPart::Head;
        helmet.offset      = Math::Vector3f{ 0.0f, 0.75f, 0.0f };
        helmet.halfExtents = Math::Vector3f{ 0.18f, 0.16f, 0.18f };
        helmet.maxHp       = 40.0f;
        helmet.hp          = helmet.maxHp;

        ArmorPiece& chest = armor.pieces[armor.count++];
        chest.name        = "chest";
        chest.part        = HitPart::Chest;
        chest.offset      = Math::Vector3f{ 0.0f, 0.35f, 0.0f };
        chest.halfExtents = Math::Vector3f{ 0.26f, 0.24f, 0.16f };
        chest.maxHp       = 60.0f;
        chest.hp          = chest.maxHp;

        return armor;
    }

    // The pieces one worn item puts on the body. Arm and leg guards cover both limbs, so they add two pieces.
    // absorb is the share of a hit on that part the piece takes (item armor / 100).
    inline int armorPiecesForItem(std::string_view itemId, float armorRating, ArmorPiece* out, int maxOut)
    {
        int  n   = 0;
        auto add = [&](HitPart part, Math::Vector3f offset, Math::Vector3f half, float hp) {
            if (n >= maxOut)
                return;
            ArmorPiece& p = out[n++];
            p.name        = itemId == "helmet" ? "helmet" : itemId == "chest" ? "chest" : itemId == "arm_guards" ? "arm_guards" : "leg_guards";
            p.part        = part;
            p.offset      = offset;
            p.halfExtents = half;
            p.maxHp       = hp;
            p.hp          = hp;
            p.absorb      = armorRating > 0.0f ? (armorRating > 90.0f ? 0.9f : armorRating / 100.0f) : 0.5f;
        };
        if (itemId == "helmet")
            add(HitPart::Head, { 0.0f, 0.75f, 0.0f }, { 0.18f, 0.16f, 0.18f }, 40.0f);
        else if (itemId == "chest")
            add(HitPart::Chest, { 0.0f, 0.35f, 0.0f }, { 0.26f, 0.24f, 0.16f }, 60.0f);
        else if (itemId == "arm_guards")
        {
            add(HitPart::RightArm, { 0.30f, 0.35f, 0.0f }, { 0.07f, 0.17f, 0.07f }, 30.0f);
            add(HitPart::LeftArm, { -0.30f, 0.35f, 0.0f }, { 0.07f, 0.17f, 0.07f }, 30.0f);
        }
        else if (itemId == "leg_guards")
        {
            add(HitPart::RightLeg, { 0.12f, -0.25f, 0.0f }, { 0.09f, 0.20f, 0.09f }, 35.0f);
            add(HitPart::LeftLeg, { -0.12f, -0.25f, 0.0f }, { 0.09f, 0.20f, 0.09f }, 35.0f);
        }
        return n;
    }

    // Guards wear a helmet, a chest plate and leg guards.
    inline ArmorPiecesComponent makeGuardArmor()
    {
        ArmorPiecesComponent armor = makeHunterArmor();
        armor.count += armorPiecesForItem("leg_guards", 45.0f, armor.pieces + armor.count, ArmorPiecesComponent::kMaxPieces - armor.count);
        return armor;
    }

    Math::Vector3f armorPieceWorldPos(const ArmorPiece& piece, const Math::Vector3f& ownerPos, const Math::Quaternion& ownerRot);

    float absorbArmorHit(ArmorPiecesComponent& armor, const Math::Vector3f& ownerPos, const Math::Quaternion& ownerRot,
                         const Math::Vector3f& hitPoint, const Math::Vector3f& hitDir, float damage, ArmorBreak& broke,
                         HitPart part = HitPart::None, bool hasFlatArmorRating = false, uint32_t damageFlags = DamageFlags::None,
                         DamageType damageType = DamageType::Slash);

} // namespace Dark::Combat
