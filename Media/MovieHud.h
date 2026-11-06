#pragma once

// ImGui movie HUD shared by Sandbox and Editor.
// Do not include this from a DarkEngine translation unit. Media/*.cpp is compiled into
// DarkEngine, and DarkEngine does not link Dear ImGui. UnitTests must not include it either.

#include "Media/MoviePlayer.h"

#include <imgui.h>

namespace Dark
{

    struct MovieHudView
    {
        bool controls = false;
    };

    inline const char* moviePlayStateLabel(MoviePlayState state)
    {
        switch (state)
        {
        case MoviePlayState::Closed:
            return "Closed";
        case MoviePlayState::Opening:
            return "Opening";
        case MoviePlayState::Ready:
            return "Ready";
        case MoviePlayState::Playing:
            return "Playing";
        case MoviePlayState::Paused:
            return "Paused";
        case MoviePlayState::Seeking:
            return "Seeking";
        case MoviePlayState::Ended:
            return "Ended";
        case MoviePlayState::Failed:
            return "Failed";
        }
        return "?";
    }

    inline void drawMovieHud(MoviePlayer& movie, const MovieHudView& view)
    {
        if (movie.state() == MoviePlayState::Closed)
            return;

        const ImGuiViewport* vp = ImGui::GetMainViewport();
        if (!vp)
            return;

        const MovieStats stats = movie.stats();
        ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x - 280.0f, vp->Pos.y + 48.0f), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
        ImGui::SetNextWindowBgAlpha(0.72f);
        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings
            | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
        if (!ImGui::Begin("Movie", nullptr, flags))
        {
            ImGui::End();
            return;
        }

        ImGui::Text("State %s", moviePlayStateLabel(movie.state()));
        ImGui::Text("Clock %.3f s", stats.clockSeconds);
        ImGui::Text("PTS %.3f s", stats.lastPresentPts);
        ImGui::Text("Video queue %u", stats.videoQueueDepth);
        ImGui::Text("Audio queue %u", stats.audioQueueDepth);
        ImGui::Text("Audio buffers %u", stats.audioBuffersQueued);
        ImGui::Text("Dropped %llu", static_cast<unsigned long long>(stats.framesDropped));
        ImGui::Text("Underruns %u", stats.audioUnderruns);
        ImGui::Text("Decode %.2f ms", static_cast<double>(stats.decodeMsAvg));

        if (view.controls && movie.blocksWorld())
        {
            if (movie.state() == MoviePlayState::Paused)
            {
                if (ImGui::Button("Resume"))
                    movie.play();
            }
            else if (ImGui::Button("Pause"))
                movie.pause();
            ImGui::SameLine();
            if (ImGui::Button("-5s"))
                movie.seek(stats.clockSeconds - 5.0);
            ImGui::SameLine();
            if (ImGui::Button("+5s"))
                movie.seek(stats.clockSeconds + 5.0);
        }

        ImGui::End();
    }

} // namespace Dark
