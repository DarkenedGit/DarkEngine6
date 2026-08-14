#pragma once

#include "HumanHead.h"
#include "HumanTorso.h"
#include "HumanUpperExtremities.h"
#include "HumanLowerExtremities.h"
#include "HumanPhysiologicalEffects.h"

class Body
{
    HumanHead  mHead;
    HeadStatus mHeadStatus;

    HumanTorso  mTorso;
    TorsoStatus mTorsoStatus;

    HumanUpperExtremities  mUpperExtem;
    UpperExtremitiesStatus mUpperExtremitiesStatus;

    HumanLowerExtremities  mLowerExtremities;
    LowerExtremitiesStatus mLowerExtremitiesStatus;
};

struct BodyStatus
{
    Pain       mPain;      //Total pain and effects it is causing;
    Hydrate    mHydration; //Ability to convert sugars
    Blood      mBlood;     //How much blood you have left and how toxic it is
    Bleeding   mBleeding;  //How quickly you are losing blood
    Concussion mConcusion; //
    Sight      mSight;
    Voice      mVoice;
    Starve     mStarve;
};
