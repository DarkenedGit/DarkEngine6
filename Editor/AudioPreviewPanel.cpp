#include "Editor/AudioPreviewPanel.h"

#include "Ui/Icons.h"

#include <imgui.h>

#include <cctype>
#include <cstdio>
#include <string_view>

using namespace Dark;

namespace
{

bool containsInsensitive(std::string_view hay, std::string_view needle)
{
    if (needle.empty())
        return true;
    if (needle.size() > hay.size())
        return false;
    auto lower = [](unsigned char c) { return static_cast<char>(std::tolower(c)); };
    for (size_t i = 0; i + needle.size() <= hay.size(); ++i)
    {
        bool match = true;
        for (size_t j = 0; j < needle.size(); ++j)
        {
            if (lower(static_cast<unsigned char>(hay[i + j])) != lower(static_cast<unsigned char>(needle[j])))
            {
                match = false;
                break;
            }
        }
        if (match)
            return true;
    }
    return false;
}

} // namespace

void AudioPreviewPanel::refresh(AssetManager& assets)
{
    m_scanned = true;
    const std::filesystem::path dir = assets.resolve("audio");
    if (!listContentAudioWavs(dir, m_entries))
    {
        m_entries.clear();
        m_selected = -1;
        std::snprintf(m_status, sizeof(m_status), "content/audio was not found");
        return;
    }
    if (m_selected >= static_cast<int>(m_entries.size()))
        m_selected = -1;
    std::snprintf(m_status, sizeof(m_status), "%d wavs", static_cast<int>(m_entries.size()));
}

void AudioPreviewPanel::stopPreview(Audio::AudioSystem& audio)
{
    if (m_voice != 0)
        audio.stop(m_voice);
    m_voice = 0;
}

void AudioPreviewPanel::loadSelected(Audio::AudioSystem& audio, AssetManager& assets)
{
    if (m_selected < 0 || m_selected >= static_cast<int>(m_entries.size()))
    {
        m_clip.reset();
        m_loadedPath.clear();
        return;
    }
    const std::string& path = m_entries[static_cast<size_t>(m_selected)].virtualPath;
    if (m_clip && m_loadedPath == path)
        return;
    m_loadedPath = path;
    m_clip       = audio.loadWav(assets, path.c_str());
    if (!m_clip)
        std::snprintf(m_status, sizeof(m_status), "Could not load %s", path.c_str());
}

void AudioPreviewPanel::playSelected(Audio::AudioSystem& audio, AssetManager& assets)
{
    loadSelected(audio, assets);
    if (!m_clip)
        return;
    // Stop only this preview. stopAll would cut gameplay sounds during play mode.
    stopPreview(audio);

    Audio::PlayDesc desc{};
    desc.volume = m_volume;
    desc.loop   = m_loop;
    if (m_spatial)
    {
        const Audio::AudioListener& ear = audio.listener();
        desc.spatial                    = true;
        desc.position                   = ear.position + ear.forward * m_distance;
    }
    m_voice = audio.play(m_clip, desc);
    if (m_voice == 0)
        std::snprintf(m_status, sizeof(m_status), "Playback failed");
    else
        std::snprintf(m_status, sizeof(m_status), "Playing %s", m_loadedPath.c_str());
}

void AudioPreviewPanel::followListener(Audio::AudioSystem& audio)
{
    if (!m_spatial || m_voice == 0 || !audio.isPlaying(m_voice))
        return;
    const Audio::AudioListener& ear = audio.listener();
    audio.setVoicePosition(m_voice, ear.position + ear.forward * m_distance);
}

void AudioPreviewPanel::draw(Audio::AudioSystem& audio, AssetManager& assets, bool* open)
{
    if (open && !*open)
    {
        stopPreview(audio);
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(460.0f, 560.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Audio", open))
    {
        ImGui::End();
        if (open && !*open)
            stopPreview(audio);
        return;
    }

    if (!m_scanned)
        refresh(assets);

    ImGui::TextDisabled("Plays content/audio through the game mixer.");
    ImGui::TextDisabled("Master %.0f%%", audio.masterVolume() * 100.0f);

    const bool canPlay = audio.isValid() && m_selected >= 0 && m_selected < static_cast<int>(m_entries.size());
    ImGui::BeginDisabled(!canPlay);
    if (ImGui::Button(ICON_FA_PLAY "  Play"))
        playSelected(audio, assets);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(m_voice == 0 || !audio.isPlaying(m_voice));
    if (ImGui::Button("Stop"))
    {
        stopPreview(audio);
        std::snprintf(m_status, sizeof(m_status), "Stopped");
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_ROTATE_RIGHT "  Refresh"))
    {
        m_loadedPath.clear();
        m_clip.reset();
        refresh(assets);
        loadSelected(audio, assets);
    }
    if (!audio.isValid())
        ImGui::TextDisabled("Audio device is not available.");

    // Live gain on the playing voice. Releasing the slider used to restart the clip,
    // so a short wav never stayed up long enough to judge loudness.
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Volume");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    float volumePercent = m_volume * 100.0f;
    if (ImGui::SliderFloat("##audio_volume", &volumePercent, 0.0f, 100.0f, "%.0f%%"))
    {
        m_volume = volumePercent / 100.0f;
        if (m_voice != 0 && audio.isPlaying(m_voice))
            audio.setVoiceVolume(m_voice, m_volume);
    }

    if (ImGui::Checkbox("Loop", &m_loop) && audio.isPlaying(m_voice))
        playSelected(audio, assets);
    ImGui::SameLine();
    if (ImGui::Checkbox("Spatial", &m_spatial) && audio.isPlaying(m_voice))
        playSelected(audio, assets);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Place the sound in front of the camera. Full volume inside 2 m, silent at 64 m.");

    ImGui::BeginDisabled(!m_spatial);
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::SliderFloat("##audio_distance", &m_distance, 0.0f, 64.0f, "Distance %.1f m");
    ImGui::EndDisabled();

    if (m_clip && m_clip->valid() && m_clip->sampleRate() > 0)
    {
        const float seconds = static_cast<float>(m_clip->frameCount()) / static_cast<float>(m_clip->sampleRate());
        ImGui::Text("%u Hz   %u ch   %.2f s", m_clip->sampleRate(), static_cast<unsigned>(m_clip->channels()), seconds);
    }
    if (m_status[0] != '\0')
        ImGui::TextWrapped("%s", m_status);

    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##audio_filter", "Filter", m_filter, sizeof(m_filter));
    ImGui::Separator();

    if (ImGui::BeginChild("audio_files", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders))
    {
        const std::string_view filter = m_filter;
        for (int i = 0; i < static_cast<int>(m_entries.size()); ++i)
        {
            const AudioPreviewEntry& entry = m_entries[static_cast<size_t>(i)];
            if (!containsInsensitive(entry.relative, filter))
                continue;
            const bool playing = m_selected == i && m_voice != 0 && audio.isPlaying(m_voice);
            char       label[320];
            if (playing)
                std::snprintf(label, sizeof(label), "%s   (playing)", entry.relative.c_str());
            else
                std::snprintf(label, sizeof(label), "%s", entry.relative.c_str());
            ImGui::PushID(i);
            if (ImGui::Selectable(label, m_selected == i))
            {
                m_selected = i;
                loadSelected(audio, assets);
            }
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                m_selected = i;
                playSelected(audio, assets);
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();

    followListener(audio);
    ImGui::End();
}
