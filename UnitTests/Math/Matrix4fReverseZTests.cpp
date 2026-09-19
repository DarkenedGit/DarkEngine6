#include <gtest/gtest.h>

#include "Math/Matrix4f.h"
#include "Math/Vector4f.h"

#include <cmath>

using Dark::Math::Mat4f;
using Dark::Math::Matrix4f;
using Dark::Math::Vector4f;

namespace
{
    float ndcZ(const Matrix4f& proj, float viewZ)
    {
        const Vector4f clip = proj * Vector4f(0.0f, 0.0f, viewZ, 1.0f);
        return clip.z / clip.w;
    }
} // namespace

TEST(Matrix4f, PerspectiveReverseInf_NearIsOne)
{
    const float    zn = 0.18f;
    const Matrix4f p  = Matrix4f::PerspectiveFovLHReverseInfMatrix(1.0f, 16.0f / 9.0f, zn);

    EXPECT_FLOAT_EQ(p.m_afEntry[Mat4f::m33], 0.0f);
    EXPECT_FLOAT_EQ(p.m_afEntry[Mat4f::m34], 1.0f);
    EXPECT_FLOAT_EQ(p.m_afEntry[Mat4f::m43], zn);
    EXPECT_FLOAT_EQ(p.m_afEntry[Mat4f::m44], 0.0f);

    EXPECT_NEAR(ndcZ(p, zn), 1.0f, 1.0e-5f);
    EXPECT_LT(ndcZ(p, 1.0e5f), 1.0e-4f);
    EXPECT_GT(ndcZ(p, zn), ndcZ(p, zn * 10.0f));
    EXPECT_GT(ndcZ(p, zn * 10.0f), ndcZ(p, 1.0e5f));
}

TEST(Matrix4f, PerspectiveReverse_FarIsZero)
{
    const float    zn = 0.18f;
    const float    zf = 2000.0f;
    const Matrix4f p  = Matrix4f::PerspectiveFovLHReverseMatrix(1.0f, 1.0f, zn, zf);

    EXPECT_NEAR(ndcZ(p, zf), 0.0f, 1.0e-5f);
    EXPECT_NEAR(ndcZ(p, zn), 1.0f, 1.0e-5f);
    EXPECT_GT(ndcZ(p, zn), ndcZ(p, zf));
}

TEST(Matrix4f, PerspectiveReverse_ZnEqualsZf)
{
    const Matrix4f p = Matrix4f::PerspectiveFovLHReverseMatrix(1.0f, 1.0f, 1.0f, 1.0f);
    EXPECT_TRUE(std::isfinite(p.m_afEntry[Mat4f::m33]));
    EXPECT_TRUE(std::isfinite(p.m_afEntry[Mat4f::m43]));
    EXPECT_NEAR(ndcZ(p, 1.0f), 1.0f, 1.0e-4f);
    EXPECT_NEAR(ndcZ(p, 1.1f), 0.0f, 1.0e-4f);
}

TEST(Matrix4f, OrthoReverse_NearGreaterThanFar)
{
    const Matrix4f p = Matrix4f::OrthographicLHReverseMatrix(0.1f, 100.0f, 10.0f, 10.0f);
    EXPECT_GT(ndcZ(p, 0.1f), ndcZ(p, 100.0f));
    EXPECT_NEAR(ndcZ(p, 0.1f), 1.0f, 1.0e-5f);
    EXPECT_NEAR(ndcZ(p, 100.0f), 0.0f, 1.0e-5f);
}

TEST(Matrix4f, OrthoReverse_ZnZeroIsNear)
{
    const Matrix4f p = Matrix4f::OrthographicLHReverseMatrix(0.0f, 80.0f, 10.0f, 10.0f);
    EXPECT_NEAR(ndcZ(p, 0.0f), 1.0f, 1.0e-5f);
    EXPECT_NEAR(ndcZ(p, 80.0f), 0.0f, 1.0e-5f);
}

TEST(Matrix4f, OrthoReverse_ZnEqualsZf)
{
    const Matrix4f p = Matrix4f::OrthographicLHReverseMatrix(5.0f, 5.0f, 10.0f, 10.0f);
    EXPECT_TRUE(std::isfinite(p.m_afEntry[Mat4f::m33]));
    EXPECT_TRUE(std::isfinite(p.m_afEntry[Mat4f::m43]));
    EXPECT_GT(ndcZ(p, 5.0f), ndcZ(p, 5.1f));
}

TEST(Matrix4f, OrthoReverse_NegativeZnAccepted)
{
    const Matrix4f p = Matrix4f::OrthographicLHReverseMatrix(-2.0f, 2.0f, 10.0f, 10.0f);
    EXPECT_NEAR(ndcZ(p, -2.0f), 1.0f, 1.0e-5f);
    EXPECT_NEAR(ndcZ(p, 2.0f), 0.0f, 1.0e-5f);
}

TEST(Matrix4f, OrthoOffCenterReverse_ZnZeroIsNear)
{
    const Matrix4f p = Matrix4f::OrthographicOffCenterLHReverseMatrix(-1.0f, 1.0f, -1.0f, 1.0f, 0.0f, 80.0f);
    EXPECT_NEAR(ndcZ(p, 0.0f), 1.0f, 1.0e-5f);
    EXPECT_NEAR(ndcZ(p, 80.0f), 0.0f, 1.0e-5f);
}
