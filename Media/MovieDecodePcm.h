#pragma once

#include <cstddef>

namespace Dark
{

    inline constexpr std::size_t kMovieAudioQueueCap = 8;
    // Under one 20 ms packet, and the stand-in for a buffer-complete callback.
    inline constexpr int kMovieAudioCapWaitMs = 10;

    inline bool movieAudioQueueAtCap(std::size_t queued)
    {
        return queued >= kMovieAudioQueueCap;
    }

    // 0: do not wait. A read can proceed, or the caller is stopping or idle.
    // >0: the audio queue is at cap. Wait, then drain again. Tick is not the drainer.
    // <0: no free video slot, and the audio queue is not at cap. Wait until present notifies.
    inline int movieDecodeWaitMs(bool stop, bool canVideo, bool canAudio, bool audioAtCap, bool streamsDone)
    {
        if (stop || streamsDone || canVideo || canAudio)
            return 0;
        if (audioAtCap)
            return kMovieAudioCapWaitMs;
        return -1;
    }

    // A false submit is a full ring. It does not mark the stream ended.
    inline bool movieApplyPcmSubmit(bool submitted, bool packetEnd, bool& eosSubmitted)
    {
        if (!submitted)
            return false;
        if (packetEnd)
            eosSubmitted = true;
        return true;
    }

} // namespace Dark
