#pragma once

struct Ribs
{
    float mImpactStrength;
    float mResiliance;
};

struct Lungs
{
    float mCapasity;
    float mConversion;
    float mRatePerMin;
    float mPressure; // From Diaphram
    float mInfection;
};

struct Heart
{
    float mStrength;
    float mRatePerMin;
    float mFatigued;
};

struct Stomach
{
    float mFood;
    float mWater;
    float mTimeFoodEmpty;
    float mTimeWaterEmpty;
    float mPain;
    float mNoiseLevel;
    float mNoiseTimer;
    float mStarveDamagePerMin;
    float mThurstDamagePerMin;
};

struct Spine
{
    float mStrength;
    float mAlignment;
    float mCrushed;
    float mSevered;
};

struct HumanTorso
{
    Ribs  mRibs;
    float mProtection;

    Lungs mLungs;
    float mAsphyxiation;

    Heart mHeart;
    float mHypotension;

    Stomach mStomach;
    float   mStarvation;

    Spine mSpine;
    float mBackPain;
    float mParalization;
};

struct TorsoStatus
{
    float mBackPain;
    float mSholderPain;
    float mUpperParalization;
    float mLowerParalization;
    float mBreathingPain;
    float mStomachPain;
    float mLungAsphyxiation;
};