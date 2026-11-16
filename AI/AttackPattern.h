#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Dark::AI
{
    enum class AttackKind : unsigned char
    {
        Melee = 0,
        Jump,
        Rush,
    };

    enum class AttackAfterChain : unsigned char
    {
        Pause = 0,
        Reposition,
    };

    struct AttackMove
    {
        std::string id;
        AttackKind  kind      = AttackKind::Melee;
        float       minRange  = 0.0f;
        float       maxRange  = 2.2f;
        float       windup    = 0.40f;
        float       damage    = 12.0f;
        float       stunSeconds = 0.28f;
        float       poise     = 10.0f;
        // Share of visits to this band that keep closing. The rest rush. 0 always rushes. 1 always closes.
        // A value above 1 and at most 100 is read as a percent.
        float       closeChance      = 0.5f;
        float       contactRange     = 1.2f;
        float       knockback        = 0.0f;
        float       knockbackSeconds = 0.18f;
        float       rushSpeed        = 0.0f; // 0 uses the hunter's run speed
        float       rushSeconds      = 1.25f;
    };

    // roll is in [0, 1). closeChance is the fraction of approaches that keep closing.
    inline bool approachKeepsClosing(float closeChance, float roll)
    {
        if (!(closeChance > 0.0f))
            return false;
        if (closeChance >= 1.0f)
            return true;
        if (roll < 0.0f)
            roll = 0.0f;
        return roll < closeChance;
    }

    // Stable 0..1 value from a salt. The same key always returns the same roll.
    inline float approachRoll(uint32_t key)
    {
        const uint32_t x = key * 1664525u + 1013904223u;
        return static_cast<float>(x >> 8) * (1.0f / 16777216.0f);
    }

    struct AttackPattern
    {
        std::string      id;
        float            gapSeconds          = 1.25f;
        int              chainCount          = 2;
        AttackAfterChain afterChain          = AttackAfterChain::Pause;
        float            pauseSeconds        = 2.5f;
        float            repositionSeconds   = 1.4f;
        float            repositionDistance  = 5.0f;
        float            packGapSeconds      = 1.2f;
        std::vector<AttackMove> attacks;
        bool             loaded = false;
    };

    struct AttackClock
    {
        float gap            = 0.0f;
        float pause          = 0.0f;
        float reposition     = 0.0f;
        int   chain          = 0;
        int   lastAttack     = -1;
        bool  preferOther    = false;
    };

    bool parseAttackPattern(std::string_view jsonText, AttackPattern& out);
    bool loadAttackPatternFile(const std::filesystem::path& path, AttackPattern& out);
    AttackPattern loadAttackPattern(std::string_view virtualPath);

    void tickAttackClock(AttackClock& clock, float dt);
    bool attackClockReady(const AttackClock& clock, float packGap);
    // Counts the attack. When the chain fills, arms a pause or a reposition and clears the chain.
    void noteAttackStarted(const AttackPattern& pattern, AttackClock& clock, float& packGap);

    // Index into pattern.attacks, or -1. preferOther skips lastAttack when another move fits.
    int pickAttack(const AttackPattern& pattern, float distance, bool preferOther, int lastAttack);
}
