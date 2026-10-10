#include "Combat/ArmorPieces.h"

#include <cmath>

namespace Dark::Combat
{

    Math::Vector3f armorPieceWorldPos(const ArmorPiece& piece, const Math::Vector3f& ownerPos, const Math::Quaternion& ownerRot)
    {
        return ownerPos + ownerRot.Rotate(piece.offset);
    }

    float absorbArmorHit(ArmorPiecesComponent& armor, const Math::Vector3f& ownerPos, const Math::Quaternion& ownerRot,
                         const Math::Vector3f& hitPoint, const Math::Vector3f& hitDir, float damage, ArmorBreak& broke)
    {
        broke = {};
        if (damage <= 0.0f)
            return damage;

        const float hitY  = hitPoint.y - ownerPos.y;
        int         best  = -1;
        float       bestD = 0.0f;
        for (int i = 0; i < armor.count; ++i)
        {
            const ArmorPiece& piece = armor.pieces[i];
            const float       d     = std::fabs(hitY - piece.offset.y);
            if (!piece.attached || d > piece.halfExtents.y + 0.15f)
                continue;
            if (best < 0 || d < bestD)
            {
                best  = i;
                bestD = d;
            }
        }
        if (best < 0)
            return damage;

        ArmorPiece& piece = armor.pieces[best];
        piece.hp -= damage * piece.absorb;
        if (piece.hp <= 0.0f)
        {
            piece.hp       = 0.0f;
            piece.attached = false;
            broke.index    = best;
            broke.position = armorPieceWorldPos(piece, ownerPos, ownerRot);
            broke.push     = hitDir;
        }
        return damage * (1.0f - piece.absorb);
    }

} // namespace Dark::Combat
