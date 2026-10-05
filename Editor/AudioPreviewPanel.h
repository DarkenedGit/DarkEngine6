#pragma once

#include "Assets/AssetManager.h"
#include "Audio/AudioSystem.h"
#include "Editor/AudioPreviewListing.h"

#include <string>
#include <vector>

// Plays content/audio wavs through the game mixer so an audition matches in-game playback.
class AudioPreviewPanel
{
public:
    void draw(Dark::Audio::AudioSystem& audio, Dark::AssetManager& assets, bool* open);

private:
    void refresh(Dark::AssetManager& assets);
    void stopPreview(Dark::Audio::AudioSystem& audio);
    void loadSelected(Dark::Audio::AudioSystem& audio, Dark::AssetManager& assets);
    void playSelected(Dark::Audio::AudioSystem& audio, Dark::AssetManager& assets);
    void followListener(Dark::Audio::AudioSystem& audio);

    std::vector<Dark::AudioPreviewEntry>        m_entries;
    int                                          m_selected = -1;
    char                                         m_filter[64]{};
    float                                        m_volume   = 1.0f;
    float                                        m_distance = 8.0f;
    bool                                         m_loop     = false;
    bool                                         m_spatial  = false;
    bool                                         m_scanned  = false;
    Dark::Audio::VoiceId                         m_voice = 0;
    std::string                                  m_loadedPath;
    Dark::AssetRef<Dark::Audio::SoundClip>       m_clip;
    char                                         m_status[160]{};
};
