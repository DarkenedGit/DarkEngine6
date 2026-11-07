#pragma once

#include <cstdint>

namespace Dark::Audio
{
    // Pure queue rule so tests can run without an XAudio device.
    // Queued above capacity counts as full.
    inline uint32_t streamPacketsFree(uint32_t capacity, uint32_t buffersQueued)
    {
        const uint32_t queued = buffersQueued < capacity ? buffersQueued : capacity;
        return capacity - queued;
    }

    // Advances only when a slot is free. A full queue leaves writeIndex unchanged.
    inline bool advanceStreamWriteIndex(uint32_t capacity, uint32_t buffersQueued, uint32_t& writeIndex)
    {
        if (capacity == 0 || streamPacketsFree(capacity, buffersQueued) == 0)
            return false;
        writeIndex = (writeIndex + 1u) % capacity;
        return true;
    }

    // Clip play sets XAUDIO2_END_OF_STREAM (0x40). A stream packet must not, or the voice stops.
    inline uint32_t streamSubmitBufferFlags()
    {
        return 0;
    }

} // namespace Dark::Audio
