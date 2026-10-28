#pragma once

#include "Assets/AssetHandle.h"
#include "Audio/AudioSystem.h"
#include "Audio/SoundClip.h"
#include "Terrain/LeafField.h"

#include <cstdint>

namespace Dark
{
    class AssetManager;
}

namespace Dark::Terrain
{
    inline constexpr const char* kLeafRustleClipPath = "audio/leaves_rustle.wav";
    inline constexpr int         kRustleVoices       = 4;
    inline constexpr float       kRustleRadiusM      = 32.0f;
    inline constexpr float       kRustleSwapMarginM  = 1.0f;

    struct RustleTree
    {
        uint32_t id       = 0;
        float    x        = 0.0f;
        float    y        = 0.0f;
        float    z        = 0.0f;
        float    strength = 0.0f;
    };

    struct RustleHeld
    {
        uint32_t id       = 0;
        bool     stopping = false;
    };

    struct RustleChoice
    {
        uint32_t id       = 0;
        float    x        = 0.0f;
        float    y        = 0.0f;
        float    z        = 0.0f;
        float    volume   = 0.0f;
        bool     stop     = false;
    };

    struct RustlePlay
    {
        bool startVoice  = false;
        bool requestTone = false;
    };

    // Strength 0 is silence. A moderate strength stays audible and rises with the wind.
    float leafRustleVolume(float strength);

    // Empty and missing clips start nothing and request no tone.
    RustlePlay rustlePlayForClip(bool clipValid);

    // Looping, spatial, unattributed, doppler off. The root is the foliage seat.
    Audio::PlayDesc makeRustlePlayDesc(float volume, float x, float y, float z);

    // Up to four trees inside 32 m. A stopping held voice keeps its slot until the host clears it.
    // A newcomer inside the swap margin does not evict a tree the set already holds.
    int selectLeafRustle(const RustleTree* trees, int treeCount, float listenerX, float listenerZ, const RustleHeld* held, int heldCount,
                         RustleChoice* out, int cap);

    // Owns at most four voices. A missing clip stays silent.
    class LeafRustleVoices
    {
    public:
        void tick(Audio::AudioSystem& audio, AssetManager& assets, const LeafField::Crown* crowns, int crownCount, float listenerX, float listenerZ,
                  double simTime, float dt);
        void stopAll(Audio::AudioSystem& audio);

    private:
        struct Slot
        {
            uint32_t treeId   = 0;
            uint32_t voice    = 0;
            float    volume   = 0.0f;
            float    x        = 0.0f;
            float    y        = 0.0f;
            float    z        = 0.0f;
            bool     stopping = false;
        };

        void clearSlot(Audio::AudioSystem& audio, Slot& slot);
        void stepStop(Audio::AudioSystem& audio, Slot& slot, float dt);

        Slot                       m_slots[kRustleVoices]{};
        AssetRef<Audio::SoundClip> m_clip;
        bool                       m_tried = false;
    };

} // namespace Dark::Terrain
