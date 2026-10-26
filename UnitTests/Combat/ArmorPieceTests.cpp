#include <gtest/gtest.h>

#include "Combat/ArmorPieces.h"

using namespace Dark;
using namespace Dark::Combat;
using namespace Dark::Math;

namespace
{

    const Vector3f kHitDir{ 0.0f, 0.0f, 1.0f };

    ArmorPiecesComponent helmetAndChest()
    {
        ArmorPiecesComponent armor;

        ArmorPiece& helmet = armor.pieces[armor.count++];
        helmet.part        = HitPart::Head;
        helmet.offset      = Vector3f{ 0.0f, 0.75f, 0.0f };
        helmet.absorb      = 0.8f;
        helmet.hp          = 40.0f;
        helmet.attached    = true;

        ArmorPiece& chest = armor.pieces[armor.count++];
        chest.part        = HitPart::Chest;
        chest.offset      = Vector3f{ 0.0f, 0.35f, 0.0f };
        chest.absorb      = 0.8f;
        chest.hp          = 60.0f;
        chest.attached    = true;
        return armor;
    }

    float absorbAt(ArmorPiecesComponent& armor, ArmorBreak& broke, HitPart part, float hitY, float damage = 10.0f,
                   bool hasFlatArmorRating = false, uint32_t damageFlags = DamageFlags::None, DamageType damageType = DamageType::Slash)
    {
        return absorbArmorHit(armor, Vector3f::ZERO, Quaternion::IDENTITY, Vector3f{ 0.0f, hitY, 0.0f }, kHitDir, damage, broke, part,
                              hasFlatArmorRating, damageFlags, damageType);
    }

    void expectPush(const Vector3f& push)
    {
        EXPECT_FLOAT_EQ(push.x, kHitDir.x);
        EXPECT_FLOAT_EQ(push.y, kHitDir.y);
        EXPECT_FLOAT_EQ(push.z, kHitDir.z);
    }

} // namespace

TEST(ArmorPiece, HeadHitSpendsHelmetAndReturnsRemainder)
{
    ArmorPiecesComponent armor = helmetAndChest();
    ArmorBreak           broke;
    const float          left = absorbAt(armor, broke, HitPart::Head, 0.35f);

    EXPECT_NEAR(armor.pieces[0].hp, 32.0f, 1.0e-4f);
    EXPECT_TRUE(armor.pieces[0].attached);
    EXPECT_NEAR(left, 2.0f, 1.0e-4f);
    EXPECT_FLOAT_EQ(armor.pieces[1].hp, 60.0f);
    EXPECT_TRUE(armor.pieces[1].attached);
    EXPECT_EQ(broke.index, -1);
}

TEST(ArmorPiece, FlatRatingSpendsHelmetAndReturnsFullDamage)
{
    ArmorPiecesComponent armor = helmetAndChest();
    ArmorBreak           broke;
    const float          left = absorbAt(armor, broke, HitPart::Head, 0.35f, 10.0f, true);

    EXPECT_NEAR(armor.pieces[0].hp, 32.0f, 1.0e-4f);
    EXPECT_TRUE(armor.pieces[0].attached);
    EXPECT_NEAR(left, 10.0f, 1.0e-4f);
    EXPECT_FLOAT_EQ(armor.pieces[1].hp, 60.0f);
    EXPECT_TRUE(armor.pieces[1].attached);
    EXPECT_EQ(broke.index, -1);
}

TEST(ArmorPiece, RightArmHitsNoPiece)
{
    ArmorPiecesComponent armor = helmetAndChest();
    ArmorBreak           broke;
    const float          left = absorbAt(armor, broke, HitPart::RightArm, 0.35f);

    EXPECT_FLOAT_EQ(armor.pieces[0].hp, 40.0f);
    EXPECT_TRUE(armor.pieces[0].attached);
    EXPECT_FLOAT_EQ(armor.pieces[1].hp, 60.0f);
    EXPECT_TRUE(armor.pieces[1].attached);
    EXPECT_NEAR(left, 10.0f, 1.0e-4f);
    EXPECT_EQ(broke.index, -1);
}

TEST(ArmorPiece, NoPartLeavesPieces)
{
    ArmorPiecesComponent armor = helmetAndChest();
    ArmorBreak           broke;
    const float          left = absorbAt(armor, broke, HitPart::None, 0.35f);

    EXPECT_FLOAT_EQ(armor.pieces[0].hp, 40.0f);
    EXPECT_TRUE(armor.pieces[0].attached);
    EXPECT_FLOAT_EQ(armor.pieces[1].hp, 60.0f);
    EXPECT_TRUE(armor.pieces[1].attached);
    EXPECT_NEAR(left, 10.0f, 1.0e-4f);
    EXPECT_EQ(broke.index, -1);
}

TEST(ArmorPiece, DetachedHelmetIsSkipped)
{
    ArmorPiecesComponent armor = helmetAndChest();
    armor.pieces[0].attached   = false;
    ArmorBreak  broke;
    const float left = absorbAt(armor, broke, HitPart::Head, 0.35f);

    EXPECT_FLOAT_EQ(armor.pieces[0].hp, 40.0f);
    EXPECT_FALSE(armor.pieces[0].attached);
    EXPECT_FLOAT_EQ(armor.pieces[1].hp, 60.0f);
    EXPECT_TRUE(armor.pieces[1].attached);
    EXPECT_NEAR(left, 10.0f, 1.0e-4f);
    EXPECT_EQ(broke.index, -1);
}

TEST(ArmorPiece, IgnoresArmorLeavesPieces)
{
    ArmorPiecesComponent armor = helmetAndChest();
    ArmorBreak           broke;
    const float          left = absorbAt(armor, broke, HitPart::Head, 0.35f, 10.0f, false, DamageFlags::IgnoresArmor);

    EXPECT_FLOAT_EQ(armor.pieces[0].hp, 40.0f);
    EXPECT_TRUE(armor.pieces[0].attached);
    EXPECT_FLOAT_EQ(armor.pieces[1].hp, 60.0f);
    EXPECT_TRUE(armor.pieces[1].attached);
    EXPECT_NEAR(left, 10.0f, 1.0e-4f);
    EXPECT_EQ(broke.index, -1);
}

TEST(ArmorPiece, TrueDamageLeavesPieces)
{
    ArmorPiecesComponent armor = helmetAndChest();
    ArmorBreak           broke;
    const float          left = absorbAt(armor, broke, HitPart::Head, 0.35f, 10.0f, false, DamageFlags::None, DamageType::True);

    EXPECT_FLOAT_EQ(armor.pieces[0].hp, 40.0f);
    EXPECT_TRUE(armor.pieces[0].attached);
    EXPECT_FLOAT_EQ(armor.pieces[1].hp, 60.0f);
    EXPECT_TRUE(armor.pieces[1].attached);
    EXPECT_NEAR(left, 10.0f, 1.0e-4f);
    EXPECT_EQ(broke.index, -1);
}

TEST(ArmorPiece, FirstAttachedHeadPieceWins)
{
    ArmorPiecesComponent armor;
    ArmorPiece&          first = armor.pieces[armor.count++];
    first.part                 = HitPart::Head;
    first.offset               = Vector3f{ 0.0f, 0.75f, 0.0f };
    first.absorb               = 0.8f;
    first.hp                   = 40.0f;
    first.attached             = true;

    ArmorPiece& second = armor.pieces[armor.count++];
    second.part        = HitPart::Head;
    second.offset      = Vector3f{ 0.0f, 0.35f, 0.0f };
    second.absorb      = 0.5f;
    second.hp          = 10.0f;
    second.attached    = true;

    ArmorBreak  broke;
    const float left = absorbAt(armor, broke, HitPart::Head, 0.35f);

    EXPECT_NEAR(armor.pieces[0].hp, 32.0f, 1.0e-4f);
    EXPECT_NEAR(armor.pieces[1].hp, 10.0f, 1.0e-4f);
    EXPECT_NEAR(left, 2.0f, 1.0e-4f);
    EXPECT_EQ(broke.index, -1);
}

TEST(ArmorPiece, DetachedFirstUsesNextAttached)
{
    ArmorPiecesComponent armor;
    ArmorPiece&          first = armor.pieces[armor.count++];
    first.part                 = HitPart::Head;
    first.offset               = Vector3f{ 0.0f, 0.75f, 0.0f };
    first.absorb               = 0.8f;
    first.hp                   = 40.0f;
    first.attached             = false;

    ArmorPiece& second = armor.pieces[armor.count++];
    second.part        = HitPart::Head;
    second.offset      = Vector3f{ 0.0f, 0.75f, 0.0f };
    second.absorb      = 0.5f;
    second.hp          = 10.0f;
    second.attached    = true;

    ArmorBreak  broke;
    const float left = absorbAt(armor, broke, HitPart::Head, 0.35f);

    EXPECT_FALSE(armor.pieces[0].attached);
    EXPECT_FLOAT_EQ(armor.pieces[0].hp, 40.0f);
    EXPECT_NEAR(armor.pieces[1].hp, 5.0f, 1.0e-4f);
    EXPECT_TRUE(armor.pieces[1].attached);
    EXPECT_NEAR(left, 5.0f, 1.0e-4f);
}

TEST(ArmorPiece, HeightDoesNotChooseChest)
{
    ArmorPiecesComponent armor = helmetAndChest();
    ArmorBreak           broke;
    const float          left = absorbAt(armor, broke, HitPart::Head, 0.35f);

    EXPECT_NEAR(armor.pieces[0].hp, 32.0f, 1.0e-4f);
    EXPECT_TRUE(armor.pieces[0].attached);
    EXPECT_FLOAT_EQ(armor.pieces[1].hp, 60.0f);
    EXPECT_TRUE(armor.pieces[1].attached);
    EXPECT_NEAR(left, 2.0f, 1.0e-4f);
}

TEST(ArmorPiece, NonPositiveDamage)
{
    ArmorPiecesComponent armor = helmetAndChest();
    ArmorBreak           broke;
    broke.index = 2;
    const float zero = absorbAt(armor, broke, HitPart::Head, 0.35f, 0.0f);

    EXPECT_FLOAT_EQ(zero, 0.0f);
    EXPECT_EQ(broke.index, -1);
    EXPECT_FLOAT_EQ(armor.pieces[0].hp, 40.0f);
    EXPECT_FLOAT_EQ(armor.pieces[1].hp, 60.0f);

    broke.index     = 2;
    const float neg = absorbAt(armor, broke, HitPart::Head, 0.35f, -1.0f);
    EXPECT_FLOAT_EQ(neg, -1.0f);
    EXPECT_EQ(broke.index, -1);
    EXPECT_FLOAT_EQ(armor.pieces[0].hp, 40.0f);
    EXPECT_FLOAT_EQ(armor.pieces[1].hp, 60.0f);
    EXPECT_TRUE(armor.pieces[0].attached);
    EXPECT_TRUE(armor.pieces[1].attached);
}

TEST(ArmorPiece, BreakOffStillFires)
{
    ArmorPiecesComponent armor = helmetAndChest();
    armor.pieces[0].hp         = 4.0f;
    ArmorBreak  broke;
    const float left = absorbAt(armor, broke, HitPart::Head, 0.35f);

    EXPECT_FLOAT_EQ(armor.pieces[0].hp, 0.0f);
    EXPECT_FALSE(armor.pieces[0].attached);
    EXPECT_EQ(broke.index, 0);
    expectPush(broke.push);
    EXPECT_NEAR(left, 2.0f, 1.0e-4f);
    EXPECT_FLOAT_EQ(armor.pieces[1].hp, 60.0f);
    EXPECT_TRUE(armor.pieces[1].attached);
}

TEST(ArmorPiece, FlatRatingCanBreakOff)
{
    ArmorPiecesComponent armor = helmetAndChest();
    armor.pieces[0].hp         = 4.0f;
    ArmorBreak  broke;
    const float left = absorbAt(armor, broke, HitPart::Head, 0.35f, 10.0f, true);

    EXPECT_FLOAT_EQ(armor.pieces[0].hp, 0.0f);
    EXPECT_FALSE(armor.pieces[0].attached);
    EXPECT_EQ(broke.index, 0);
    expectPush(broke.push);
    EXPECT_NEAR(left, 10.0f, 1.0e-4f);
    EXPECT_FLOAT_EQ(armor.pieces[1].hp, 60.0f);
    EXPECT_TRUE(armor.pieces[1].attached);
}

TEST(ArmorPiece, HunterArmorBindsHeadAndChest)
{
    const ArmorPiecesComponent armor = makeHunterArmor();
    EXPECT_EQ(armor.count, 2);
    EXPECT_EQ(armor.pieces[0].part, HitPart::Head);
    EXPECT_EQ(armor.pieces[1].part, HitPart::Chest);
}
