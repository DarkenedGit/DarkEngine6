#include "Editor/ParticleEditorPanel.h"
#include "Ui/Icons.h"

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

    auto finishDesc = [&emitter](Dark::ParticleEmitterDesc next)
    {
        if (next.renderMode == Dark::ParticleEmitterDesc::RenderMode::Ribbon && next.simMode == Dark::ParticleSimMode::Gpu)
        {
            next.simMode = Dark::ParticleSimMode::Cpu;
        }
        if (next.simMode == Dark::ParticleSimMode::Cpu && next.maxParticles > Dark::kMaxCpuParticles)
        {
            next.maxParticles = Dark::kMaxCpuParticles;
        }
        emitter.setDesc(next);
    };

    const bool ribbon = d.renderMode == Dark::ParticleEmitterDesc::RenderMode::Ribbon;
    if (ribbon && d.simMode == Dark::ParticleSimMode::Gpu)
        finishDesc(d);

    char nameBuf[128]{};
#if defined(_MSC_VER)
    strncpy_s(nameBuf, d.name.c_str(), _TRUNCATE);
#else
    std::strncpy(nameBuf, d.name.c_str(), sizeof(nameBuf) - 1);
#endif
    if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf)))
        d.name = nameBuf;

    ImGui::SeparatorText("Playback");
    if (ImGui::Button(emitter.isPlaying() ? ICON_FA_PAUSE "  Pause" : ICON_FA_PLAY "  Play"))
    {
        if (emitter.isPlaying())
            emitter.stop(false);
        else
            emitter.play();
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_ROTATE_RIGHT "  Restart"))
        emitter.restart();
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_BROOM "  Clear"))
        emitter.stop(true);

    ImGui::SliderInt("Burst count", &m_burstCount, 1, 256);
    if (ImGui::Button(ICON_FA_BOLT "  Emit Burst"))
    {
        m_burstRequested = true;
        m_lastBurst      = static_cast<uint32_t>(m_burstCount);
    }

    if (d.simMode == Dark::ParticleSimMode::Gpu && !emitter.forceCpu())
        ImGui::Text("GPU capacity %u", d.maxParticles);
    else
        ImGui::Text("Alive: %u / %u", emitter.aliveCount(), emitter.capacity());

    ImGui::SeparatorText("Emission");
    const char* sims[] = { "CPU", "GPU" };
    int         sim    = static_cast<int>(d.simMode);
    ImGui::BeginDisabled(ribbon);
    if (ImGui::Combo("Simulation", &sim, sims, 2))
    {
        Dark::ParticleEmitterDesc next = d;
        next.simMode                   = static_cast<Dark::ParticleSimMode>(sim);
        finishDesc(next);
    }
    ImGui::EndDisabled();

    int       maxP      = static_cast<int>(d.maxParticles);
    const int maxSlider = (d.simMode == Dark::ParticleSimMode::Gpu) ? static_cast<int>(Dark::kMaxGpuParticlesPerEmitter) : 8192;
    if (ImGui::SliderInt("Max particles", &maxP, 16, maxSlider))
    {
        Dark::ParticleEmitterDesc next = d;
        next.maxParticles              = static_cast<uint32_t>(maxP);
        finishDesc(next);
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

    ImGui::SeparatorText("Render");
    const char* modes[] = { "Billboard", "Ribbon" };
    int         mode    = static_cast<int>(d.renderMode);
    if (ImGui::Combo("Render mode", &mode, modes, 2))
    {
        d.renderMode = static_cast<Dark::ParticleEmitterDesc::RenderMode>(mode);
        if (d.renderMode == Dark::ParticleEmitterDesc::RenderMode::Ribbon && d.simMode == Dark::ParticleSimMode::Gpu)
            finishDesc(d);
    }
    if (d.renderMode == Dark::ParticleEmitterDesc::RenderMode::Ribbon)
    {
        int ribbons = static_cast<int>(d.ribbonCount);
        if (ImGui::SliderInt("Ribbon count", &ribbons, 1, static_cast<int>(Dark::kMaxRibbonCount)))
            d.ribbonCount = static_cast<uint32_t>(ribbons);
        ImGui::DragFloat("Ribbon UV scale", &d.ribbonUvScale, 0.05f, 0.1f, 16.0f);
        ImGui::TextWrapped("Ribbon links particles in spawn order into a camera-facing strip. "
                           "Raise emission rate and lifetime for a longer trail; zero start speed "
                           "makes the strip follow the emitter.");
    }

    ImGui::SeparatorText("Collision");
    const char* reactions[] = { "None", "Bounce", "Stop", "Spawn secondary" };
    int         reaction    = static_cast<int>(d.collision);
    if (ImGui::Combo("Collision", &reaction, reactions, 4))
    {
        d.collision = static_cast<Dark::ParticleCollisionReaction>(reaction);
        if (d.collision == Dark::ParticleCollisionReaction::SpawnSecondary && !d.hasSecondary)
        {
            Dark::ParticleEmitterDesc next = d;
            next.hasSecondary              = true;
            finishDesc(next);
        }
    }
    if (d.simMode == Dark::ParticleSimMode::Cpu)
        ImGui::TextUnformatted("CPU emitters do not collide");

    ImGui::DragFloat("Restitution", &d.restitution, 0.01f, 0.0f, 1.0f);
    ImGui::DragFloat("Friction", &d.friction, 0.01f, 0.0f, 1.0f);
    ImGui::DragFloat("Thickness", &d.collisionThickness, 0.01f, 0.0f, 5.0f);
    ImGui::DragFloat("Bias", &d.collisionBias, 0.01f, 0.0f, 1.0f);
    ImGui::DragFloat("Rest lifetime", &d.restLifetime, 0.01f, 0.0f, 20.0f);
    int secondaryPerFrame = static_cast<int>(d.maxSecondaryPerFrame);
    if (ImGui::DragInt("Max secondary per frame", &secondaryPerFrame, 1.0f, 0, static_cast<int>(Dark::kMaxSecondaryPerFrameHard)))
        d.maxSecondaryPerFrame = static_cast<uint32_t>(secondaryPerFrame);

    if (d.collision == Dark::ParticleCollisionReaction::SpawnSecondary)
    {
        ImGui::PushID("child");
        ImGui::SeparatorText("Child");
        int childCap = static_cast<int>(d.secondary.maxParticles);
        if (ImGui::SliderInt("Capacity", &childCap, 1, static_cast<int>(Dark::kMaxSecondaryParticles)))
        {
            Dark::ParticleEmitterDesc next = d;
            next.hasSecondary              = true;
            next.secondary.maxParticles    = static_cast<uint32_t>(childCap);
            finishDesc(next);
        }

        bool childEdited = false;
        if (ImGui::DragFloatRange2("Lifetime", &d.secondary.lifetime.min, &d.secondary.lifetime.max, 0.01f, 0.0f, 20.0f))
            childEdited = true;
        if (ImGui::DragFloatRange2("Start size", &d.secondary.startSize.min, &d.secondary.startSize.max, 0.01f, 0.0f, 5.0f))
            childEdited = true;
        if (ImGui::DragFloat("Rest lifetime", &d.secondary.restLifetime, 0.01f, 0.0f, 20.0f))
            childEdited = true;
        if (ImGui::Checkbox("Additive blend", &d.secondary.additiveBlend))
            childEdited = true;
        if (childEdited && !d.hasSecondary)
        {
            Dark::ParticleEmitterDesc next = d;
            next.hasSecondary              = true;
            finishDesc(next);
        }
        ImGui::PopID();
    }

    ImGui::SeparatorText("Forces / direction");
    ImGui::DragFloat3("Gravity", &d.gravity.x, 0.05f);
    ImGui::DragFloat3("Direction", &d.direction.x, 0.02f);
    ImGui::SliderFloat("Spread (deg)", &d.spreadDegrees, 0.0f, 180.0f);

    ImGui::SeparatorText("Shape");
    const char* shapes[] = { "Point", "Box", "Sphere" };
    int         shape    = static_cast<int>(d.shape);
    if (ImGui::Combo("Spawn shape", &shape, shapes, 3))
        d.shape = static_cast<Dark::ParticleEmitterDesc::Shape>(shape);
    ImGui::DragFloat3("Shape size", &d.shapeSize.x, 0.02f, 0.0f, 20.0f);

    ImGui::Separator();
    ImGui::TextWrapped("Tip: place an emitter with key 3 / type Particle, then drag the RGB arrows to move it. "
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
