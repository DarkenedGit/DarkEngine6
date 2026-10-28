#include "Terrain/LeafRustle.h"

#include "Assets/AssetManager.h"
#include "Audio/AudioSystem.h"
#include "Audio/SoundClip.h"
#include "Terrain/LeafWind.h"

#include <cmath>

namespace Dark::Terrain
{
    namespace
    {
        constexpr float kRampSeconds = 0.12f;

        float xzDistance(float x, float z, float listenerX, float listenerZ)
        {
            const float dx = x - listenerX;
            const float dz = z - listenerZ;
            return std::sqrt(dx * dx + dz * dz);
        }

        struct NearTree
        {
            RustleTree tree;
            float      dist = 0.0f;
        };

        int findHeld(const RustleHeld* held, int heldCount, uint32_t id)
        {
            for (int i = 0; i < heldCount; ++i)
            {
                if (held[i].id == id)
                    return i;
            }
            return -1;
        }
    } // namespace

    float leafRustleVolume(float strength)
    {
        if (!(strength > 0.0f))
            return 0.0f;
        if (strength > 1.0f)
            return 1.0f;
        return strength;
    }

    RustlePlay rustlePlayForClip(bool clipValid)
    {
        RustlePlay play;
        play.startVoice  = clipValid;
        play.requestTone = false;
        return play;
    }

    Audio::PlayDesc makeRustlePlayDesc(float volume, float x, float y, float z)
    {
        Audio::PlayDesc desc;
        desc.volume      = volume;
        desc.loop        = true;
        desc.spatial     = true;
        desc.doppler     = false;
        desc.sourceId    = 0;
        desc.minDistance = 2.0f;
        desc.maxDistance = kRustleRadiusM;
        desc.position    = Math::Vector3f(x, y, z);
        return desc;
    }

    int selectLeafRustle(const RustleTree* trees, int treeCount, float listenerX, float listenerZ, const RustleHeld* held, int heldCount,
                         RustleChoice* out, int cap)
    {
        if (!out || cap <= 0)
            return 0;
        if (cap > kRustleVoices)
            cap = kRustleVoices;
        if (treeCount < 0)
            treeCount = 0;
        if (heldCount < 0)
            heldCount = 0;
        if (!trees)
            treeCount = 0;
        if (!held)
            heldCount = 0;

        RustleChoice chosen[kRustleVoices]{};
        int          count = 0;

        for (int h = 0; h < heldCount && count < cap; ++h)
        {
            if (held[h].id == 0 || !held[h].stopping)
                continue;
            bool already = false;
            for (int i = 0; i < count; ++i)
            {
                if (chosen[i].id == held[h].id)
                    already = true;
            }
            if (already)
                continue;
            RustleChoice& choice = chosen[count++];
            choice.id            = held[h].id;
            choice.stop          = true;
        }

        for (int h = 0; h < heldCount && count < cap; ++h)
        {
            if (held[h].id == 0 || held[h].stopping)
                continue;
            const RustleTree* tree = nullptr;
            float             dist = kRustleRadiusM + 1.0f;
            for (int i = 0; i < treeCount; ++i)
            {
                if (trees[i].id != held[h].id)
                    continue;
                tree = &trees[i];
                dist = xzDistance(tree->x, tree->z, listenerX, listenerZ);
                break;
            }
            if (!tree || dist > kRustleRadiusM)
            {
                RustleChoice& choice = chosen[count++];
                choice.id            = held[h].id;
                choice.stop          = true;
                continue;
            }
            RustleChoice& choice = chosen[count++];
            choice.id            = tree->id;
            choice.x             = tree->x;
            choice.y             = tree->y;
            choice.z             = tree->z;
            choice.volume        = leafRustleVolume(tree->strength);
            choice.stop          = false;
        }

        int   order[256];
        float orderDist[256];
        int   orderCount = 0;
        for (int i = 0; i < treeCount; ++i)
        {
            const RustleTree& tree = trees[i];
            if (tree.id == 0)
                continue;
            if (findHeld(held, heldCount, tree.id) >= 0)
                continue;
            const float dist = xzDistance(tree.x, tree.z, listenerX, listenerZ);
            if (dist > kRustleRadiusM)
                continue;
            if (orderCount == 256)
            {
                int farthest = 0;
                for (int n = 1; n < orderCount; ++n)
                {
                    if (orderDist[n] > orderDist[farthest])
                        farthest = n;
                }
                if (!(dist < orderDist[farthest]))
                    continue;
                for (int n = farthest; n < orderCount - 1; ++n)
                {
                    order[n]     = order[n + 1];
                    orderDist[n] = orderDist[n + 1];
                }
                --orderCount;
            }
            int place = orderCount;
            ++orderCount;
            while (place > 0 && dist < orderDist[place - 1])
            {
                order[place]     = order[place - 1];
                orderDist[place] = orderDist[place - 1];
                --place;
            }
            order[place]     = i;
            orderDist[place] = dist;
        }

        for (int n = 0; n < orderCount; ++n)
        {
            const RustleTree& tree = trees[order[n]];
            const float       dist = orderDist[n];

            int freeSlot = -1;
            for (int s = 0; s < cap; ++s)
            {
                if (s >= count || chosen[s].id == 0)
                {
                    freeSlot = s;
                    break;
                }
            }
            if (freeSlot >= 0 && freeSlot >= count)
            {
                RustleChoice& choice = chosen[count++];
                choice.id            = tree.id;
                choice.x             = tree.x;
                choice.y             = tree.y;
                choice.z             = tree.z;
                choice.volume        = leafRustleVolume(tree.strength);
                choice.stop          = false;
                continue;
            }
            if (freeSlot >= 0 && chosen[freeSlot].id == 0)
            {
                RustleChoice& choice = chosen[freeSlot];
                choice.id            = tree.id;
                choice.x             = tree.x;
                choice.y             = tree.y;
                choice.z             = tree.z;
                choice.volume        = leafRustleVolume(tree.strength);
                choice.stop          = false;
                continue;
            }

            int   farthest = -1;
            float farDist  = -1.0f;
            for (int s = 0; s < count; ++s)
            {
                if (chosen[s].stop || chosen[s].id == 0)
                    continue;
                const float heldDist = xzDistance(chosen[s].x, chosen[s].z, listenerX, listenerZ);
                if (heldDist > farDist)
                {
                    farDist  = heldDist;
                    farthest = s;
                }
            }
            if (farthest < 0)
                continue;
            if (!(dist + kRustleSwapMarginM < farDist))
                continue;
            chosen[farthest].stop   = true;
            chosen[farthest].volume = 0.0f;
        }

        for (int i = 0; i < count; ++i)
            out[i] = chosen[i];
        return count;
    }

    void LeafRustleVoices::clearSlot(Audio::AudioSystem& audio, Slot& slot)
    {
        if (slot.voice != 0)
            audio.stop(slot.voice);
        slot = {};
    }

    void LeafRustleVoices::stepStop(Audio::AudioSystem& audio, Slot& slot, float dt)
    {
        const float step = kRampSeconds > 0.0f ? dt / kRampSeconds : 1.0f;
        slot.volume -= step;
        if (!(slot.volume > 0.0f))
        {
            clearSlot(audio, slot);
            return;
        }
        if (slot.voice != 0)
            audio.setVoiceVolume(slot.voice, slot.volume);
    }

    void LeafRustleVoices::stopAll(Audio::AudioSystem& audio)
    {
        for (Slot& slot : m_slots)
            clearSlot(audio, slot);
    }

    int LeafRustleVoices::activeVoiceCount() const
    {
        int count = 0;
        for (const Slot& slot : m_slots)
        {
            if (slot.voice != 0)
                ++count;
        }
        return count;
    }

    void LeafRustleVoices::tick(Audio::AudioSystem& audio, AssetManager& assets, const LeafField::Crown* crowns, int crownCount, float listenerX,
                                float listenerY, float listenerZ, double simTime, float dt)
    {
        (void)listenerY;
        if (!m_tried)
        {
            m_tried = true;
            m_clip  = audio.loadWav(assets, kLeafRustleClipPath);
        }

        NearTree near[256];
        int      nearCount = 0;
        if (crowns && crownCount > 0)
        {
            for (int i = 0; i < crownCount; ++i)
            {
                const LeafField::Crown& crown = crowns[i];
                if (crown.id == 0)
                    continue;
                const float dist = xzDistance(crown.x, crown.z, listenerX, listenerZ);
                if (dist > kRustleRadiusM)
                    continue;
                NearTree item;
                item.tree.id = crown.id;
                item.tree.x  = crown.x;
                item.tree.y  = crown.seatY;
                item.tree.z  = crown.z;
                const LeafWindSample wind = sampleLeafWind(crown.x, crown.z, simTime);
                item.tree.strength        = wind.strength;
                item.dist                 = dist;
                if (nearCount < 256)
                {
                    near[nearCount++] = item;
                    continue;
                }
                int   farthest = 0;
                float farDist  = near[0].dist;
                for (int n = 1; n < nearCount; ++n)
                {
                    if (near[n].dist > farDist)
                    {
                        farDist  = near[n].dist;
                        farthest = n;
                    }
                }
                if (dist < farDist)
                    near[farthest] = item;
            }
        }

        RustleTree trees[256];
        for (int i = 0; i < nearCount; ++i)
            trees[i] = near[i].tree;

        RustleHeld held[kRustleVoices];
        int        heldCount = 0;
        for (const Slot& slot : m_slots)
        {
            if (slot.treeId == 0 || slot.voice == 0)
                continue;
            held[heldCount].id       = slot.treeId;
            held[heldCount].stopping = slot.stopping;
            ++heldCount;
        }

        RustleChoice choices[kRustleVoices];
        const int    choiceCount = selectLeafRustle(trees, nearCount, listenerX, listenerZ, held, heldCount, choices, kRustleVoices);
        const RustlePlay play    = rustlePlayForClip(static_cast<bool>(m_clip) && m_clip->valid());

        bool used[kRustleVoices]{};
        for (int c = 0; c < choiceCount; ++c)
        {
            const RustleChoice& choice = choices[c];
            int                 slotIndex = -1;
            for (int s = 0; s < kRustleVoices; ++s)
            {
                if (m_slots[s].treeId == choice.id && m_slots[s].voice != 0)
                {
                    slotIndex = s;
                    break;
                }
            }
            if (slotIndex < 0)
            {
                for (int s = 0; s < kRustleVoices; ++s)
                {
                    if (m_slots[s].voice == 0 && m_slots[s].treeId == 0)
                    {
                        slotIndex = s;
                        break;
                    }
                }
            }
            if (slotIndex < 0)
                continue;
            used[slotIndex] = true;
            Slot& slot      = m_slots[slotIndex];
            if (choice.stop || !(choice.volume > 0.0f))
            {
                slot.treeId   = choice.id;
                slot.stopping = true;
                if (slot.voice == 0)
                {
                    slot = {};
                    used[slotIndex] = false;
                    continue;
                }
                stepStop(audio, slot, dt);
                continue;
            }
            slot.treeId   = choice.id;
            slot.stopping = false;
            slot.x        = choice.x;
            slot.y        = choice.y;
            slot.z        = choice.z;
            slot.volume   = choice.volume;
            if (slot.voice == 0)
            {
                if (!play.startVoice || play.requestTone)
                    continue;
                const Audio::PlayDesc desc = makeRustlePlayDesc(slot.volume, slot.x, slot.y, slot.z);
                slot.voice                 = audio.play(m_clip, desc);
                continue;
            }
            audio.setVoiceVolume(slot.voice, slot.volume);
            audio.setVoicePosition(slot.voice, Math::Vector3f(slot.x, slot.y, slot.z));
        }

        for (int s = 0; s < kRustleVoices; ++s)
        {
            if (!used[s] && m_slots[s].voice != 0)
                clearSlot(audio, m_slots[s]);
        }
    }

} // namespace Dark::Terrain
