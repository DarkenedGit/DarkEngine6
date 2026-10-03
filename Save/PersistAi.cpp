#include "AI/AiComponents.h"
#include "Core/Log.h"
#include "AI/WolfApproach.h"
#include "ECS/World.h"
#include "Save/SaveBinding.h"
#include "Save/SaveJson.h"
#include "Save/SaveTypes.h"

#include <string>
#include <vector>

namespace Dark
{
        const char* stalkName(AI::WolfStalk stalk)
        {
            switch (stalk)
            {
            case AI::WolfStalk::Inactive: return "Inactive";
            case AI::WolfStalk::BackOff:  return "BackOff";
            case AI::WolfStalk::Flank:    return "Flank";
            case AI::WolfStalk::Commit:   return "Commit";
            }
            return "Inactive";
        }

        const char* sideName(AI::WolfApproachSide side)
        {
            switch (side)
            {
            case AI::WolfApproachSide::Left:   return "Left";
            case AI::WolfApproachSide::Right:  return "Right";
            case AI::WolfApproachSide::Behind: return "Behind";
            }
            return "Behind";
        }

        bool stalkFromName(std::string_view name, AI::WolfStalk& out)
        {
            for (int i = 0; i <= static_cast<int>(AI::WolfStalk::Commit); ++i)
            {
                const auto stalk = static_cast<AI::WolfStalk>(i);
                if (name == stalkName(stalk))
                {
                    out = stalk;
                    return true;
                }
            }
            return false;
        }

        bool sideFromName(std::string_view name, AI::WolfApproachSide& out)
        {
            for (int i = 0; i <= static_cast<int>(AI::WolfApproachSide::Behind); ++i)
            {
                const auto side = static_cast<AI::WolfApproachSide>(i);
                if (name == sideName(side))
                {
                    out = side;
                    return true;
                }
            }
            return false;
        }

        using Save::SaveReader;
        using Save::SaveWriter;

        void captureAgent(const void* component, void* writer)
        {
            const auto& agent = *static_cast<const AiAgentComponent*>(component);
            auto&       out   = *static_cast<SaveWriter*>(writer);
            out.vec3("forward", agent.forward);
            out.vec3("planarVelocity", agent.planarVelocity);
            out.vec3("lastSeen", agent.lastSeen);
            out.vec3("wanderDest", agent.wanderDest);
            out.vec3("helpPos", agent.helpPos);
            out.f32("deadFor", agent.deadFor);
            out.f32("assistLeft", agent.assistLeft);
            out.f32("fleeLeft", agent.fleeLeft);
            out.f32("attackGap", agent.attackGap);
            out.f32("attackPause", agent.attackPause);
            out.f32("repositionLeft", agent.repositionLeft);
            out.f32("meleeWindup", agent.meleeWindup);
            out.i32("attackChain", agent.attackChain);
            out.i32("lastAttack", agent.lastAttack);
            out.i32("meleeAttack", agent.meleeAttack);
            out.boolean("preferOtherAttack", agent.preferOtherAttack);
            out.boolean("givenUp", agent.givenUp);
            out.vec3("repositionDest", agent.repositionDest);
            out.boolean("hasLastSeen", agent.hasLastSeen);
            out.string("stalk", stalkName(agent.stalk));
            out.string("stalkSide", sideName(agent.stalkSide));
            out.vec3("stalkDest", agent.stalkDest);
            out.f32("stalkLeft", agent.stalkLeft);
        }

        void applyAgent(void* component, void* reader, uint16_t)
        {
            auto& agent = *static_cast<AiAgentComponent*>(component);
            auto& in    = *static_cast<SaveReader*>(reader);
            AiAgentComponent next = agent;
            std::string stalk = stalkName(agent.stalk);
            std::string side = sideName(agent.stalkSide);
            if (!in.vec3("forward", next.forward, Save::kMaxAbsPosition) || !in.vec3("planarVelocity", next.planarVelocity, Save::kMaxAbsPosition) || !in.vec3("lastSeen", next.lastSeen, Save::kMaxAbsPosition) || !in.vec3("wanderDest", next.wanderDest, Save::kMaxAbsPosition) || !in.vec3("helpPos", next.helpPos, Save::kMaxAbsPosition))
                return;
            if (!in.f32("deadFor", next.deadFor, 0.0f, Save::kMaxTimerSeconds) || !in.f32("assistLeft", next.assistLeft, 0.0f, Save::kMaxTimerSeconds) || !in.f32("fleeLeft", next.fleeLeft, 0.0f, Save::kMaxTimerSeconds) || !in.f32("attackGap", next.attackGap, 0.0f, Save::kMaxTimerSeconds) || !in.f32("attackPause", next.attackPause, 0.0f, Save::kMaxTimerSeconds))
                return;
            if (!in.f32("repositionLeft", next.repositionLeft, 0.0f, Save::kMaxTimerSeconds) || !in.f32("meleeWindup", next.meleeWindup, 0.0f, Save::kMaxTimerSeconds))
                return;
            if (!in.i32("attackChain", next.attackChain, 0, 16) || !in.i32("lastAttack", next.lastAttack, -8, 64) || !in.i32("meleeAttack", next.meleeAttack, -8, 64))
                return;
            if (!in.boolean("preferOtherAttack", next.preferOtherAttack) || !in.boolean("givenUp", next.givenUp) || !in.vec3("repositionDest", next.repositionDest, Save::kMaxAbsPosition) || !in.boolean("hasLastSeen", next.hasLastSeen))
                return;
            if (!in.string("stalk", stalk, 32) || !in.string("stalkSide", side, 32) || !in.vec3("stalkDest", next.stalkDest, Save::kMaxAbsPosition) || !in.f32("stalkLeft", next.stalkLeft, 0.0f, Save::kMaxTimerSeconds))
                return;
            if (!stalkFromName(stalk, next.stalk))
            {
                DE_LOG_WARN("Save: unknown wolf stalk '{}'", stalk);
                next.stalk = agent.stalk;
            }
            if (!sideFromName(side, next.stalkSide))
            {
                DE_LOG_WARN("Save: unknown wolf side '{}'", side);
                next.stalkSide = agent.stalkSide;
            }
            next.footstepAcc = agent.footstepAcc;
            agent = next;
        }

        bool includeBrain(const void* component)
        {
            return static_cast<const BrainComponent*>(component)->brain != nullptr;
        }

        struct HistoryItem
        {
            std::string composite;
            std::string shallow;
            std::vector<std::string> deep;
        };

        struct HistoryCollect
        {
            std::vector<HistoryItem>* items = nullptr;
        };

        void collectHistory(AI::HsmState* composite, AI::HsmState* shallow, const std::vector<AI::HsmState*>& deep, void* user)
        {
            if (!composite)
                return;
            HistoryItem item;
            item.composite = composite->name();
            if (shallow)
                item.shallow = shallow->name();
            for (AI::HsmState* state : deep)
            {
                if (state)
                    item.deep.push_back(state->name());
            }
            static_cast<HistoryCollect*>(user)->items->push_back(std::move(item));
        }

        void captureBrain(const void* component, void* writer)
        {
            const auto& brain = *static_cast<const BrainComponent*>(component)->brain;
            auto&       out   = *static_cast<SaveWriter*>(writer);
            const auto& path  = brain.graph().machine().activePath();
            out.arrayObjects("path", static_cast<int>(path.size()), [](SaveWriter& item, int index, void* user) {
                const auto& states = *static_cast<const std::vector<AI::HsmState*>*>(user);
                item.string("name", states[static_cast<size_t>(index)] ? states[static_cast<size_t>(index)]->name() : "");
            }, const_cast<std::vector<AI::HsmState*>*>(&path));
            out.f32("memoryLeft", brain.memoryLeft());
            std::vector<HistoryItem> history;
            HistoryCollect collect{ &history };
            brain.graph().machine().visitHistory(&collectHistory, &collect);
            out.arrayObjects("history", static_cast<int>(history.size()), [](SaveWriter& item, int index, void* user) {
                const auto& row = (*static_cast<const std::vector<HistoryItem>*>(user))[static_cast<size_t>(index)];
                item.string("composite", row.composite);
                item.string("shallow", row.shallow);
                item.arrayObjects("deep", static_cast<int>(row.deep.size()), [](SaveWriter& deepItem, int deepIndex, void* deepUser) {
                    const auto& names = *static_cast<const std::vector<std::string>*>(deepUser);
                    deepItem.string("name", names[static_cast<size_t>(deepIndex)]);
                }, const_cast<std::vector<std::string>*>(&row.deep));
            }, &history);
        }

        struct PathRead
        {
            std::vector<std::string>* names = nullptr;
        };

        void readPathName(SaveReader& item, int, void* user)
        {
            std::string name;
            if (!item.string("name", name, Save::kMaxStringBytes))
                return;
            static_cast<PathRead*>(user)->names->push_back(std::move(name));
        }

        struct HistoryRead
        {
            std::vector<HistoryItem>* items = nullptr;
        };

        void readDeepName(SaveReader& item, int, void* user)
        {
            std::string name;
            if (!item.string("name", name, Save::kMaxStringBytes))
                return;
            static_cast<std::vector<std::string>*>(user)->push_back(std::move(name));
        }

        void readHistory(SaveReader& item, int, void* user)
        {
            HistoryItem row;
            if (!item.string("composite", row.composite, Save::kMaxStringBytes) || !item.string("shallow", row.shallow, Save::kMaxStringBytes))
                return;
            int deepCount = -1;
            if (!item.arrayObjects("deep", 16, deepCount, &readDeepName, &row.deep))
                return;
            static_cast<HistoryRead*>(user)->items->push_back(std::move(row));
        }

        void applyBrain(void* component, void* reader, uint16_t)
        {
            auto* brain = static_cast<BrainComponent*>(component)->brain.get();
            if (!brain)
                return;
            auto& in = *static_cast<SaveReader*>(reader);
            std::vector<std::string> path;
            PathRead pathCtx{ &path };
            int pathCount = -1;
            if (!in.arrayObjects("path", 16, pathCount, &readPathName, &pathCtx))
                return;
            float memory = brain->memoryLeft();
            if (!in.f32("memoryLeft", memory, 0.0f, Save::kMaxTimerSeconds))
                return;
            std::vector<HistoryItem> history;
            HistoryRead historyCtx{ &history };
            int historyCount = -1;
            if (!in.arrayObjects("history", 32, historyCount, &readHistory, &historyCtx))
                return;
            if (pathCount >= 0 && !brain->restore(path, memory))
            {
                DE_LOG_WARN("Save: brain path did not match the graph");
                return;
            }
            if (pathCount < 0)
                brain->restore({}, memory);
            if (historyCount >= 0)
            {
                for (const HistoryItem& row : history)
                {
                    if (!brain->restoreHistoryRecord(row.composite, row.shallow, row.deep))
                        DE_LOG_WARN("Save: brain history '{}' did not match the graph", row.composite);
                }
            }
        }

    const PersistFns AiAgentComponent::kPersist{ "AiAgent", AiAgentComponent::kSaveVersion, 100, nullptr, captureAgent, applyAgent, nullptr };
    const PersistFns BrainComponent::kPersist{ "Brain", BrainComponent::kSaveVersion, 100, includeBrain, captureBrain, applyBrain, nullptr };

    namespace
    {
        struct PersistReg
        {
            PersistReg()
            {
                Save::bindPersist<AiAgentComponent>();
                Save::bindPersist<BrainComponent>();
            }
        };
        const PersistReg g_persistAi;
    }

} // namespace Dark
