#include <gtest/gtest.h>

#include "Game/EchoFeature.h"

using namespace Dark;

TEST(EchoFeature, UncreatedFeatureIsInvalidWithTheBuiltinChirp)
{
    EchoFeature feature;
    EXPECT_FALSE(feature.isValid());
    EXPECT_FALSE(feature.pipeline().hasSnapshot());
    const EchoChirpSettings builtin = builtinEchoChirpSettings();
    EXPECT_FLOAT_EQ(feature.chirp().periodSeconds, builtin.periodSeconds);
    EXPECT_FLOAT_EQ(feature.chirp().speedMetersPerSec, builtin.speedMetersPerSec);
    EXPECT_FLOAT_EQ(feature.chirp().rangeMeters, builtin.rangeMeters);
    feature.release();
    EXPECT_FALSE(feature.isValid());
}
