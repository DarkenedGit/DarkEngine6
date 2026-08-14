#pragma once
struct Eye
{
    float mDist;
    float mAngleOfRotation; //How much eyes can rotate before you have to turn head.
    float mReactionTime;    //Time to focus on item, maybe never if its a lazy eye?
    float mFOVCentral;      //High detail vision;
    float mFOVPeripheral;   //Motion detection vision;
};

struct Sight
{
    // 2D vs 3D, if they take damage to 1 eye, they are mostly 2D
    bool mDepthPerception;

    // Blurry 0 is no blur
    float mBlurRadius;

    // Light Sensitivity, pain if light or looking at sun.
    float mPhotophobia;

    // ability to see in the dark
    float mScotopic;
};

struct Ear
{
    float mFOV;
    float mTriangulationTime; //Time to rotate head and pinpoint sound
    float mSensitivity;
};

struct Hearing
{
    float mMinDecibel;
    float mMaxDecibel;

    float mMinFrequency;
    float mMaxFrequency;

    float mAuditoryFatigue;  //Can't hear after concusion
    bool  mAuditoryClipping; //Ringing in ears after concusion
};

struct Mouth
{
    float mFOV;
    float mMuscles;
    float mSpeed;
    float mTeeth;
    float mTongue;
    float mLips;
};

struct OralSkills
{
    float mSpeak;
    float mBite;
};

struct Brain
{
    float mIQ;
    float mMemory;
};

struct CognitiveFitness
{
    float mReaction;
    float mConscious;
};

struct Skull
{
    float mBone;
    float mSkin;
    float mHair;
};

struct SkullState
{
    float mOverheat;
    float mSunScreen;
    float mConcusion;
};

struct HeadSkin
{
    float mTemprature;
    float mFirmness;
    float mColor;
    float mScars;
};

struct SkinStatus
{
    float mRadiated;
    float mFrostBite;
    float mSunBurn;
    float mBleeding;
    float mBurning;
};

struct HumanHead
{
    Eye   mEyes[2];
    Sight mSight;

    Ear     mEars[2];
    Hearing mHearing;

    Mouth      mMouth;
    OralSkills mOral;

    Brain            mBrain;
    CognitiveFitness mCognitive;

    Skull      mSkull;
    SkullState mSkullStatus;

    HeadSkin   mFaceSkin;
    SkinStatus mSkinStatus;
};

struct HeadStatus
{
    float mStroke;        //Part of your brain is damaged, lost memories, lost motor control
    float mConcusion;     //You temperarially have no recall
    float mDisorientated; //Your senses are delayed and often not right.
    float mDeaf;          //You can not hear
    float mBlind;         //You can not see
    float mUgly;          //No one wants to look at you.
    float mGrating;       //No one wants to listen too.
};
