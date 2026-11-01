#pragma once

#include "Assets/AssetHandle.h"
#include "ECS/Entity.h"
#include "Math/Vector3f.h"
#include "Play/DialogueGraph.h"
#include "Play/DialogueRuntime.h"
#include "Play/DialogueSpeech.h"

namespace Dark
{

    class AssetManager;
    class GuardVoice;
    class QuestRuntime;
    class World;

    namespace Audio
    {
        class AudioSystem;
    }

    // Filled by the app on each call. Pointers are not owned.
    // Sandbox leaves enabled set. Editor Play sets it from play mode.
    struct DialogueHost
    {
        World*              world      = nullptr;
        AssetManager*       assets     = nullptr;
        Audio::AudioSystem* audio      = nullptr;
        GuardVoice*         guardVoice = nullptr;
        QuestRuntime*       quest      = nullptr;

        Entity player{};
        Entity session{};

        Math::Vector3f look{ 0.0f, 0.0f, 1.0f };
        Math::Vector3f cameraPos{ 0.0f, 0.0f, 0.0f };

        bool enabled   = true;
        bool uiBlocked = false;
    };

    // One conversation for Sandbox and Editor Play. Does not own the world, and is never relocated.
    // The ImGui HUD is Play/DialogueHud.h. That header is included only by the exes.
    class DialogueController
    {
    public:
        DialogueController() = default;
        ~DialogueController() = default;
        DialogueController(const DialogueController&) = delete;
        DialogueController& operator=(const DialogueController&) = delete;
        DialogueController(DialogueController&&) = delete;
        DialogueController& operator=(DialogueController&&) = delete;

        void bind(void (*fill)(void* user, DialogueHost& host), void* user);

        Entity pickTarget();
        void   tryBegin();
        void   tick(float dt);

        // Editor Play Stop. Interrupt writes a resume bookmark, then heard lines, that bookmark,
        // and session facts are cleared. Sandbox menu, pause, and cancel interrupts must not call this.
        void endPlay();

        bool active() const { return m_runtime.active(); }
        bool locksWorld() const { return m_runtime.locksWorld(); }
        bool choiceOpen() const { return m_runtime.choiceOpen(); }
        void requestSkip() { m_runtime.requestSkip(); }
        void moveChoice(int delta) { m_runtime.moveChoice(delta); }
        void commitChoice() { m_runtime.commitChoice(); }
        void interrupt(DialogueInterrupt reason) { m_runtime.interrupt(reason); }

        DialogueRuntime&       runtime() { return m_runtime; }
        const DialogueRuntime& runtime() const { return m_runtime; }
        DialogueSpeech&        speech() { return m_speech; }

        // Pulls the host. The HUD uses this for the speaker transform.
        World* world();

    private:
        void pull();
        void releaseSpeaker();

        void (*m_fill)(void* user, DialogueHost& host) = nullptr;
        void*          m_user = nullptr;
        DialogueHost   m_host{};
        DialogueRuntime m_runtime;
        DialogueSpeech  m_speech;
        AssetRef<DialogueGraphDef> m_graph;
        Entity         m_talkNpc{};
    };

} // namespace Dark
