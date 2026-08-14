#pragma once

struct Arm
{
    float mStrength;
    float mSpeed;
    float mMotionRange;

    float mBoneDamage;
    float mMuscleDamage;
    float mJointDamage;
};

struct Hand
{
    float mStrength;
    float mSpeed;

    float mFingerDamage;
    float mHandDamage;
};

struct HumanUpperExtremities
{
    Arm  mArms[2];
    Hand mHands[2];
    // Status effects on the above.
};

struct UpperExtremitiesStatus
{
    float mArthritis;
    float mBroken;
    float mTorn;
    float mExhausted;
    float mBleeding;
    float mBrusing;
    float mPain;
    float mSpreadAirInfection;
    float mSpreadTouchInfection;
};