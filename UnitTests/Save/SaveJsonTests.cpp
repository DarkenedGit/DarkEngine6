#include <gtest/gtest.h>

#include "Save/SaveJson.h"

#include <cmath>
#include <limits>

using namespace Dark;

TEST(SaveJson, MissingKeyKeepsValue)
{
    nlohmann::ordered_json obj = nlohmann::ordered_json::object();
    Save::SaveReader in(obj, nullptr);
    float value = 3.5f;
    EXPECT_TRUE(in.f32("hp", value, 0.0f, 10.0f));
    EXPECT_EQ(value, 3.5f);
    EXPECT_FALSE(in.failed());
}

TEST(SaveJson, WrongTypeDoesNotWrite)
{
    nlohmann::ordered_json obj = nlohmann::ordered_json::object();
    obj["hp"] = "nope";
    Save::SaveReader in(obj, nullptr);
    float value = 3.5f;
    EXPECT_FALSE(in.f32("hp", value, 0.0f, 10.0f));
    EXPECT_EQ(value, 3.5f);
    EXPECT_TRUE(in.failed());
}

TEST(SaveJson, NonFiniteBecomesZero)
{
    nlohmann::ordered_json obj = nlohmann::ordered_json::object();
    int sanitized = 0;
    Save::SaveWriter out(obj, nullptr);
    out.shareSanitize(&sanitized);
    out.f32("x", std::numeric_limits<float>::quiet_NaN());
    const auto* number = obj["x"].get_ptr<const double*>();
    ASSERT_NE(number, nullptr);
    EXPECT_EQ(*number, 0.0);
    EXPECT_EQ(sanitized, 1);
}

TEST(SaveJson, HugeQuatComponentLeavesValue)
{
    nlohmann::ordered_json obj = nlohmann::ordered_json::object();
    obj["rot"] = nlohmann::ordered_json::array({ 1.0, 0.0, 0.0, 1e308 });
    Save::SaveReader in(obj, nullptr);
    Math::Quaternion value(0.5f, 0.25f, -0.25f, 0.75f);
    EXPECT_FALSE(in.quat("rot", value));
    EXPECT_EQ(value.w, 0.5f);
    EXPECT_EQ(value.x, 0.25f);
    EXPECT_EQ(value.y, -0.25f);
    EXPECT_EQ(value.z, 0.75f);
    EXPECT_TRUE(in.failed());
}

TEST(SaveJson, ParseRejectsNonObject)
{
    nlohmann::ordered_json doc;
    Save::SaveResult error = Save::SaveResult::Ok;
    EXPECT_FALSE(Save::parseDocument("[]", doc, error));
    EXPECT_EQ(error, Save::SaveResult::NotObject);
}
