#pragma once

#include "AI/Hsm.h"
#include "Assets/AssetHandle.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Dark
{
    struct HsmEventDef
    {
        std::string name;
        AI::HsmEventId id = AI::kHsmEventNone;
    };

    struct HsmParamDef
    {
        std::string name;
        float       defaultFloat = 0.0f;
    };

    struct HsmStateDef
    {
        std::string     name;
        std::string     parent;
        std::string     initial;
        std::string     base;
        AI::HsmHistory  history          = AI::HsmHistory::None;
        bool            inheritEntryExit = true;
        std::string     onEnter;
        std::string     onExit;
    };

    struct HsmTransitionDef
    {
        std::vector<std::string> from;
        std::string              to;
        std::string              event;
        std::string              guard;
        std::string              action;
        AI::HsmTransitionKind    kind = AI::HsmTransitionKind::External;
    };

    class HsmGraphDef : public Asset
    {
    public:
        HsmGraphDef();

        std::string                    name;
        std::string                    root;
        std::string                    sourcePath;
        std::vector<HsmEventDef>       events;
        std::vector<HsmParamDef>       params;
        std::vector<HsmStateDef>       states;
        std::vector<HsmTransitionDef>  transitions;

        int32_t findStateIndex(std::string_view key) const;
        int32_t findEventIndex(std::string_view key) const;
        AI::HsmEventId eventId(std::string_view key) const;
        float          paramFloat(std::string_view key, float fallback) const;
    };

    using HsmNamedActionMap = std::unordered_map<std::string, AI::HsmAction>;
    using HsmNamedGuardMap  = std::unordered_map<std::string, AI::HsmGuard>;

    // Owns an HsmState tree wired from a HsmGraphDef. Not movable (machine holds raw state pointers).
    class HsmGraphInstance
    {
    public:
        HsmGraphInstance() = default;
        ~HsmGraphInstance() = default;
        HsmGraphInstance(const HsmGraphInstance&)            = delete;
        HsmGraphInstance& operator=(const HsmGraphInstance&) = delete;
        HsmGraphInstance(HsmGraphInstance&&)                 = delete;
        HsmGraphInstance& operator=(HsmGraphInstance&&)      = delete;

        bool build(const HsmGraphDef& def, void* owner = nullptr, const HsmNamedActionMap& actions = {}, const HsmNamedGuardMap& guards = {});
        void reset();

        bool start();
        void stop();
        bool isRunning() const;

        bool processEvent(AI::HsmEvent event);
        bool processEventNamed(std::string_view name, void* payload = nullptr, float dt = 0.0f);
        bool tick(float dt);

        bool        isIn(std::string_view stateName) const;
        const char* leafName() const;
        AI::HsmEventId eventId(std::string_view name) const;
        float          paramFloat(std::string_view name, float fallback) const;

        const HsmGraphDef* def() const { return &m_ownedDef; }
        AI::HsmMachine&       machine() { return m_machine; }
        const AI::HsmMachine& machine() const { return m_machine; }
        AI::HsmState*         findState(std::string_view name) const;

        const std::vector<std::string>& activePathNames() const { return m_pathNames; }
        void                            refreshPathNames();

    private:
        AI::HsmAction bindAction(const std::string& name, const HsmNamedActionMap& actions) const;
        AI::HsmGuard  bindGuard(const std::string& name, const HsmNamedGuardMap& guards) const;

        HsmGraphDef                                     m_ownedDef;
        const HsmGraphDef*                              m_def = nullptr;
        std::vector<std::unique_ptr<AI::HsmState>>      m_states;
        std::unordered_map<std::string, AI::HsmState*>  m_byName;
        AI::HsmMachine                                  m_machine;
        std::vector<std::string>                        m_pathNames;
        HsmNamedActionMap                               m_actions;
        HsmNamedGuardMap                                m_guards;
    };

    HsmGraphDef makeHunterHsmGraph();
    HsmGraphDef makePlayerHsmGraph();
} // namespace Dark
