#include "Combat/ArmorPieces.h"

namespace Dark::Combat
{

    Math::Vector3f armorPieceWorldPos(const ArmorPiece& piece, const Math::Vector3f& ownerPos, const Math::Quaternion& ownerRot)
    {
        return ownerPos + ownerRot.Rotate(piece.offset);
    }

    float absorbArmorHit(ArmorPiecesComponent& armor, const Math::Vector3f& ownerPos, const Math::Quaternion& ownerRot,
                         const Math::Vector3f& hitPoint, const Math::Vector3f& hitDir, float damage, ArmorBreak& broke,
                         HitPart part, bool hasFlatArmorRating, uint32_t damageFlags, DamageType damageType)
    {
        broke = {};
        if (damage <= 0.0f)
            return damage;

        // Height does not choose a piece, and HitPart::None is not a slot.
        (void)hitPoint;
        if (part == HitPart::None || (damageFlags & DamageFlags::IgnoresArmor) != 0u || isTrue(damageType))
            return damage;

        int hitIndex = -1;
        for (int i = 0; i < armor.count; ++i)
        {
            const ArmorPiece& candidate = armor.pieces[i];
            if (!candidate.attached || candidate.part != part)
                continue;
            hitIndex = i;
            break;
        }
        if (hitIndex < 0)
            return damage;

        ArmorPiece& piece = armor.pieces[hitIndex];
        piece.hp -= damage * piece.absorb;
        if (piece.hp <= 0.0f)
        {
            piece.hp       = 0.0f;
            piece.attached = false;
            broke.index    = hitIndex;
            broke.position = armorPieceWorldPos(piece, ownerPos, ownerRot);
            broke.push     = hitDir;
        }
        if (hasFlatArmorRating)
            return damage;
        return damage * (1.0f - piece.absorb);
    }

} // namespace Dark::Combat
