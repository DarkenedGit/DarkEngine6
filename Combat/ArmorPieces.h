#pragma once

#include "ECS/Entity.h"
#include "Math/Quaternion.h"
#include "Math/Vector3f.h"

namespace Dark::Combat
{

    struct ArmorPiece
    {
        const char*    name        = "";
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
        static constexpr int         kMaxPieces = 4;

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
        helmet.offset      = Math::Vector3f{ 0.0f, 0.75f, 0.0f };
        helmet.halfExtents = Math::Vector3f{ 0.18f, 0.16f, 0.18f };
        helmet.maxHp       = 40.0f;
        helmet.hp          = helmet.maxHp;

        ArmorPiece& chest = armor.pieces[armor.count++];
        chest.name        = "chest";
        chest.offset      = Math::Vector3f{ 0.0f, 0.35f, 0.0f };
        chest.halfExtents = Math::Vector3f{ 0.26f, 0.24f, 0.16f };
        chest.maxHp       = 60.0f;
        chest.hp          = chest.maxHp;

        return armor;
    }

    Math::Vector3f armorPieceWorldPos(const ArmorPiece& piece, const Math::Vector3f& ownerPos, const Math::Quaternion& ownerRot);

    float absorbArmorHit(ArmorPiecesComponent& armor, const Math::Vector3f& ownerPos, const Math::Quaternion& ownerRot,
                         const Math::Vector3f& hitPoint, const Math::Vector3f& hitDir, float damage, ArmorBreak& broke);

} // namespace Dark::Combat
