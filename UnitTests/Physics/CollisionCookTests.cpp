#include <gtest/gtest.h>
#include <memory>

#include "Assets/AssetManager.h"
#include "Assets/GltfLoader.h"
#include "Assets/Material.h"
#include "Assets/MeshData.h"
#include "Assets/Model.h"
#include "Math/AABox3f.h"
#include "Math/Matrix4f.h"
#include "Math/Vector3f.h"
#include "Physics/CollisionCook.h"
#include "Physics/PhysicsSurface.h"

using Dark::AssetManager;
using Dark::AssetType;
using Dark::GltfCpuModel;
using Dark::GltfCpuPrimitive;
using Dark::MeshData;
using Dark::Model;
using Dark::NULL_ASSET;
using Dark::Math::AABox3f;
using Dark::Math::Matrix4f;
using Dark::Math::Vector3f;
using Dark::Physics::CollisionCookDesc;
using Dark::Physics::CollisionCookKind;
using Dark::Physics::CollisionGeom;
using Dark::Physics::CollisionShape;
using Dark::Physics::CollisionSidecar;
using Dark::Physics::PhysicsSurfaceCatalog;
using Dark::Physics::cookCollisionShape;
using Dark::Physics::internCollisionShape;
using Dark::Physics::kPawnCapsuleRadius;
using Dark::Physics::parseCollisionSidecar;

namespace
{
    MeshData makeTri()
    {
        MeshData m;
        m.positions = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
        m.normals   = {{0, 0, 1}, {0, 0, 1}, {0, 0, 1}};
        m.uvs       = {{0, 0}, {1, 0}, {0, 1}};
        m.indices   = {0, 1, 2};
        return m;
    }

    MeshData makeTet()
    {
        MeshData m;
        m.positions = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
        m.indices   = {0, 1, 2, 0, 1, 3, 0, 2, 3, 1, 2, 3};
        return m;
    }

    Model::Part namedPart(std::string name, MeshData mesh, const Matrix4f& xform = Matrix4f())
    {
        Model::Part p;
        p.name        = std::move(name);
        p.mesh        = std::move(mesh);
        p.localToRoot = xform;
        return p;
    }
} // namespace

TEST(CollisionCook, CollisionNameFilter)
{
    EXPECT_TRUE(Dark::isCollisionPartName("trunk_col"));
    EXPECT_TRUE(Dark::isCollisionPartName("trunk_col / Wood"));
    EXPECT_TRUE(Dark::isCollisionPartName("col_box"));
    EXPECT_TRUE(Dark::isCollisionPartName("Tree_PHYS"));
    EXPECT_TRUE(Dark::isCollisionPartName("canopy_colconv"));
    EXPECT_TRUE(Dark::isCollisionPartName("roof_colmesh"));
    EXPECT_TRUE(Dark::isCollisionPartName("body_colcapsule"));
    EXPECT_FALSE(Dark::isCollisionPartName("wood_column"));
    EXPECT_FALSE(Dark::isCollisionPartName("Trunk"));
    EXPECT_FALSE(Dark::isCollisionPartName(""));
}

TEST(CollisionCook, ColGoesToCollisionPartsNotOpaque)
{
    Model model;
    ASSERT_TRUE(model.createFromParts({namedPart("Trunk", makeTri()), namedPart("trunk_col", makeTri())}));
    ASSERT_EQ(model.opaque().size(), 1u);
    ASSERT_EQ(model.collisionParts().size(), 1u);
    EXPECT_EQ(model.opaque()[0].name, "Trunk");
    EXPECT_FALSE(model.opaque()[0].collisionOnly);
    EXPECT_EQ(model.collisionParts()[0].name, "trunk_col");
    EXPECT_TRUE(model.collisionParts()[0].collisionOnly);
    EXPECT_EQ(model.partCount(), 1u);
    EXPECT_EQ(model.partAt(0), &model.opaque()[0]);
}

TEST(CollisionCook, ColOnlyModelStillValid)
{
    Model model;
    ASSERT_TRUE(model.createFromParts({namedPart("helper_col", makeTri())}));
    EXPECT_TRUE(model.valid());
    EXPECT_FALSE(model.hasVisual());
    EXPECT_TRUE(model.hasCollision());
    EXPECT_TRUE(model.opaque().empty());
    EXPECT_TRUE(model.translucent().empty());
    EXPECT_EQ(model.collisionParts().size(), 1u);
}

TEST(CollisionCook, ColOnlyParsedGltfStillValid)
{
    AssetManager assets;
    GltfCpuModel cpu;
    GltfCpuPrimitive p;
    p.mesh     = makeTri();
    p.meshName = "volume_col";
    cpu.primitives.push_back(p);

    Model model;
    ASSERT_TRUE(model.createFromParsed(assets, cpu, "unit:/col-only"));
    EXPECT_TRUE(model.valid());
    EXPECT_TRUE(model.opaque().empty());
    ASSERT_EQ(model.collisionParts().size(), 1u);
    EXPECT_TRUE(model.collisionParts()[0].collisionOnly);
}

TEST(CollisionCook, BoundsAreVisualOnly)
{
    Model model;
    Model::Part visual = namedPart("Trunk", makeTri());
    Model::Part col    = namedPart("far_col", makeTri(), Matrix4f::TranslationMatrix(100.0f, 0.0f, 0.0f));
    ASSERT_TRUE(model.createFromParts({visual, col}));
    EXPECT_TRUE(model.bounds().Contains(Vector3f(0.0f, 0.0f, 0.0f)));
    EXPECT_FALSE(model.bounds().Contains(Vector3f(100.0f, 0.0f, 0.0f)));
    EXPECT_FALSE(model.bounds().Contains(Vector3f(101.0f, 0.0f, 0.0f)));
}

TEST(CollisionCook, CubeHalfExtentsFromScale)
{
    CollisionCookDesc desc;
    desc.kind  = CollisionCookKind::Cube;
    desc.scale = Vector3f(2.0f, 4.0f, 6.0f);
    CollisionShape shape;
    ASSERT_TRUE(cookCollisionShape(desc, shape));
    ASSERT_EQ(shape.parts().size(), 1u);
    EXPECT_EQ(shape.parts()[0].geom, CollisionGeom::Box);
    EXPECT_NEAR(shape.parts()[0].halfExtents.x, 1.0f, 1.0e-5f);
    EXPECT_NEAR(shape.parts()[0].halfExtents.y, 2.0f, 1.0e-5f);
    EXPECT_NEAR(shape.parts()[0].halfExtents.z, 3.0f, 1.0e-5f);
}

TEST(CollisionCook, SphereRadiusFromScale)
{
    CollisionCookDesc desc;
    desc.kind  = CollisionCookKind::Sphere;
    desc.scale = Vector3f(2.0f, 1.0f, 4.0f);
    CollisionShape shape;
    ASSERT_TRUE(cookCollisionShape(desc, shape));
    ASSERT_EQ(shape.parts().size(), 1u);
    EXPECT_EQ(shape.parts()[0].geom, CollisionGeom::Sphere);
    EXPECT_NEAR(shape.parts()[0].radius, 2.0f, 1.0e-5f);
}

TEST(CollisionCook, PawnCapsuleRadius045)
{
    CollisionCookDesc desc;
    desc.kind = CollisionCookKind::Pawn;
    CollisionShape shape;
    ASSERT_TRUE(cookCollisionShape(desc, shape));
    ASSERT_EQ(shape.parts().size(), 1u);
    EXPECT_EQ(shape.parts()[0].geom, CollisionGeom::Capsule);
    EXPECT_NEAR(shape.parts()[0].radius, kPawnCapsuleRadius, 1.0e-5f);
    EXPECT_NEAR(shape.parts()[0].capsuleA.y, 0.0f, 1.0e-5f);
    EXPECT_NEAR(shape.parts()[0].capsuleB.y, 0.9f, 1.0e-5f);
}

TEST(CollisionCook, ModelOptInRequiresColOrSidecar)
{
    Model model;
    ASSERT_TRUE(model.createFromParts({namedPart("Trunk", makeTri())}));
    CollisionCookDesc desc;
    desc.kind            = CollisionCookKind::Model;
    desc.model           = &model;
    desc.loadSidecarFile = false;
    CollisionShape shape;
    EXPECT_FALSE(cookCollisionShape(desc, shape));
    EXPECT_FALSE(shape.valid());
}

TEST(CollisionCook, ModelColCooksHull)
{
    Model model;
    ASSERT_TRUE(model.createFromParts({namedPart("visual", makeTri()), namedPart("body_colconv", makeTet())}));
    CollisionCookDesc desc;
    desc.kind            = CollisionCookKind::Model;
    desc.model           = &model;
    desc.loadSidecarFile = false;
    CollisionShape shape;
    ASSERT_TRUE(cookCollisionShape(desc, shape));
    ASSERT_EQ(shape.parts().size(), 1u);
    EXPECT_EQ(shape.parts()[0].geom, CollisionGeom::Hull);
    EXPECT_GE(shape.parts()[0].hullPoints.size(), 4u);
}

TEST(CollisionCook, HullFallbackToAabbBox)
{
    Model model;
    ASSERT_TRUE(model.createFromParts({namedPart("flat_colconv", makeTri())}));
    CollisionCookDesc desc;
    desc.kind            = CollisionCookKind::Model;
    desc.model           = &model;
    desc.loadSidecarFile = false;
    CollisionShape shape;
    ASSERT_TRUE(cookCollisionShape(desc, shape));
    ASSERT_EQ(shape.parts().size(), 1u);
    EXPECT_EQ(shape.parts()[0].geom, CollisionGeom::Box);
    EXPECT_GT(shape.parts()[0].halfExtents.x, 0.0f);
    EXPECT_TRUE(shape.parts()[0].hullPoints.empty());
}

TEST(CollisionCook, SidecarParseAndCook)
{
    PhysicsSurfaceCatalog surfaces;
    ASSERT_TRUE(surfaces.parse(R"({ "version": 1, "surfaces": [ { "id": "default" }, { "id": "wood", "friction": 0.5 } ] })"));

    Model model;
    ASSERT_TRUE(model.createFromParts({namedPart("trunk_col", makeTet())}));

    CollisionSidecar sidecar;
    ASSERT_TRUE(parseCollisionSidecar(R"({
      "version": 1,
      "root": {
        "body": "static",
        "parts": [
          { "node": "trunk_col", "geom": "capsule", "surface": "wood", "category": "World" }
        ]
      }
    })", sidecar));
    EXPECT_EQ(sidecar.parts.size(), 1u);
    EXPECT_EQ(sidecar.parts[0].node, "trunk_col");
    EXPECT_EQ(sidecar.parts[0].geom, "capsule");
    EXPECT_EQ(sidecar.parts[0].surface, "wood");

    CollisionCookDesc desc;
    desc.kind            = CollisionCookKind::Model;
    desc.model           = &model;
    desc.loadSidecarFile = false;
    desc.sidecarJson     = R"({
      "version": 1,
      "root": {
        "body": "static",
        "parts": [
          { "node": "trunk_col", "geom": "capsule", "surface": "wood", "category": "World" }
        ]
      }
    })";
    desc.surfaces = &surfaces;
    CollisionShape shape;
    ASSERT_TRUE(cookCollisionShape(desc, shape));
    ASSERT_EQ(shape.parts().size(), 1u);
    EXPECT_EQ(shape.parts()[0].geom, CollisionGeom::Capsule);
    EXPECT_EQ(shape.parts()[0].surfaceId, surfaces.idOf("wood"));
}

TEST(CollisionCook, SidecarBadJsonDoesNotThrow)
{
    CollisionSidecar sidecar;
    sidecar.version = 7;
    sidecar.body    = "keep";
    EXPECT_FALSE(parseCollisionSidecar("{", sidecar));
    EXPECT_FALSE(parseCollisionSidecar("not json", sidecar));
    EXPECT_FALSE(parseCollisionSidecar("null", sidecar));
    EXPECT_FALSE(parseCollisionSidecar("[]", sidecar));
    EXPECT_FALSE(parseCollisionSidecar(R"({ "version": 2, "root": { "parts": [] } })", sidecar));
    EXPECT_EQ(sidecar.version, 7);
    EXPECT_EQ(sidecar.body, "keep");
}

TEST(CollisionCook, InternCollisionShapeType)
{
    AssetManager assets;
    CollisionCookDesc desc;
    desc.kind = CollisionCookKind::Cube;
    auto shape = std::make_shared<CollisionShape>();
    ASSERT_TRUE(cookCollisionShape(desc, *shape));
    auto interned = internCollisionShape(assets, shape, "physics:/unit-cube");
    ASSERT_TRUE(interned);
    EXPECT_NE(interned->id, NULL_ASSET);
    EXPECT_EQ(interned->type, AssetType::CollisionShape);
}
