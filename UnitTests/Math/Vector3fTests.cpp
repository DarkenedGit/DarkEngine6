#include <gtest/gtest.h>
#include "Math/Vector3f.h"
#include "Math/MathHelper.h"

using namespace Dark::Math;

namespace
{
bool Near(const Vector3f& a, const Vector3f& b, float eps = 1.0e-5f)
{
    return NearEqual(a.x, b.x, eps) && NearEqual(a.y, b.y, eps) && NearEqual(a.z, b.z, eps);
}
} // namespace

TEST(Vector3f, ConstructionAndConstants)
{
    Vector3f v{1.0f, 2.0f, 3.0f};
    EXPECT_FLOAT_EQ(v.x, 1.0f);
    EXPECT_FLOAT_EQ(v.y, 2.0f);
    EXPECT_FLOAT_EQ(v.z, 3.0f);

    EXPECT_TRUE(Near(Vector3f{Vector3f::ZERO}, Vector3f{0.0f, 0.0f, 0.0f}));
    EXPECT_TRUE(Near(Vector3f{Vector3f::ONE}, Vector3f{1.0f, 1.0f, 1.0f}));
    EXPECT_TRUE(Near(Vector3f{Vector3f::X_AXIS}, Vector3f{1.0f, 0.0f, 0.0f}));
}

TEST(Vector3f, Arithmetic)
{
    Vector3f a{1.0f, 2.0f, 3.0f};
    Vector3f b{4.0f, 5.0f, 6.0f};

    EXPECT_TRUE(Near(a + b, Vector3f{5.0f, 7.0f, 9.0f}));
    EXPECT_TRUE(Near(b - a, Vector3f{3.0f, 3.0f, 3.0f}));
    EXPECT_TRUE(Near(a * 2.0f, Vector3f{2.0f, 4.0f, 6.0f}));
    EXPECT_TRUE(Near(a / 2.0f, Vector3f{0.5f, 1.0f, 1.5f}));
    EXPECT_TRUE(Near(-a, Vector3f{-1.0f, -2.0f, -3.0f}));
    EXPECT_TRUE(Near(a * b, Vector3f{4.0f, 10.0f, 18.0f}));
}

TEST(Vector3f, CompoundAssignment)
{
    Vector3f v{1.0f, 1.0f, 1.0f};
    v += Vector3f{1.0f, 2.0f, 3.0f};
    EXPECT_TRUE(Near(v, Vector3f{2.0f, 3.0f, 4.0f}));
    v *= 0.5f;
    EXPECT_TRUE(Near(v, Vector3f{1.0f, 1.5f, 2.0f}));
}

TEST(Vector3f, DotCrossMagnitude)
{
    Vector3f x{1.0f, 0.0f, 0.0f};
    Vector3f y{0.0f, 1.0f, 0.0f};
    Vector3f z{0.0f, 0.0f, 1.0f};

    EXPECT_FLOAT_EQ(x.Dot(y), 0.0f);
    EXPECT_FLOAT_EQ(x.Dot(x), 1.0f);
    EXPECT_TRUE(Near(x.Cross(y), z));
    EXPECT_TRUE(Near(y.Cross(z), x));
    EXPECT_TRUE(Near(z.Cross(x), y));

    Vector3f v{3.0f, 4.0f, 0.0f};
    EXPECT_FLOAT_EQ(v.Magnitude(), 5.0f);
    EXPECT_FLOAT_EQ(v.MagnitudeSqrd(), 25.0f);
}

TEST(Vector3f, Normalize)
{
    Vector3f v{0.0f, 3.0f, 4.0f};
    v.Normalize();
    EXPECT_NEAR(v.Magnitude(), 1.0f, 1.0e-5f);
    EXPECT_TRUE(Near(v, Vector3f{0.0f, 0.6f, 0.8f}));
}

TEST(Vector3f, EqualityUsesTolerance)
{
    // operator== uses a 0.1 component absolute threshold (squared > 0.01).
    Vector3f a{1.0f, 2.0f, 3.0f};
    Vector3f b{1.05f, 2.05f, 3.05f};
    EXPECT_TRUE(a == b);

    Vector3f c{1.2f, 2.0f, 3.0f};
    EXPECT_TRUE(a != c);
}

TEST(Vector3f, Indexer)
{
    Vector3f v{9.0f, 8.0f, 7.0f};
    EXPECT_FLOAT_EQ(v[0], 9.0f);
    EXPECT_FLOAT_EQ(v[1], 8.0f);
    EXPECT_FLOAT_EQ(v[2], 7.0f);
    v[1] = 1.5f;
    EXPECT_FLOAT_EQ(v.y, 1.5f);
}
