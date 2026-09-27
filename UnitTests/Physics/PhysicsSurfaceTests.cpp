#include <gtest/gtest.h>

#include "Physics/PhysicsSurface.h"

using namespace Dark::Math;
using namespace Dark::Physics;

namespace
{
    constexpr const char* kCatalogJson = R"({
  "version": 1,
  "surfaces": [
    { "id": "default", "friction": 0.6, "restitution": 0.0, "density": 1.0, "rolling": 0.0,
      "tags": ["generic"], "footstep": "audio/foot_dirt.wav" },
    { "id": "wood",    "friction": 0.5, "restitution": 0.05, "density": 0.7, "tags": ["wood"],
      "footstep": "audio/foot_wood.wav" },
    { "id": "metal",   "friction": 0.4, "restitution": 0.2, "density": 3.0, "tags": ["metal"] },
    { "id": "water",   "friction": 0.0, "restitution": 0.0, "density": 0.0, "sensor": true,
      "tags": ["water"] },
    { "id": "hurt",    "friction": 0.6, "restitution": 0.0, "density": 0.0, "sensor": true,
      "tags": ["damage"], "damagePerSecond": 10 }
  ]
})";
} // namespace

TEST(PhysicsSurface, ParseCatalog)
{
    PhysicsSurfaceCatalog cat;
    ASSERT_TRUE(cat.parse(kCatalogJson));
    EXPECT_EQ(cat.count(), 5u);

    const PhysicsSurface* def = cat.find("default");
    ASSERT_NE(def, nullptr);
    EXPECT_EQ(def->internId, 1u);
    EXPECT_NEAR(def->friction, 0.6f, 1.0e-5f);
    EXPECT_NEAR(def->density, 1.0f, 1.0e-5f);
    EXPECT_FALSE(def->sensor);
    ASSERT_EQ(def->tags.size(), 1u);
    EXPECT_EQ(def->tags[0], "generic");
    EXPECT_EQ(def->footstep, "audio/foot_dirt.wav");

    const PhysicsSurface* wood = cat.find("wood");
    ASSERT_NE(wood, nullptr);
    EXPECT_NEAR(wood->friction, 0.5f, 1.0e-5f);
    EXPECT_NEAR(wood->restitution, 0.05f, 1.0e-5f);
    EXPECT_NEAR(wood->density, 0.7f, 1.0e-5f);

    const PhysicsSurface* water = cat.find("water");
    ASSERT_NE(water, nullptr);
    EXPECT_TRUE(water->sensor);
    EXPECT_NEAR(water->density, 0.0f, 1.0e-5f);

    const PhysicsSurface* hurt = cat.find("hurt");
    ASSERT_NE(hurt, nullptr);
    EXPECT_TRUE(hurt->sensor);
    EXPECT_NEAR(hurt->damagePerSecond, 10.0f, 1.0e-5f);

    EXPECT_EQ(cat.idOf("metal"), 3u);
    EXPECT_EQ(cat.idOf("missing"), kNullPhysicsSurfaceId);
    EXPECT_EQ(cat.find("missing"), nullptr);
}

TEST(PhysicsSurface, UserMaterialIdRoundTrip)
{
    PhysicsSurfaceCatalog cat;
    ASSERT_TRUE(cat.parse(kCatalogJson));

    const PhysicsSurface* wood = cat.find("wood");
    ASSERT_NE(wood, nullptr);
    const uint64_t umid = wood->userMaterialId();
    EXPECT_EQ(umid, static_cast<uint64_t>(wood->internId));

    const PhysicsSurface* again = cat.findByUserMaterialId(umid);
    ASSERT_NE(again, nullptr);
    EXPECT_EQ(again->id, "wood");
    EXPECT_EQ(again, wood);
    EXPECT_EQ(cat.find(wood->internId), wood);

    EXPECT_EQ(cat.findByUserMaterialId(0), nullptr);
    EXPECT_EQ(cat.find(kNullPhysicsSurfaceId), nullptr);
}

TEST(PhysicsSurface, BadJsonDoesNotThrow)
{
    PhysicsSurfaceCatalog cat;
    ASSERT_TRUE(cat.parse(kCatalogJson));
    const uint32_t before = cat.count();

    EXPECT_FALSE(cat.parse("{"));
    EXPECT_FALSE(cat.parse("not json"));
    EXPECT_FALSE(cat.parse("null"));
    EXPECT_FALSE(cat.parse("[]"));
    EXPECT_FALSE(cat.parse(""));
    EXPECT_FALSE(cat.parse(R"({ "version": 2, "surfaces": [] })"));
    EXPECT_FALSE(cat.parse(R"({ "version": 1 })"));
    EXPECT_FALSE(cat.parse(R"({ "version": 1, "surfaces": "wood" })"));
    EXPECT_FALSE(cat.parse(R"({ "version": 1, "surfaces": [ { "id": "wood" }, { "id": "wood" } ] })"));

    EXPECT_EQ(cat.count(), before);
    EXPECT_NE(cat.find("wood"), nullptr);
}

TEST(PhysicsSurface, WrongFieldTypesKeepDefaults)
{
    PhysicsSurfaceCatalog cat;
    ASSERT_TRUE(cat.parse(R"({
      "version": 1,
      "surfaces": [
        { "id": "slip", "friction": "ice", "sensor": "yes", "tags": "wood" }
      ]
    })"));

    const PhysicsSurface* s = cat.find("slip");
    ASSERT_NE(s, nullptr);
    EXPECT_NEAR(s->friction, 0.6f, 1.0e-5f);
    EXPECT_FALSE(s->sensor);
    EXPECT_TRUE(s->tags.empty());
    EXPECT_NE(cat.find("default"), nullptr);
}

TEST(PhysicsSurface, OptionalFields)
{
    PhysicsSurfaceCatalog cat;
    ASSERT_TRUE(cat.parse(R"({
      "version": 1,
      "surfaces": [
        { "id": "belt", "climbable": true, "customColor": "#ff8800",
          "tangentVelocity": [1.5, 0, 0] }
      ]
    })"));

    const PhysicsSurface* s = cat.find("belt");
    ASSERT_NE(s, nullptr);
    EXPECT_TRUE(s->climbable);
    EXPECT_EQ(s->customColor, 0x00ff8800u);
    EXPECT_NEAR(s->tangentVelocity.x, 1.5f, 1.0e-5f);
    EXPECT_NEAR(s->tangentVelocity.y, 0.0f, 1.0e-5f);
    EXPECT_NEAR(s->tangentVelocity.z, 0.0f, 1.0e-5f);
}

TEST(PhysicsSurface, LoadFromContent)
{
    PhysicsSurfaceCatalog cat;
    ASSERT_TRUE(cat.loadFromContent());
    EXPECT_GE(cat.count(), 5u);

    const PhysicsSurface& def = cat.getDefault();
    EXPECT_EQ(def.id, "default");
    EXPECT_NEAR(def.friction, 0.6f, 1.0e-5f);

    const PhysicsSurface* wood = cat.find("wood");
    ASSERT_NE(wood, nullptr);
    const PhysicsSurface* roundTrip = cat.findByUserMaterialId(wood->userMaterialId());
    ASSERT_NE(roundTrip, nullptr);
    EXPECT_EQ(roundTrip->id, "wood");

    const PhysicsSurface* water = cat.find("water");
    ASSERT_NE(water, nullptr);
    EXPECT_TRUE(water->sensor);
}
