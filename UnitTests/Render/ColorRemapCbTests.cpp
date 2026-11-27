#include "Render/ColorRemapCb.h"

#include <gtest/gtest.h>

TEST(ColorRemapCb, CreateNullDeviceLeavesNoHandle)
{
    // create() failure must not present a descriptor for the lighting table to copy.
    Dark::ColorRemapCb cb;
    EXPECT_FALSE(cb.create(nullptr));
    EXPECT_FALSE(cb.valid());
    EXPECT_EQ(cb.cpu(0).ptr, 0ull);
    EXPECT_EQ(cb.cpu(1).ptr, 0ull);
}
