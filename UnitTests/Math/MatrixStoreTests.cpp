#include <gtest/gtest.h>

#include "ECS/Components.h"
#include "Math/Matrix4f.h"

using namespace Dark;
using namespace Dark::Math;

TEST(Matrix4f, StoreCopiesRowMajorEntries)
{
    const Matrix4f m = Matrix4f::TranslationMatrix(1.5f, -2.0f, 3.25f);
    float          dst[16]{};
    for (int i = 0; i < 16; ++i)
        dst[i] = -1.0f;
    m.store(dst);
    for (int i = 0; i < 16; ++i)
        EXPECT_FLOAT_EQ(dst[i], m.m_afEntry[i]);
    EXPECT_FLOAT_EQ(dst[Mat4f::m41], 1.5f);
    EXPECT_FLOAT_EQ(dst[Mat4f::m42], -2.0f);
    EXPECT_FLOAT_EQ(dst[Mat4f::m43], 3.25f);
}

TEST(TransformComponent, WorldMatrixIsScaleThenRotationThenTranslation)
{
    TransformComponent xf;
    xf.position = Vector3f(5.0f, 6.0f, 7.0f);
    xf.scale    = Vector3f(2.0f, 3.0f, 4.0f);
    xf.rotation = Quaternion(0.9238795f, 0.0f, 0.3826834f, 0.0f);

    const Matrix4f got = worldMatrix(xf);
    const Matrix4f expect = Matrix4f::ScaleMatrixXYZ(xf.scale.x, xf.scale.y, xf.scale.z) * xf.rotation.ToMatrix4()
        * Matrix4f::TranslationMatrix(xf.position.x, xf.position.y, xf.position.z);
    for (int i = 0; i < 16; ++i)
        EXPECT_FLOAT_EQ(got.m_afEntry[i], expect.m_afEntry[i]);

    EXPECT_NEAR(got.m_afEntry[Mat4f::m41], 5.0f, 1.0e-4f);
    EXPECT_NEAR(got.m_afEntry[Mat4f::m42], 6.0f, 1.0e-4f);
    EXPECT_NEAR(got.m_afEntry[Mat4f::m43], 7.0f, 1.0e-4f);
}
