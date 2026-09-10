#include <gtest/gtest.h>
#include <type_traits>
#include <utility>

#include "Math/DarkMath.h"

using namespace Dark::Math;

namespace
{
    template <typename T>
    constexpr bool IsNothrowMovable = std::is_nothrow_move_constructible_v<T> && std::is_nothrow_move_assignable_v<T>;

    template <typename T>
    constexpr bool IsTriviallyCopyableValue = std::is_trivially_copyable_v<T> && std::is_trivially_copy_constructible_v<T> &&
                                              std::is_trivially_move_constructible_v<T> && std::is_trivially_copy_assignable_v<T> &&
                                              std::is_trivially_move_assignable_v<T>;
} // namespace

TEST(MathMoveSemantics, ValueTypesAreTrivialAndNothrowMovable)
{
    static_assert(IsNothrowMovable<Vector2f> && IsTriviallyCopyableValue<Vector2f>);
    static_assert(IsNothrowMovable<Vector3f> && IsTriviallyCopyableValue<Vector3f>);
    static_assert(IsNothrowMovable<Vector4f> && IsTriviallyCopyableValue<Vector4f>);
    static_assert(IsNothrowMovable<Matrix3f> && IsTriviallyCopyableValue<Matrix3f>);
    static_assert(IsNothrowMovable<Matrix4f> && IsTriviallyCopyableValue<Matrix4f>);
    static_assert(IsNothrowMovable<Quaternion> && IsTriviallyCopyableValue<Quaternion>);
    static_assert(IsNothrowMovable<Plane4f> && IsTriviallyCopyableValue<Plane4f>);
    static_assert(IsNothrowMovable<AABox2f> && IsTriviallyCopyableValue<AABox2f>);
    static_assert(IsNothrowMovable<AABox3f> && IsTriviallyCopyableValue<AABox3f>);
    static_assert(IsNothrowMovable<Box2f> && IsTriviallyCopyableValue<Box2f>);
    static_assert(IsNothrowMovable<Box3f> && IsTriviallyCopyableValue<Box3f>);
    static_assert(IsNothrowMovable<Ray2f> && IsTriviallyCopyableValue<Ray2f>);
    static_assert(IsNothrowMovable<Ray3f> && IsTriviallyCopyableValue<Ray3f>);
    static_assert(IsNothrowMovable<Sphere2f> && IsTriviallyCopyableValue<Sphere2f>);
    static_assert(IsNothrowMovable<Sphere3f> && IsTriviallyCopyableValue<Sphere3f>);
}

TEST(MathMoveSemantics, Vector3fMovePreservesValues)
{
    Vector3f src{1.0f, 2.0f, 3.0f};
    Vector3f dst{std::move(src)};
    EXPECT_FLOAT_EQ(dst.x, 1.0f);
    EXPECT_FLOAT_EQ(dst.y, 2.0f);
    EXPECT_FLOAT_EQ(dst.z, 3.0f);

    Vector3f assigned;
    assigned = std::move(dst);
    EXPECT_FLOAT_EQ(assigned.x, 1.0f);
    EXPECT_FLOAT_EQ(assigned.y, 2.0f);
    EXPECT_FLOAT_EQ(assigned.z, 3.0f);
}

TEST(MathMoveSemantics, Matrix4fMovePreservesValues)
{
    Matrix4f src = Matrix4f::TranslationMatrix(4.0f, 5.0f, 6.0f);
    Matrix4f dst{std::move(src)};
    EXPECT_FLOAT_EQ(dst.GetTranslation().x, 4.0f);
    EXPECT_FLOAT_EQ(dst.GetTranslation().y, 5.0f);
    EXPECT_FLOAT_EQ(dst.GetTranslation().z, 6.0f);

    Matrix4f assigned;
    assigned = std::move(dst);
    EXPECT_FLOAT_EQ(assigned.GetTranslation().x, 4.0f);
    EXPECT_FLOAT_EQ(assigned.GetTranslation().y, 5.0f);
    EXPECT_FLOAT_EQ(assigned.GetTranslation().z, 6.0f);
}

TEST(MathMoveSemantics, QuaternionMovePreservesValues)
{
    Quaternion src{0.1f, 0.2f, 0.3f, 0.4f};
    Quaternion dst{std::move(src)};
    EXPECT_FLOAT_EQ(dst.w, 0.1f);
    EXPECT_FLOAT_EQ(dst.x, 0.2f);
    EXPECT_FLOAT_EQ(dst.y, 0.3f);
    EXPECT_FLOAT_EQ(dst.z, 0.4f);

    Quaternion assigned;
    assigned = std::move(dst);
    EXPECT_FLOAT_EQ(assigned.w, 0.1f);
    EXPECT_FLOAT_EQ(assigned.x, 0.2f);
    EXPECT_FLOAT_EQ(assigned.y, 0.3f);
    EXPECT_FLOAT_EQ(assigned.z, 0.4f);
}

TEST(MathMoveSemantics, CompositeTypesMoveConstructAndAssign)
{
    AABox3f boxSrc{Vector3f{0.0f, 1.0f, 2.0f}, Vector3f{3.0f, 4.0f, 5.0f}};
    AABox3f boxDst{std::move(boxSrc)};
    EXPECT_FLOAT_EQ(boxDst.Min.x, 0.0f);
    EXPECT_FLOAT_EQ(boxDst.Max.z, 5.0f);
    AABox3f boxAssigned;
    boxAssigned = std::move(boxDst);
    EXPECT_FLOAT_EQ(boxAssigned.Min.y, 1.0f);
    EXPECT_FLOAT_EQ(boxAssigned.Max.x, 3.0f);

    Sphere3f sphereSrc{Vector3f{7.0f, 8.0f, 9.0f}, 2.5f};
    Sphere3f sphereDst{std::move(sphereSrc)};
    EXPECT_FLOAT_EQ(sphereDst.Center.x, 7.0f);
    EXPECT_FLOAT_EQ(sphereDst.Radius, 2.5f);
    Sphere3f sphereAssigned;
    sphereAssigned = std::move(sphereDst);
    EXPECT_FLOAT_EQ(sphereAssigned.Center.z, 9.0f);
    EXPECT_FLOAT_EQ(sphereAssigned.Radius, 2.5f);

    Ray2f raySrc{Vector2f{1.0f, 2.0f}, Vector2f{0.0f, 1.0f}};
    Ray2f rayDst{std::move(raySrc)};
    EXPECT_FLOAT_EQ(rayDst.Origin.x, 1.0f);
    EXPECT_FLOAT_EQ(rayDst.Direction.y, 1.0f);
    Ray2f rayAssigned;
    rayAssigned = std::move(rayDst);
    EXPECT_FLOAT_EQ(rayAssigned.Origin.y, 2.0f);
}
