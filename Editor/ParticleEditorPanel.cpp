#include "Editor/ParticleEditorPanel.h"

#include <imgui.h>
#include <cstring>

void ParticleEditorPanel::draw(Dark::ParticleEmitter& emitter, bool* open)
{
    if (open && !*open)
        return;

    if (!ImGui::Begin("Particle System", open))
    {
        ImGui::End();
        return;
    }

    Dark::ParticleEmitterDesc& d = emitter.desc();

    char nameBuf[128]{};
#if defined(_MSC_VER)
    strncpy_s(nameBuf, d.name.c_str(), _TRUNCATE);
#else
    std::strncpy(nameBuf, d.name.c_str(), sizeof(nameBuf) - 1);
#endif
    if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf)))
        d.name = nameBuf;

    ImGui::SeparatorText("Playback");
    if (ImGui::Button(emitter.isPlaying() ? "Pause" : "Play"))
    {
        if (emitter.isPlaying())
            emitter.stop(false);
        else
            emitter.play();
    }
    ImGui::SameLine();
    if (ImGui::Button("Restart"))
        emitter.restart();
    ImGui::SameLine();
    if (ImGui::Button("Clear"))
        emitter.stop(true);

    ImGui::SliderInt("Burst count", &m_burstCount, 1, 256);
    if (ImGui::Button("Emit Burst"))
    {
        m_burstRequested = true;
        m_lastBurst      = static_cast<uint32_t>(m_burstCount);
    }

    ImGui::Text("Alive: %u / %u", emitter.aliveCount(), emitter.capacity());

    ImGui::SeparatorText("Emission");
    int maxP = static_cast<int>(d.maxParticles);
    if (ImGui::SliderInt("Max particles", &maxP, 16, 8192))
    {
        d.maxParticles = static_cast<uint32_t>(maxP);
        emitter.setDesc(d); // rebuild pool
    }
    ImGui::DragFloat("Emission rate", &d.emissionRate, 1.0f, 0.0f, 2000.0f);
    ImGui::DragFloat("Duration (0=inf)", &d.duration, 0.05f, 0.0f, 60.0f);
    ImGui::Checkbox("Looping", &d.looping);
    ImGui::Checkbox("Prewarm", &d.prewarm);
    ImGui::DragFloat("Sim speed", &d.simulationSpeed, 0.05f, 0.0f, 5.0f);

    ImGui::SeparatorText("Lifetime");
    ImGui::DragFloatRange2("Lifetime", &d.lifetime.min, &d.lifetime.max, 0.05f, 0.05f, 20.0f);
    ImGui::DragFloatRange2("Start speed", &d.startSpeed.min, &d.startSpeed.max, 0.05f, 0.0f, 50.0f);
    ImGui::DragFloatRange2("Start size", &d.startSize.min, &d.startSize.max, 0.01f, 0.0f, 5.0f);
    ImGui::DragFloatRange2("End size", &d.endSize.min, &d.endSize.max, 0.01f, 0.0f, 5.0f);

    ImGui::SeparatorText("Color");
    ImGui::ColorEdit4("Start color", d.startColor);
    ImGui::ColorEdit4("End color", d.endColor);
    ImGui::Checkbox("Additive blend", &d.additiveBlend);

    ImGui::SeparatorText("Forces / direction");
    ImGui::DragFloat3("Gravity", &d.gravity.x, 0.05f);
    ImGui::DragFloat3("Direction", &d.direction.x, 0.02f);
    ImGui::SliderFloat("Spread (deg)", &d.spreadDegrees, 0.0f, 180.0f);

    ImGui::SeparatorText("Shape");
    const char* shapes[] = { "Point", "Box", "Sphere" };
    int shape = static_cast<int>(d.shape);
    if (ImGui::Combo("Spawn shape", &shape, shapes, 3))
        d.shape = static_cast<Dark::ParticleEmitterDesc::Shape>(shape);
    ImGui::DragFloat3("Shape size", &d.shapeSize.x, 0.02f, 0.0f, 20.0f);

    ImGui::Separator();
    ImGui::TextWrapped(
        "Tip: place an emitter with key 3 / type Particle, select it, and edit here. "
        "Params apply live to the selected emitter.");

    ImGui::End();
}

bool ParticleEditorPanel::consumeBurstRequest(uint32_t& outCount)
{
    if (!m_burstRequested)
        return false;
    m_burstRequested = false;
    outCount         = m_lastBurst;
    return true;
}
