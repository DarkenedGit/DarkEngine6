#pragma once

struct Posterior
{
    float mStrength;
    float mIndurance;
};

struct Hips
{
    float   mStrength;
    float   mRangeOfMotion;
};

struct Genital
{
    float mExcited;
    float mDamaged;
    float mPain;
    float mDesease;
};

struct Pelvis
{
    Posterior   mArse;
    float       mPain;

    Hips        mHips;
    float       mPain;
    float       mArthritis;

};

struct Thigh
{
    float mStrength;
    float mIndurance;
};

struct Knee
{
    float   mStrength;
    float   mRangeOfMotion;
    float   mArthritis;
};

struct Shin
{
    float mStrength;
    float mIndurance;
};

struct Leg
{
    Thigh mThigh;
    Knee mKnee;
    Shin mShin;

    float mSpeed;
    float mRangeOfMotion;
};

struct Foot
{
    float mSize;
    float mBalance;
};

struct HumanLowerExtremities
{
    Pelvis  mPelvis;
    
    Genital mGenitalia;

    Leg     mLegs[2];
    Foot    mFeet[2];
};

struct LowerExtremitiesStatus
{
    float mArthritis;
    float mBroken;
    float mTorn;
    float mExhausted;
    float mBleeding;
    float mBrusing;
    float mPain;
    float mStearile;
    float mSpreadVDs;
};