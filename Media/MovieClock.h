#pragma once

#include <cstdint>

namespace Dark
{

    inline constexpr double kMovieAudioLeadSeconds = 0.030;

    struct MovieClockInput
    {
        bool   audioMaster    = false;
        bool   audioDrained   = false; // Voice is open and the sample counter has stopped.
        double audioSeconds   = 0.0;   // (samplesPlayed - segmentBase) / rate + seekOrigin
        double wallSeconds    = 0.0;   // steady_clock minus paused time
        double leadSeconds    = kMovieAudioLeadSeconds;
        double frameDuration  = 1.0 / 30.0;
    };

    struct MovieClockOutput
    {
        double targetPts = 0.0;
    };

    struct MovieQueuedFrame
    {
        double pts   = 0.0;
        int    index = -1;
    };

    struct MovieFrameChoice
    {
        int  presentIndex = -1;
        bool holdLast     = false;
        bool drawBlack    = false;
        int  dropCount    = 0;
        int  droppedIndex[8]{};
    };

    // audioSeconds = seekOrigin + (samplesPlayed - segmentBase) / sampleRate
    double movieAudioSeconds(uint64_t samplesPlayed, uint64_t segmentBase, uint32_t sampleRate, double seekOrigin);

    // Source EOS has been submitted and the voice has no buffers left. The sample counter will not move.
    inline bool movieAudioDrained(bool audioEos, bool audioQueued, bool eosSubmitted, uint32_t buffersQueued)
    {
        return audioEos && !audioQueued && eosSubmitted && buffersQueued == 0u;
    }

    // While samples are moving, targetPts = audioSeconds - lead. A drained voice follows wallSeconds with no lead.
    MovieClockOutput evaluateMovieClock(const MovieClockInput& in);

    // Latest queued frame with pts <= target. More than two durations behind drops until the queue is empty, then holds.
    MovieFrameChoice selectMovieFrame(const MovieQueuedFrame* frames, int frameCount, double targetPts, double frameDuration, bool havePresented);

} // namespace Dark
