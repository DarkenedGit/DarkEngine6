#include <gtest/gtest.h>

#include "Render/Camera3D.h"
#include "Render/DrawList.h"
#include "Render/Frustum3f.h"
#include "Render/GpuModel.h"
#include "Render/GpuResourceCache.h"
#include "Render/PassTable.h"

#include <d3d12.h>
#include <dxgi1_4.h>

#include <cstring>
#include <new>
#include <vector>

using Microsoft::WRL::ComPtr;

using namespace Dark;
using namespace Dark::Math;

namespace
{

RenderItem staticItem(const Mesh& mesh, AssetID material, StaticPopulation population, float red)
{
    RenderItem item;
    item.mesh         = &mesh;
    item.material     = material;
    item.population   = population;
    item.kind         = RenderItemKind::Static;
    item.worldAabb    = AABox3f::Empty();
    item.color[0]     = red;
    item.color[1]     = 1.0f;
    item.color[2]     = 1.0f;
    item.color[3]     = 0.0f;
    return item;
}

void storeMatrix(float dst[16], const Matrix4f& matrix)
{
    for (int i = 0; i < 16; ++i)
        dst[i] = matrix[i];
}

Frustum3f cameraFrustum(const Vector3f& eye, const Vector3f& at, float farZ)
{
    Camera3D camera;
    camera.SetLens(1.0f, 1.0f, 0.1f, farZ);
    camera.LookAt(eye, at, Vector3f(0.0f, 1.0f, 0.0f));
    return Frustum3f(camera.GetCullViewProj());
}

const GBufferBatch* batchForMesh(const DrawList& list, const Mesh& mesh)
{
    for (uint32_t i = 0; i < list.gbufferBatchCount(); ++i)
    {
        if (list.gbufferBatch(i).mesh == &mesh)
            return &list.gbufferBatch(i);
    }
    return nullptr;
}

const DepthBatch* depthForMesh(const DrawList& list, const Mesh& mesh)
{
    for (uint32_t i = 0; i < list.depthBatchCount(); ++i)
    {
        if (list.depthBatch(i).mesh == &mesh)
            return &list.depthBatch(i);
    }
    return nullptr;
}

std::vector<PassId> idsExcept(const PassNode* table, uint32_t count, PassId skip)
{
    std::vector<PassId> ids;
    for (uint32_t i = 0; i < count; ++i)
    {
        if (table[i].id != skip)
            ids.push_back(table[i].id);
    }
    return ids;
}

} // namespace

TEST(DrawList, SortKeyIsProcessLocalNotAnAddress)
{
    Mesh blank;
    EXPECT_EQ(blank.sortKey(), 0u);

    alignas(Mesh) unsigned char storage[2][sizeof(Mesh)];
    Mesh* higherAddress = new (storage[1]) Mesh(Mesh::takeSortIdentity());
    Mesh* lowerAddress  = new (storage[0]) Mesh(Mesh::takeSortIdentity());
    EXPECT_LT(reinterpret_cast<uintptr_t>(lowerAddress), reinterpret_cast<uintptr_t>(higherAddress));
    EXPECT_NE(higherAddress->sortKey(), 0u);
    EXPECT_LT(higherAddress->sortKey(), lowerAddress->sortKey());
    EXPECT_NE(higherAddress->sortKey(), static_cast<uint32_t>(reinterpret_cast<uintptr_t>(higherAddress)));
    EXPECT_NE(lowerAddress->sortKey(), static_cast<uint32_t>(reinterpret_cast<uintptr_t>(lowerAddress)));

    RenderItem lowKey  = staticItem(*higherAddress, 1, StaticPopulation::OpaqueMeshes, 1.0f);
    RenderItem highKey = staticItem(*lowerAddress, 1, StaticPopulation::OpaqueMeshes, 1.0f);
    EXPECT_TRUE(batchKeyLess(batchKeyOf(lowKey), batchKeyOf(highKey)));
    EXPECT_FALSE(batchKeyLess(batchKeyOf(highKey), batchKeyOf(lowKey)));

    DrawList list;
    list.beginFrame(0);
    ASSERT_TRUE(list.submit(highKey));
    ASSERT_TRUE(list.submit(lowKey));
    EXPECT_EQ(list.gbufferBatchCount(), 2u);
    EXPECT_EQ(list.itemAt(0).mesh, higherAddress);
    EXPECT_EQ(list.itemAt(1).mesh, lowerAddress);

    higherAddress->~Mesh();
    lowerAddress->~Mesh();
}

TEST(DrawList, SameMeshAndMaterialIsOneBatch)
{
    Mesh cube = Mesh::takeSortIdentity();
    DrawList list;
    list.beginFrame(0);
    ASSERT_TRUE(list.submit(staticItem(cube, 4, StaticPopulation::OpaqueMeshes, 0.2f)));
    ASSERT_TRUE(list.submit(staticItem(cube, 4, StaticPopulation::OpaqueMeshes, 0.8f)));
    ASSERT_TRUE(list.submit(staticItem(cube, 4, StaticPopulation::Projectiles, 0.8f)));

    EXPECT_EQ(list.gbufferBatchCount(), 2u);
    const GBufferBatch& cubes = list.gbufferBatch(0);
    EXPECT_EQ(cubes.itemCount, 2u);
    EXPECT_EQ(cubes.key.population, static_cast<uint32_t>(StaticPopulation::OpaqueMeshes));
    EXPECT_EQ(cubes.key.meshKey, cube.sortKey());
    EXPECT_FLOAT_EQ(list.itemAt(cubes.itemBegin).color[0], 0.2f);
    EXPECT_FLOAT_EQ(list.itemAt(cubes.itemBegin + 1).color[0], 0.8f);
    EXPECT_EQ(list.gbufferBatch(1).key.population, static_cast<uint32_t>(StaticPopulation::Projectiles));
    EXPECT_EQ(list.gbufferBatch(1).key.meshKey, cube.sortKey());
}

TEST(DrawList, MaskedAndOpaqueShareOneDepthPrefix)
{
    Mesh meshA = Mesh::takeSortIdentity();
    Mesh meshB = Mesh::takeSortIdentity();
    DrawList list;
    list.beginFrame(0);
    RenderItem masked = staticItem(meshA, 2, StaticPopulation::OpaqueMeshes, 1.0f);
    masked.alphaMask  = true;
    RenderItem other  = staticItem(meshB, 1, StaticPopulation::OpaqueMeshes, 1.0f);
    RenderItem opaque = staticItem(meshA, 3, StaticPopulation::OpaqueMeshes, 1.0f);
    ASSERT_TRUE(list.submit(masked));
    ASSERT_TRUE(list.submit(other));
    ASSERT_TRUE(list.submit(opaque));

    EXPECT_EQ(list.gbufferBatchCount(), 3u);
    const GBufferBatch* aOpaque = nullptr;
    const GBufferBatch* aMasked = nullptr;
    for (uint32_t i = 0; i < list.gbufferBatchCount(); ++i)
    {
        const GBufferBatch& batch = list.gbufferBatch(i);
        if (batch.mesh != &meshA)
            continue;
        if (batch.key.alphaMask == 0)
            aOpaque = &batch;
        else
            aMasked = &batch;
    }
    ASSERT_NE(aOpaque, nullptr);
    ASSERT_NE(aMasked, nullptr);
    EXPECT_EQ(aMasked->itemCount, 1u);
    EXPECT_EQ(aOpaque->itemCount, 1u);
    EXPECT_LT(aOpaque->itemBegin, aMasked->itemBegin);

    const DepthBatch* depthA = depthForMesh(list, meshA);
    ASSERT_NE(depthA, nullptr);
    EXPECT_EQ(depthA->itemBegin, aOpaque->itemBegin);
    EXPECT_EQ(depthA->itemCount, aOpaque->itemCount + aMasked->itemCount);
    const DepthBatch* depthB = depthForMesh(list, meshB);
    ASSERT_NE(depthB, nullptr);
    EXPECT_EQ(depthB->itemCount, 1u);
}

TEST(DrawList, StableMeshRunsKeepSubmissionOrder)
{
    Mesh cube  = Mesh::takeSortIdentity();
    Mesh sphere = Mesh::takeSortIdentity();
    Mesh cross = Mesh::takeSortIdentity();
    DrawList list;
    list.beginFrame(0);
    ASSERT_TRUE(list.submit(staticItem(cube, 1, StaticPopulation::OpaqueMeshes, 0.1f)));
    ASSERT_TRUE(list.submit(staticItem(sphere, 1, StaticPopulation::OpaqueMeshes, 0.2f)));
    ASSERT_TRUE(list.submit(staticItem(cross, 1, StaticPopulation::OpaqueMeshes, 0.3f)));
    ASSERT_TRUE(list.submit(staticItem(cube, 1, StaticPopulation::OpaqueMeshes, 0.4f)));

    EXPECT_EQ(list.gbufferBatchCount(), 3u);
    const GBufferBatch* cubes = batchForMesh(list, cube);
    ASSERT_NE(cubes, nullptr);
    EXPECT_EQ(cubes->itemCount, 2u);
    EXPECT_FLOAT_EQ(list.itemAt(cubes->itemBegin).color[0], 0.1f);
    EXPECT_FLOAT_EQ(list.itemAt(cubes->itemBegin + 1).color[0], 0.4f);
    EXPECT_EQ(batchForMesh(list, sphere)->itemCount, 1u);
    EXPECT_EQ(batchForMesh(list, cross)->itemCount, 1u);
    EXPECT_LT(cubes->itemBegin, batchForMesh(list, sphere)->itemBegin);
    EXPECT_LT(batchForMesh(list, sphere)->itemBegin, batchForMesh(list, cross)->itemBegin);
}

TEST(DrawList, WorldAabbCullKeepsInvalidBoxes)
{
    Mesh cube   = Mesh::takeSortIdentity();
    Mesh sphere = Mesh::takeSortIdentity();
    const AABox3f local(Vector3f(-0.5f, -0.5f, -0.5f), Vector3f(0.5f, 0.5f, 0.5f));
    const Matrix4f placed = Matrix4f::TranslationMatrix(0.0f, 0.0f, 8.0f);

    DrawList list;
    list.beginFrame(0);
    RenderItem cubeItem = staticItem(cube, 1, StaticPopulation::OpaqueMeshes, 1.0f);
    cubeItem.worldAabb  = local.Transformed(placed);
    RenderItem sphereItem = staticItem(sphere, 1, StaticPopulation::OpaqueMeshes, 1.0f);
    sphereItem.worldAabb = AABox3f::Empty();
    ASSERT_TRUE(list.submit(cubeItem));
    ASSERT_TRUE(list.submit(sphereItem));

    const float identity[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
    const Frustum3f aimed = cameraFrustum(Vector3f(0.0f, 0.0f, 4.0f), Vector3f(0.0f, 0.0f, 8.0f), 30.0f);
    const Frustum3f origin = cameraFrustum(Vector3f(0.0f, 0.0f, 0.0f), Vector3f(0.0f, 0.0f, 1.0f), 2.0f);
    list.buildView(aimed, identity, identity, RenderPassKind::GBuffer, ViewId{ 0 });
    list.buildView(origin, identity, identity, RenderPassKind::GBuffer, ViewId{ 1 });

    const uint32_t cubeBatch = 0;
    const uint32_t sphereBatch = 1;
    EXPECT_EQ(list.gbufferBatch(cubeBatch).mesh, &cube);
    EXPECT_EQ(list.gbufferBatch(sphereBatch).mesh, &sphere);
    EXPECT_EQ(list.visibleCount(ViewId{ 0 }, cubeBatch), 1u);
    EXPECT_EQ(list.visibleCount(ViewId{ 0 }, sphereBatch), 1u);
    EXPECT_EQ(list.visibleCount(ViewId{ 1 }, cubeBatch), 0u);
    EXPECT_EQ(list.visibleCount(ViewId{ 1 }, sphereBatch), 1u);
}

TEST(DrawList, ShadowViewDoesNotReuseCameraSurvivors)
{
    Mesh mesh = Mesh::takeSortIdentity();
    const AABox3f local(Vector3f(-0.5f, -0.5f, -0.5f), Vector3f(0.5f, 0.5f, 0.5f));
    DrawList list;
    list.beginFrame(0);
    RenderItem nearItem = staticItem(mesh, 1, StaticPopulation::OpaqueModels, 1.0f);
    nearItem.worldAabb  = local.Transformed(Matrix4f::TranslationMatrix(0.0f, 0.0f, 8.0f));
    RenderItem farItem  = staticItem(mesh, 1, StaticPopulation::OpaqueModels, 1.0f);
    farItem.worldAabb   = local.Transformed(Matrix4f::TranslationMatrix(0.0f, 0.0f, 40.0f));
    ASSERT_TRUE(list.submit(nearItem));
    ASSERT_TRUE(list.submit(farItem));

    const float identity[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
    const Frustum3f camera = cameraFrustum(Vector3f(0.0f, 0.0f, 0.0f), Vector3f(0.0f, 0.0f, 1.0f), 80.0f);
    const Frustum3f shadow = cameraFrustum(Vector3f(0.0f, 0.0f, 0.0f), Vector3f(0.0f, 0.0f, 1.0f), 12.0f);
    list.buildView(camera, identity, identity, RenderPassKind::GBuffer, ViewId{ 0 });
    list.buildView(shadow, identity, identity, RenderPassKind::Depth, ViewId{ 1 });

    EXPECT_EQ(list.visibleCount(ViewId{ 0 }, 0), 2u);
    EXPECT_EQ(list.visibleCount(ViewId{ 1 }, 0), 1u);
    EXPECT_EQ(list.indexAt(ViewId{ 1 }, list.depthBatch(0).itemBegin), 0u);
}

TEST(DrawList, StaticModelBakesLocalToRootAndRollsBack)
{
    const Matrix4f local  = Matrix4f::ScaleMatrixXYZ(2.0f, 0.5f, 4.0f);
    const Matrix4f entity = Matrix4f::TranslationMatrix(10.0f, -3.0f, 1.0f);
    const Matrix4f prevEntity = Matrix4f::TranslationMatrix(-4.0f, 0.5f, 0.0f);
    EXPECT_NE(local * entity, entity * local);

    GpuModel::Part part;
    part.mesh        = Mesh::takeSortIdentity();
    part.localToRoot = local;
    part.materialId  = NULL_ASSET;
    std::vector<GpuModel::Part> parts;
    parts.push_back(std::move(part));
    GpuModel model(std::move(parts), {});

    const AABox3f asset(Vector3f(-0.5f, -0.5f, -0.5f), Vector3f(0.5f, 0.5f, 0.5f));
    const Matrix4f view = Matrix4f::TranslationMatrix(0.0f, 1.0f, 0.0f);
    const Matrix4f prevView = Matrix4f::TranslationMatrix(0.0f, 0.0f, 2.0f);
    float viewF[16];
    float prevF[16];
    storeMatrix(viewF, view);
    storeMatrix(prevF, prevView);

    GpuResourceCache gpu(nullptr);
    DrawList list;
    list.beginFrame(2);
    ASSERT_TRUE(list.submitStaticModel(gpu, model, entity, StaticPopulation::OpaqueModels, true, asset, &prevEntity));
    const Frustum3f wide = cameraFrustum(Vector3f(0.0f, 0.0f, 0.0f), Vector3f(0.0f, 0.0f, 1.0f), 100.0f);
    list.buildView(wide, viewF, prevF, RenderPassKind::GBuffer, ViewId{ 0 });

    const RenderItem& item = list.itemAt(0);
    const Matrix4f baked = local * entity;
    const Matrix4f bakedPrev = local * prevEntity;
    for (int i = 0; i < 16; ++i)
    {
        EXPECT_FLOAT_EQ(item.world[i], baked[i]);
        EXPECT_FLOAT_EQ(item.prevWorld[i], bakedPrev[i]);
        EXPECT_FLOAT_EQ(list.drawViewProj(ViewId{ 0 })[i], viewF[i]);
        EXPECT_FLOAT_EQ(list.drawPrevViewProj(ViewId{ 0 })[i], prevF[i]);
    }
    const AABox3f expectedBox = asset.Transformed(entity);
    EXPECT_FLOAT_EQ(item.worldAabb.Min.x, expectedBox.Min.x);
    EXPECT_FLOAT_EQ(item.worldAabb.Min.y, expectedBox.Min.y);
    EXPECT_FLOAT_EQ(item.worldAabb.Min.z, expectedBox.Min.z);
    EXPECT_FLOAT_EQ(item.worldAabb.Max.x, expectedBox.Max.x);

    const Vector4f point(0.25f, -0.5f, 0.75f, 1.0f);
    const Vector4f clip = view * (item.world * point);
    const Vector4f expectedClip = view * (baked * point);
    const Vector4f prevClip = prevView * (item.prevWorld * point);
    const Vector4f expectedPrev = prevView * (bakedPrev * point);
    for (int i = 0; i < 4; ++i)
    {
        EXPECT_NEAR(clip[i], expectedClip[i], 1.0e-5f);
        EXPECT_NEAR(prevClip[i], expectedPrev[i], 1.0e-5f);
    }

    DrawList full;
    full.beginFrame(3);
    Mesh filler = Mesh::takeSortIdentity();
    for (uint32_t i = 0; i < kMaxStaticItems - 1; ++i)
        ASSERT_TRUE(full.submit(staticItem(filler, 1, StaticPopulation::OpaqueMeshes, 1.0f)));
    GpuModel::Part first;
    GpuModel::Part second;
    first.mesh         = Mesh::takeSortIdentity();
    second.mesh        = Mesh::takeSortIdentity();
    first.localToRoot  = Matrix4f::IDENTITY;
    second.localToRoot = Matrix4f::IDENTITY;
    std::vector<GpuModel::Part> overflow;
    overflow.push_back(std::move(first));
    overflow.push_back(std::move(second));
    GpuModel tooBig(std::move(overflow), {});
    EXPECT_FALSE(full.submitStaticModel(gpu, tooBig, Matrix4f::IDENTITY, StaticPopulation::OpaqueModels, true, AABox3f::Empty(), nullptr));
    EXPECT_EQ(full.staticCount(), kMaxStaticItems - 1);
}

TEST(DrawList, TranslucentSortsFartherFirstAndDoesNotBatch)
{
    Camera3D camera;
    camera.SetLens(1.0f, 1.0f, 0.1f, 100.0f);
    camera.LookAt(Vector3f(0.0f, 0.0f, 0.0f), Vector3f(0.0f, 0.0f, 1.0f), Vector3f(0.0f, 1.0f, 0.0f));

    TranslucentDraw draws[3];
    draws[0].worldAabb = AABox3f(Vector3f(-0.5f, -0.5f, 1.5f), Vector3f(0.5f, 0.5f, 2.5f));
    draws[0].user      = 0;
    draws[1].worldAabb = AABox3f(Vector3f(-0.5f, -0.5f, 9.5f), Vector3f(0.5f, 0.5f, 10.5f));
    draws[1].user      = 1;
    draws[2].worldAabb = draws[1].worldAabb;
    draws[2].user      = 2;
    sortTranslucent(draws, 3, camera.GetView());
    EXPECT_EQ(draws[0].user, 1u);
    EXPECT_EQ(draws[1].user, 2u);
    EXPECT_EQ(draws[2].user, 0u);

    Mesh mesh = Mesh::takeSortIdentity();
    DrawList list;
    list.beginFrame(0);
    RenderItem blend = staticItem(mesh, 1, StaticPopulation::OpaqueModels, 1.0f);
    blend.kind       = RenderItemKind::Translucent;
    EXPECT_FALSE(list.submit(blend));
    EXPECT_EQ(list.staticCount(), 0u);
    EXPECT_EQ(list.gbufferBatchCount(), 0u);
}

TEST(DrawList, PassSequenceSkipsOptionalAndRejectsReorder)
{
    uint32_t deferredCount = 0;
    const PassNode* deferred = sandboxDeferredPasses(deferredCount);
    const std::vector<PassId> withoutWater = idsExcept(deferred, deferredCount, PassId::Water);
    EXPECT_TRUE(passSequenceOk(deferred, deferredCount, withoutWater.data(), static_cast<uint32_t>(withoutWater.size())));

    const std::vector<PassId> withoutGBuffer = idsExcept(deferred, deferredCount, PassId::GBuffer);
    EXPECT_FALSE(passSequenceOk(deferred, deferredCount, withoutGBuffer.data(), static_cast<uint32_t>(withoutGBuffer.size())));

    std::vector<PassId> swapped = withoutWater;
    ASSERT_GE(swapped.size(), 2u);
    std::swap(swapped[0], swapped[1]);
    const std::vector<PassId> swappedCopy = swapped;
    EXPECT_FALSE(passSequenceOk(deferred, deferredCount, swapped.data(), static_cast<uint32_t>(swapped.size())));
    EXPECT_EQ(swapped, swappedCopy);

    uint32_t forwardCount = 0;
    const PassNode* forward = sandboxForwardPasses(forwardCount);
    const PassId gbuffer = PassId::GBuffer;
    EXPECT_FALSE(passSequenceOk(forward, forwardCount, &gbuffer, 1));

    uint32_t editorForwardCount = 0;
    const PassNode* editorForward = editorForwardPasses(editorForwardCount);
    const PassId editorOrder[] = { PassId::EditorGrid, PassId::EditorGizmos, PassId::Translucent, PassId::Post };
    EXPECT_TRUE(passSequenceOk(editorForward, editorForwardCount, editorOrder, 4));
    PassId editorReversed[] = { PassId::EditorGizmos, PassId::EditorGrid, PassId::Translucent, PassId::Post };
    const PassId reversedCopy[] = { PassId::EditorGizmos, PassId::EditorGrid, PassId::Translucent, PassId::Post };
    EXPECT_FALSE(passSequenceOk(editorForward, editorForwardCount, editorReversed, 4));
    EXPECT_EQ(std::memcmp(editorReversed, reversedCopy, sizeof(reversedCopy)), 0);
}

TEST(DrawList, ThreeKeyInterleaveSecondBatchBaseIsNotZero)
{
    Mesh meshA = Mesh::takeSortIdentity();
    Mesh meshB = Mesh::takeSortIdentity();
    Mesh meshC = Mesh::takeSortIdentity();
    DrawList list;
    list.beginFrame(0);
    RenderItem b = staticItem(meshB, 1, StaticPopulation::OpaqueModels, 0.2f);
    RenderItem a0 = staticItem(meshA, 1, StaticPopulation::OpaqueModels, 0.4f);
    RenderItem c = staticItem(meshC, 1, StaticPopulation::OpaqueModels, 0.6f);
    RenderItem a1 = staticItem(meshA, 1, StaticPopulation::OpaqueModels, 0.8f);
    EXPECT_TRUE(list.submit(b));
    EXPECT_TRUE(list.submit(a0));
    EXPECT_TRUE(list.submit(c));
    EXPECT_TRUE(list.submit(a1));

    ASSERT_EQ(list.gbufferBatchCount(), 3u);
    EXPECT_EQ(list.gbufferBatch(0).mesh, &meshA);
    EXPECT_EQ(list.gbufferBatch(0).itemBegin, 0u);
    EXPECT_EQ(list.gbufferBatch(0).itemCount, 2u);
    EXPECT_EQ(list.gbufferBatch(1).mesh, &meshB);
    EXPECT_NE(list.gbufferBatch(1).itemBegin, 0u);
    EXPECT_EQ(list.gbufferBatch(1).itemBegin, 2u);
    EXPECT_EQ(list.gbufferBatch(2).mesh, &meshC);
    EXPECT_EQ(list.gbufferBatch(2).itemBegin, 3u);
    EXPECT_NEAR(list.itemAt(0).color[0], 0.4f, 1e-5f);
    EXPECT_NEAR(list.itemAt(1).color[0], 0.8f, 1e-5f);

    float ident[16] = {};
    ident[0] = ident[5] = ident[10] = ident[15] = 1.0f;
    const Frustum3f frustum = cameraFrustum(Vector3f(0.0f, 0.0f, 0.0f), Vector3f(0.0f, 0.0f, 1.0f), 50.0f);
    list.buildView(frustum, ident, ident, RenderPassKind::GBuffer, ViewId{ 0 });
    EXPECT_EQ(list.indexAt(ViewId{ 0 }, list.gbufferBatch(0).itemBegin), 0u);
    EXPECT_EQ(list.indexAt(ViewId{ 0 }, list.gbufferBatch(1).itemBegin), 2u);
    EXPECT_EQ(list.indexAt(ViewId{ 0 }, list.gbufferBatch(2).itemBegin), 3u);
}

TEST(DrawList, MaskedDepthPrefixBaseIndexIsNotZero)
{
    Mesh meshA = Mesh::takeSortIdentity();
    Mesh meshB = Mesh::takeSortIdentity();
    DrawList list;
    list.beginFrame(0);
    RenderItem opaque = staticItem(meshA, 1, StaticPopulation::OpaqueModels, 0.1f);
    RenderItem masked = staticItem(meshA, 2, StaticPopulation::OpaqueModels, 0.2f);
    masked.alphaMask = true;
    RenderItem other = staticItem(meshB, 1, StaticPopulation::OpaqueModels, 0.3f);
    EXPECT_TRUE(list.submit(opaque));
    EXPECT_TRUE(list.submit(masked));
    EXPECT_TRUE(list.submit(other));

    ASSERT_EQ(list.gbufferBatchCount(), 3u);
    EXPECT_EQ(list.gbufferBatch(1).itemBegin, 1u);
    EXPECT_NE(list.gbufferBatch(1).itemBegin, 0u);
    ASSERT_EQ(list.depthBatchCount(), 2u);
    EXPECT_EQ(list.depthBatch(0).itemBegin, 0u);
    EXPECT_EQ(list.depthBatch(0).itemCount, 2u);
    EXPECT_EQ(list.depthBatch(1).meshKey, meshB.sortKey());
    EXPECT_NE(list.depthBatch(1).itemBegin, 0u);
    EXPECT_EQ(list.depthBatch(1).itemBegin, 2u);

    float ident[16] = {};
    ident[0] = ident[5] = ident[10] = ident[15] = 1.0f;
    const Frustum3f frustum = cameraFrustum(Vector3f(0.0f, 0.0f, 0.0f), Vector3f(0.0f, 0.0f, 1.0f), 50.0f);
    list.buildView(frustum, ident, ident, RenderPassKind::Depth, ViewId{ 1 });
    EXPECT_EQ(list.indexAt(ViewId{ 1 }, list.depthBatch(1).itemBegin), 2u);
    EXPECT_EQ(list.visibleCount(ViewId{ 1 }, 0), 2u);
}

ComPtr<ID3D12Device> tryWarpDevice()
{
    ComPtr<ID3D12Device> device;
    ComPtr<IDXGIFactory4> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
        return {};
    ComPtr<IDXGIAdapter> warp;
    if (SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp))))
    {
        if (SUCCEEDED(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))))
            return device;
    }
    return {};
}

TEST(DrawList, UploadKeepsSortedSlotAndRecords)
{
    ComPtr<ID3D12Device> device = tryWarpDevice();
    if (!device)
        GTEST_SKIP() << "no WARP device at feature level 11_0";

    Mesh meshA = Mesh::takeSortIdentity();
    Mesh meshB = Mesh::takeSortIdentity();
    DrawList list;
    ASSERT_TRUE(list.create(device.Get()));
    EXPECT_TRUE(list.gpuReady());

    list.beginFrame(0);
    RenderItem later = staticItem(meshB, NULL_ASSET, StaticPopulation::OpaqueModels, 0.2f);
    later.world = Matrix4f::TranslationMatrix(4.0f, 0.0f, 0.0f);
    RenderItem first = staticItem(meshA, NULL_ASSET, StaticPopulation::OpaqueModels, 0.7f);
    first.world = Matrix4f::TranslationMatrix(-2.0f, 1.0f, 3.0f);
    first.prevWorld = Matrix4f::TranslationMatrix(-1.0f, 0.0f, 0.0f);
    EXPECT_TRUE(list.submit(later));
    EXPECT_TRUE(list.submit(first));

    float ident[16] = {};
    ident[0] = ident[5] = ident[10] = ident[15] = 1.0f;
    const Frustum3f frustum = cameraFrustum(Vector3f(0.0f, 0.0f, 0.0f), Vector3f(0.0f, 0.0f, 1.0f), 80.0f);
    list.buildView(frustum, ident, ident, RenderPassKind::GBuffer, ViewId{ 0 });
    list.buildView(frustum, ident, ident, RenderPassKind::Depth, ViewId{ 1 });

    const MeshInstanceGpu* slot0 = list.uploadedInstances();
    ASSERT_NE(slot0, nullptr);
    EXPECT_EQ(list.gbufferBatch(0).mesh, &meshA);
    EXPECT_NE(list.gbufferBatch(1).itemBegin, 0u);
    EXPECT_NEAR(slot0[0].color[0], 0.7f, 1e-5f);
    EXPECT_NEAR(slot0[0].world[12], -2.0f, 1e-5f);
    EXPECT_NEAR(slot0[0].prevWorld[12], -1.0f, 1e-5f);
    EXPECT_NEAR(slot0[1].color[0], 0.2f, 1e-5f);
    EXPECT_NEAR(slot0[1].world[12], 4.0f, 1e-5f);

    const uint32_t* indices = list.uploadedIndices(ViewId{ 0 });
    ASSERT_NE(indices, nullptr);
    EXPECT_EQ(indices[list.gbufferBatch(1).itemBegin], list.indexAt(ViewId{ 0 }, list.gbufferBatch(1).itemBegin));
    EXPECT_EQ(indices[0], 0u);
    EXPECT_EQ(indices[1], 1u);

    const uint32_t* depthIndices = list.uploadedIndices(ViewId{ 1 });
    ASSERT_NE(depthIndices, nullptr);
    EXPECT_EQ(depthIndices[list.depthBatch(1).itemBegin], 1u);

    GpuResourceCache gpu(nullptr);
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> cmd;
    ASSERT_TRUE(SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))));
    ASSERT_TRUE(SUCCEEDED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&cmd))));
    list.recordOpaque(cmd.Get(), gpu, ViewId{ 0 }, StaticPopulation::OpaqueModels);
    list.recordOpaque(cmd.Get(), gpu, ViewId{ 0 }, StaticPopulation::Projectiles);
    list.recordDepth(cmd.Get(), ViewId{ 1 }, ident);
    list.recordDepth(nullptr, ViewId{ 1 }, ident);
    EXPECT_TRUE(SUCCEEDED(cmd->Close()));

    list.beginFrame(1);
    RenderItem only = staticItem(meshA, NULL_ASSET, StaticPopulation::Projectiles, 0.9f);
    EXPECT_TRUE(list.submit(only));
    list.buildView(frustum, ident, ident, RenderPassKind::GBuffer, ViewId{ 0 });
    const MeshInstanceGpu* slot1 = list.uploadedInstances();
    ASSERT_NE(slot1, nullptr);
    EXPECT_NE(slot1, slot0);
    EXPECT_NEAR(slot1[0].color[0], 0.9f, 1e-5f);
    EXPECT_NEAR(slot0[0].color[0], 0.7f, 1e-5f);
}

TEST(DrawList, IndirectArgsKeepZeroStartInstance)
{
    ComPtr<ID3D12Device> device = tryWarpDevice();
    if (!device)
        GTEST_SKIP() << "no WARP device at feature level 11_0";

    Mesh meshA = Mesh::takeSortIdentity();
    Mesh meshB = Mesh::takeSortIdentity();
    DrawList list;
    ASSERT_TRUE(list.create(device.Get()));
    EXPECT_TRUE(list.gpuReady());
    ASSERT_TRUE(list.indirectReady());

    list.beginFrame(0);
    RenderItem later = staticItem(meshB, NULL_ASSET, StaticPopulation::OpaqueModels, 0.2f);
    RenderItem first = staticItem(meshA, NULL_ASSET, StaticPopulation::OpaqueModels, 0.7f);
    EXPECT_TRUE(list.submit(later));
    EXPECT_TRUE(list.submit(first));

    float ident[16] = {};
    ident[0] = ident[5] = ident[10] = ident[15] = 1.0f;
    const Frustum3f frustum = cameraFrustum(Vector3f(0.0f, 0.0f, 0.0f), Vector3f(0.0f, 0.0f, 1.0f), 80.0f);
    list.buildView(frustum, ident, ident, RenderPassKind::GBuffer, ViewId{ 0 });
    list.buildView(frustum, ident, ident, RenderPassKind::Depth, ViewId{ 1 });

    ASSERT_GE(list.gbufferBatchCount(), 2u);
    EXPECT_EQ(list.gbufferBatch(0).mesh, &meshA);
    EXPECT_EQ(list.gbufferBatch(1).mesh, &meshB);
    EXPECT_NE(list.gbufferBatch(1).itemBegin, 0u);

    const DrawIndexedArgs& arg0 = list.drawArg(ViewId{ 0 }, 0);
    const DrawIndexedArgs& arg1 = list.drawArg(ViewId{ 0 }, 1);
    EXPECT_EQ(arg0.startInstanceLocation, 0u);
    EXPECT_EQ(arg1.startInstanceLocation, 0u);
    EXPECT_NE(arg1.startInstanceLocation, list.gbufferBatch(1).itemBegin);
    EXPECT_EQ(arg0.startIndexLocation, 0u);
    EXPECT_EQ(arg1.startIndexLocation, 0u);
    EXPECT_EQ(arg0.baseVertexLocation, 0);
    EXPECT_EQ(arg1.baseVertexLocation, 0);
    EXPECT_EQ(arg0.instanceCount, list.visibleCount(ViewId{ 0 }, 0));
    EXPECT_EQ(arg1.instanceCount, list.visibleCount(ViewId{ 0 }, 1));
    EXPECT_EQ(arg0.indexCountPerInstance, meshA.indexCount());
    EXPECT_EQ(arg1.indexCountPerInstance, meshB.indexCount());

    ASSERT_GE(list.depthBatchCount(), 2u);
    EXPECT_NE(list.depthBatch(1).itemBegin, 0u);
    const DrawIndexedArgs& depth1 = list.drawArg(ViewId{ 1 }, 1);
    EXPECT_EQ(depth1.startInstanceLocation, 0u);
    EXPECT_EQ(depth1.baseVertexLocation, 0);
    EXPECT_EQ(depth1.instanceCount, list.visibleCount(ViewId{ 1 }, 1));
    EXPECT_EQ(depth1.indexCountPerInstance, meshB.indexCount());

    GpuResourceCache gpu(nullptr);
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> cmd;
    ASSERT_TRUE(SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))));
    ASSERT_TRUE(SUCCEEDED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&cmd))));
    list.recordOpaque(cmd.Get(), gpu, ViewId{ 0 }, StaticPopulation::OpaqueModels);
    list.recordOpaque(cmd.Get(), gpu, ViewId{ 0 }, StaticPopulation::Projectiles);
    list.recordDepth(cmd.Get(), ViewId{ 1 }, ident);
    ASSERT_TRUE(SUCCEEDED(cmd->Close()));

    list.setIndirect(false);
    list.beginFrame(0);
    EXPECT_TRUE(list.submit(later));
    EXPECT_TRUE(list.submit(first));
    list.buildView(frustum, ident, ident, RenderPassKind::GBuffer, ViewId{ 0 });
    list.buildView(frustum, ident, ident, RenderPassKind::Depth, ViewId{ 1 });
    EXPECT_EQ(list.drawArg(ViewId{ 0 }, 1).startInstanceLocation, 0u);
    EXPECT_EQ(list.drawArg(ViewId{ 0 }, 1).instanceCount, list.visibleCount(ViewId{ 0 }, 1));
    EXPECT_NE(list.gbufferBatch(1).itemBegin, 0u);

    ASSERT_TRUE(SUCCEEDED(allocator->Reset()));
    ASSERT_TRUE(SUCCEEDED(cmd->Reset(allocator.Get(), nullptr)));
    list.recordOpaque(cmd.Get(), gpu, ViewId{ 0 }, StaticPopulation::OpaqueModels);
    list.recordDepth(cmd.Get(), ViewId{ 1 }, ident);
    ASSERT_TRUE(SUCCEEDED(cmd->Close()));
}
