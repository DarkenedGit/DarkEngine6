#include "Media/MovieClock.h"

namespace Dark
{

    double movieAudioSeconds(uint64_t samplesPlayed, uint64_t segmentBase, uint32_t sampleRate, double seekOrigin)
    {
        if (sampleRate == 0)
            return seekOrigin;

        const double played = static_cast<double>(samplesPlayed);
        const double base   = static_cast<double>(segmentBase);
        return seekOrigin + (played - base) / static_cast<double>(sampleRate);
    }

    MovieClockOutput evaluateMovieClock(const MovieClockInput& in)
    {
        MovieClockOutput out;
        const bool       useAudio = in.audioMaster && !in.audioDrained;
        const double     clock    = useAudio ? in.audioSeconds : in.wallSeconds;
        const double     lead     = useAudio ? in.leadSeconds : 0.0;
        out.targetPts             = clock - lead;
        return out;
    }

    namespace
    {

        void pushDrop(MovieFrameChoice& out, int index)
        {
            if (out.dropCount < 8)
                out.droppedIndex[out.dropCount] = index;
            ++out.dropCount;
        }

        void holdOrBlack(MovieFrameChoice& out, bool havePresented)
        {
            if (havePresented)
                out.holdLast = true;
            else
                out.drawBlack = true;
        }

    } // namespace

    MovieFrameChoice selectMovieFrame(const MovieQueuedFrame* frames, int frameCount, double targetPts, double frameDuration, bool havePresented)
    {
        MovieFrameChoice out;
        if (!frames || frameCount <= 0)
        {
            holdOrBlack(out, havePresented);
            return out;
        }

        int eligible[64];
        int eligibleCount = 0;
        const int limit = frameCount < 64 ? frameCount : 64;
        for (int i = 0; i < limit; ++i)
        {
            if (frames[i].pts <= targetPts && eligibleCount < 64)
            {
                eligible[eligibleCount] = i;
                ++eligibleCount;
            }
        }

        if (eligibleCount == 0)
        {
            // A frame with pts above the target stays queued.
            holdOrBlack(out, havePresented);
            return out;
        }

        int best = eligible[0];
        for (int n = 1; n < eligibleCount; ++n)
        {
            const int i = eligible[n];
            if (frames[i].pts >= frames[best].pts)
                best = i;
        }

        const double behind = targetPts - frames[best].pts;
        if (behind > 2.0 * frameDuration)
        {
            // Newest is past two durations, so nothing is within one duration. Drop them and hold.
            for (int n = 0; n < eligibleCount; ++n)
                pushDrop(out, frames[eligible[n]].index);
            holdOrBlack(out, havePresented);
            return out;
        }

        out.presentIndex = frames[best].index;
        for (int n = 0; n < eligibleCount; ++n)
        {
            const int i = eligible[n];
            if (i != best)
                pushDrop(out, frames[i].index);
        }
        return out;
    }

} // namespace Dark
