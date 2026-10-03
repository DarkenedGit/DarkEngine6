#include "Animation/AnimGraphComponent.h"
#include "Core/Log.h"
#include "ECS/World.h"
#include "Save/SaveBinding.h"
#include "Save/SaveJson.h"
#include "Save/SaveTypes.h"

#include <string>
#include <vector>

namespace Dark
{
        using Save::SaveReader;
        using Save::SaveWriter;

        bool includeAnim(const void* component)
        {
            return static_cast<const AnimGraphComponent*>(component)->graph.def() != nullptr;
        }

        struct ParamItem
        {
            std::string name;
            bool        boolean = false;
            float       number  = 0.0f;
            bool        flag    = false;
        };

        void captureAnim(const void* component, void* writer)
        {
            const auto& anim = *static_cast<const AnimGraphComponent*>(component);
            auto&       out  = *static_cast<SaveWriter*>(writer);
            const AnimGraphDef* def = anim.graph.def();
            out.string("state", anim.graph.currentStateName() ? anim.graph.currentStateName() : "");
            out.boolean("lock", anim.graph.stateLocked());
            std::vector<ParamItem> params;
            if (def)
            {
                const int cap = static_cast<int>(def->params.size()) < 64 ? static_cast<int>(def->params.size()) : 64;
                for (int i = 0; i < cap; ++i)
                {
                    const AnimParamDef& param = def->params[static_cast<size_t>(i)];
                    if (param.type == AnimParamType::Trigger)
                        continue;
                    ParamItem item;
                    item.name = param.name;
                    item.boolean = param.type == AnimParamType::Bool;
                    if (item.boolean)
                        item.flag = anim.graph.getBool(param.name, false);
                    else
                        item.number = anim.graph.getFloat(param.name, 0.0f);
                    params.push_back(std::move(item));
                }
            }
            out.arrayObjects("params", static_cast<int>(params.size()), [](SaveWriter& item, int index, void* user) {
                const auto& param = (*static_cast<const std::vector<ParamItem>*>(user))[static_cast<size_t>(index)];
                item.string("name", param.name);
                item.string("type", param.boolean ? "bool" : "float");
                if (param.boolean)
                    item.boolean("value", param.flag);
                else
                    item.f32("value", param.number);
            }, &params);
            const char* clip = anim.graph.player().clipName();
            out.string("clip", clip ? clip : "");
            out.f32("time", anim.graph.player().time());
            out.f32("speed", anim.graph.player().speed());
            out.i32("loop", anim.graph.player().loopOverride());
            out.f32("lowerBodyYaw", anim.graph.player().lowerBodyYaw());
        }

        struct StagedParam
        {
            std::string name;
            bool        boolean = false;
            bool        flag    = false;
            float       number  = 0.0f;
        };

        struct ParamRead
        {
            const AnimGraphDef*     def    = nullptr;
            std::vector<StagedParam>* staged = nullptr;
        };

        void readParam(SaveReader& item, int, void* user)
        {
            auto* ctx = static_cast<ParamRead*>(user);
            std::string name;
            std::string type;
            if (!item.string("name", name, Save::kMaxStringBytes) || !item.string("type", type, 16))
                return;
            if (!ctx->def)
                return;
            const AnimParamDef* found = nullptr;
            for (const AnimParamDef& param : ctx->def->params)
            {
                if (param.name == name)
                {
                    found = &param;
                    break;
                }
            }
            if (!found || found->type == AnimParamType::Trigger)
            {
                DE_LOG_WARN("Save: anim param '{}' was skipped", name);
                return;
            }
            StagedParam staged;
            staged.name    = std::move(name);
            staged.boolean = found->type == AnimParamType::Bool;
            if (staged.boolean)
            {
                if (!item.boolean("value", staged.flag))
                    return;
            }
            else if (!item.f32("value", staged.number, -1.0e6f, 1.0e6f))
            {
                return;
            }
            ctx->staged->push_back(std::move(staged));
        }

        void applyAnim(void* component, void* reader, uint16_t)
        {
            auto& anim = *static_cast<AnimGraphComponent*>(component);
            auto& in   = *static_cast<SaveReader*>(reader);
            std::string state = anim.graph.currentStateName() ? anim.graph.currentStateName() : "";
            bool lock = anim.graph.stateLocked();
            std::string clip = anim.graph.player().clipName() ? anim.graph.player().clipName() : "";
            float time = anim.graph.player().time();
            float speed = anim.graph.player().speed();
            int loop = anim.graph.player().loopOverride();
            float yaw = anim.graph.player().lowerBodyYaw();
            if (!in.string("state", state, Save::kMaxStringBytes) || !in.boolean("lock", lock) || !in.string("clip", clip, Save::kMaxStringBytes) || !in.f32("time", time, 0.0f, Save::kMaxTimerSeconds) || !in.f32("speed", speed, 0.0f, 10.0f) || !in.i32("loop", loop, -1, 1) || !in.f32("lowerBodyYaw", yaw, -10.0f, 10.0f))
                return;
            std::vector<StagedParam> staged;
            ParamRead ctx{ anim.graph.def(), &staged };
            int paramCount = -1;
            if (!in.arrayObjects("params", 64, paramCount, &readParam, &ctx))
                return;
            anim.graph.snapForLoad(state, lock, clip, time, speed, loop, yaw);
            for (const StagedParam& param : staged)
            {
                if (param.boolean)
                    anim.graph.setBool(param.name, param.flag);
                else
                    anim.graph.setFloat(param.name, param.number);
            }
            anim.prevWorldValid = false;
        }

    const PersistFns AnimGraphComponent::kPersist{ "AnimGraph", AnimGraphComponent::kSaveVersion, 100, includeAnim, captureAnim, applyAnim, nullptr };

    namespace
    {
        struct PersistReg
        {
            PersistReg() { Save::bindPersist<AnimGraphComponent>(); }
        };
        const PersistReg g_persistAnim;
    }

} // namespace Dark
