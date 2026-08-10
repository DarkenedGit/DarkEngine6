#pragma once

struct Pain
{
    float mPainLevel;

    // Conditions cause by the pain.
    float mDiscomfert   = 1.0f;
    float mRestrictive  = 4.0f;
    float mCrippling    = 8.0f;

    //Time duration of pain
    bool mAcute;        // Short sudden pain, cuts and small burns
    bool mSubacute;     // Up to 3 months to heal, broken bones, cuts that go the bone.
    bool mChronic;      // Can last forever, damage that is around after the healing.
    bool mNeuropathic;  //Damage to the nervous system itself, burning or shooting pain 
};