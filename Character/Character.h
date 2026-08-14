#pragma once

#include "Body.h"

class Character
{
    //Physical
    Body       mBody;
    BodyStatus mBodyStatus;

    //Not part of body as items can effect.
    float mClimbSpeed;
    float mVerticality; // How high can normally jump;
    float mRunSpeed;
    float mSprintSpeed;

    //Skills
    std::vector<Language> mLanguages;

    //Personality mPersonality;
};
