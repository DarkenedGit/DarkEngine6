#include "Character/HealthComponent.h"
#include "Core/Log.h"
#include "Character/PlayerMotorComponent.h"
#include "Character/SkillComponent.h"
#include "Character/SkillId.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Gameplay/Coin.h"
#include "Gameplay/HealthPack.h"
#include "Save/PersistentId.h"
#include "Save/ProgressComponents.h"
#include "Save/SaveBinding.h"
#include "Save/SaveJson.h"
#include "Save/SaveTypes.h"

namespace Dark
{
        using Save::SaveReader;
        using Save::SaveWriter;

        bool namedSkill(std::string_view name, SkillId& out)
        {
            for (int i = 0; i < kSkillCount; ++i)
            {
                const char* text = skillIdName(static_cast<SkillId>(i));
                if (text && name == text)
                {
                    out = static_cast<SkillId>(i);
                    return true;
                }
            }
            return false;
        }

        const char* motorStateName(PlayerMoveState state)
        {
            switch (state)
            {
            case PlayerMoveState::Grounded: return "Grounded";
            case PlayerMoveState::Jumping:  return "Jumping";
            case PlayerMoveState::Falling:  return "Falling";
            case PlayerMoveState::Swimming: return "Swimming";
            case PlayerMoveState::Dodge:    return "Dodge";
            case PlayerMoveState::Crouch:   return "Crouch";
            }
            return "Grounded";
        }

        bool motorStateFromName(std::string_view name, PlayerMoveState& out)
        {
            for (int i = 0; i <= static_cast<int>(PlayerMoveState::Crouch); ++i)
            {
                const auto state = static_cast<PlayerMoveState>(i);
                if (name == motorStateName(state))
                {
                    out = state;
                    return true;
                }
            }
            return false;
        }

        void captureTransform(const void* component, void* writer)
        {
            const auto& xf  = *static_cast<const TransformComponent*>(component);
            auto&       out = *static_cast<SaveWriter*>(writer);
            out.vec3("pos", xf.position);
            out.quat("rot", xf.rotation);
            out.vec3("scl", xf.scale);
        }

        void applyTransform(void* component, void* reader, uint16_t)
        {
            auto& xf = *static_cast<TransformComponent*>(component);
            auto& in = *static_cast<SaveReader*>(reader);
            Math::Vector3f pos = xf.position;
            Math::Quaternion rot = xf.rotation;
            Math::Vector3f scl = xf.scale;
            if (!in.vec3("pos", pos, Save::kMaxAbsPosition) || !in.quat("rot", rot) || !in.vec3("scl", scl, Save::kMaxAbsPosition))
                return;
            xf.position = pos;
            xf.rotation = rot;
            xf.scale    = scl;
        }

        void captureHealth(const void* component, void* writer)
        {
            const auto& health = static_cast<const HealthComponent*>(component)->health;
            auto&       out    = *static_cast<SaveWriter*>(writer);
            out.f32("hp", health.hp());
            out.f32("sinceDamage", health.timeSinceDamage());
        }

        void applyHealth(void* component, void* reader, uint16_t)
        {
            auto& health = static_cast<HealthComponent*>(component)->health;
            auto& in     = *static_cast<SaveReader*>(reader);
            float hp     = health.hp();
            float since  = health.timeSinceDamage();
            const float cap = health.maxHp() < 0.0f ? 0.0f : health.maxHp();
            if (!in.f32("hp", hp, 0.0f, cap) || !in.f32("sinceDamage", since, 0.0f, Save::kMaxTimerSeconds))
                return;
            health.restore(hp, since);
        }

        void captureHit(const void* component, void* writer)
        {
            const auto& hit = static_cast<const HitReactionComponent*>(component)->hit;
            auto&       out = *static_cast<SaveWriter*>(writer);
            out.f32("stun", hit.stunRemaining());
            out.f32("knock", hit.knockbackRemaining());
            out.f32("knockTime", hit.knockTimeLeft());
            out.vec3("dir", hit.knockDirection());
        }

        void applyHit(void* component, void* reader, uint16_t)
        {
            auto& hit = static_cast<HitReactionComponent*>(component)->hit;
            auto& in  = *static_cast<SaveReader*>(reader);
            float stun = hit.stunRemaining();
            float knock = hit.knockbackRemaining();
            float knockTime = hit.knockTimeLeft();
            Math::Vector3f dir{ 0.0f, 0.0f, 1.0f };
            if (!in.f32("stun", stun, 0.0f, Save::kMaxTimerSeconds) || !in.f32("knock", knock, 0.0f, Save::kMaxTimerSeconds) || !in.f32("knockTime", knockTime, 0.0f, Save::kMaxTimerSeconds) || !in.vec3("dir", dir, Save::kMaxAbsPosition))
                return;
            hit.restore(stun, knock, knockTime, dir);
        }

        void captureSkill(const void* component, void* writer)
        {
            const auto* skill = static_cast<const SkillComponent*>(component);
            auto&       out   = *static_cast<SaveWriter*>(writer);
            out.arrayObjects("ranks", kSkillCount, [](SaveWriter& item, int index, void* user) {
                const auto* ranks = static_cast<const SkillComponent*>(user);
                item.string("id", skillIdName(static_cast<SkillId>(index)));
                item.i32("level", ranks->ranks[index].level);
                item.f32("xp", ranks->ranks[index].xp);
            }, const_cast<SkillComponent*>(skill));
            out.i32("grantMask", skill->grantMask);
        }

        struct SkillRead
        {
            SkillComponent* skill = nullptr;
        };

        void readRank(SaveReader& item, int, void* user)
        {
            auto* ctx = static_cast<SkillRead*>(user);
            std::string name;
            if (!item.string("id", name, Save::kMaxStringBytes))
                return;
            SkillId id = SkillId::Shoot;
            if (!namedSkill(name, id))
            {
                DE_LOG_WARN("Save: unknown skill '{}'", name);
                return;
            }
            int   level = ctx->skill->ranks[static_cast<int>(id)].level;
            float xp    = ctx->skill->ranks[static_cast<int>(id)].xp;
            if (!item.i32("level", level, 1, 100) || !item.f32("xp", xp, 0.0f, 1.0e7f))
                return;
            ctx->skill->ranks[static_cast<int>(id)].level = level;
            ctx->skill->ranks[static_cast<int>(id)].xp    = xp;
        }

        void applySkill(void* component, void* reader, uint16_t)
        {
            auto& skill = *static_cast<SkillComponent*>(component);
            auto& in    = *static_cast<SaveReader*>(reader);
            SkillComponent staged = skill;
            SkillRead ctx{ &staged };
            int count = 0;
            if (!in.arrayObjects("ranks", kSkillCount, count, &readRank, &ctx))
                return;
            int mask = staged.grantMask;
            if (!in.i32("grantMask", mask, 0, 255))
                return;
            staged.grantMask = static_cast<uint8_t>(mask);
            for (int i = 0; i < kSkillCount; ++i)
                skill.ranks[i] = staged.ranks[i];
            skill.grantMask = staged.grantMask;
        }

        void capturePack(const void* component, void* writer)
        {
            const auto& pack = *static_cast<const HealthPackComponent*>(component);
            auto&       out  = *static_cast<SaveWriter*>(writer);
            out.boolean("active", pack.active);
            out.f32("respawnIn", pack.respawnIn);
        }

        void applyPack(void* component, void* reader, uint16_t)
        {
            auto& pack = *static_cast<HealthPackComponent*>(component);
            auto& in   = *static_cast<SaveReader*>(reader);
            bool  active = pack.active;
            float respawn = pack.respawnIn;
            if (!in.boolean("active", active) || !in.f32("respawnIn", respawn, 0.0f, Save::kMaxTimerSeconds))
                return;
            pack.active    = active;
            pack.respawnIn = respawn;
        }

        void captureCoin(const void* component, void* writer)
        {
            const auto& coin = *static_cast<const CoinComponent*>(component);
            static_cast<SaveWriter*>(writer)->boolean("collected", coin.collected);
        }

        void applyCoin(void* component, void* reader, uint16_t)
        {
            auto& coin = *static_cast<CoinComponent*>(component);
            auto& in   = *static_cast<SaveReader*>(reader);
            bool  collected = coin.collected;
            if (!in.boolean("collected", collected))
                return;
            coin.collected = collected;
        }

        void captureLook(const void* component, void* writer)
        {
            const auto& look = *static_cast<const LookComponent*>(component);
            auto&       out  = *static_cast<SaveWriter*>(writer);
            out.f32("yaw", look.yaw);
            out.f32("pitch", look.pitch);
            out.boolean("lightOn", look.lightOn);
        }

        void applyLook(void* component, void* reader, uint16_t)
        {
            auto& look = *static_cast<LookComponent*>(component);
            auto& in   = *static_cast<SaveReader*>(reader);
            float yaw = look.yaw;
            float pitch = look.pitch;
            bool  light = look.lightOn;
            if (!in.f32("yaw", yaw, -100.0f, 100.0f) || !in.f32("pitch", pitch, -100.0f, 100.0f) || !in.boolean("lightOn", light))
                return;
            look.yaw     = yaw;
            look.pitch   = pitch;
            look.lightOn = light;
        }

        void captureClock(const void* component, void* writer)
        {
            const auto& clock = *static_cast<const WorldClockComponent*>(component);
            auto&       out   = *static_cast<SaveWriter*>(writer);
            out.f64("playTimeSec", clock.playTimeSec);
            out.f32("simTimeSec", clock.simTimeSec);
            out.f32("envTimeOfDay", clock.envTimeOfDay);
            out.f32("cloudTime", clock.cloudTime);
            out.f32("waterTime", clock.waterTime);
        }

        void applyClock(void* component, void* reader, uint16_t)
        {
            auto& clock = *static_cast<WorldClockComponent*>(component);
            auto& in    = *static_cast<SaveReader*>(reader);
            double play = clock.playTimeSec;
            float sim = clock.simTimeSec;
            float env = clock.envTimeOfDay;
            float cloud = clock.cloudTime;
            float water = clock.waterTime;
            if (!in.f64("playTimeSec", play, 0.0, 1.0e12) || !in.f32("simTimeSec", sim, 0.0f, 1.0e7f) || !in.f32("envTimeOfDay", env, -1.0e6f, 1.0e6f) || !in.f32("cloudTime", cloud, -1.0e6f, 1.0e6f) || !in.f32("waterTime", water, -1.0e6f, 1.0e6f))
                return;
            clock.playTimeSec  = play;
            clock.simTimeSec   = sim;
            clock.envTimeOfDay = env;
            clock.cloudTime    = cloud;
            clock.waterTime    = water;
        }

        void captureAiClock(const void* component, void* writer)
        {
            const auto& clock = *static_cast<const AiClockComponent*>(component);
            auto&       out   = *static_cast<SaveWriter*>(writer);
            out.f32("time", clock.time);
            out.f32("packAttackGap", clock.packAttackGap);
            out.entity("token", clock.token);
        }

        void applyAiClock(void* component, void* reader, uint16_t)
        {
            auto& clock = *static_cast<AiClockComponent*>(component);
            auto& in    = *static_cast<SaveReader*>(reader);
            float time = clock.time;
            float gap  = clock.packAttackGap;
            if (!in.f32("time", time, 0.0f, 1.0e7f) || !in.f32("packAttackGap", gap, 0.0f, Save::kMaxTimerSeconds))
                return;
            clock.time          = time;
            clock.packAttackGap = gap;
        }

        void bindAiClock(void* component, void* reader)
        {
            auto& clock = *static_cast<AiClockComponent*>(component);
            auto& in    = *static_cast<SaveReader*>(reader);
            Entity token = clock.token;
            if (!in.entityRef("token", token))
                return;
            clock.token = token;
        }

        void captureRun(const void* component, void* writer)
        {
            const auto& run = *static_cast<const RunProgressComponent*>(component);
            auto&       out = *static_cast<SaveWriter*>(writer);
            out.f32("deadTimer", run.deadTimer);
            out.f32("spawnAge", run.spawnAge);
            out.boolean("hasSpawn", run.hasSpawn);
            out.vec3("spawn", run.spawn);
        }

        void applyRun(void* component, void* reader, uint16_t)
        {
            auto& run = *static_cast<RunProgressComponent*>(component);
            auto& in  = *static_cast<SaveReader*>(reader);
            float dead = run.deadTimer;
            float age  = run.spawnAge;
            bool  has  = run.hasSpawn;
            Math::Vector3f spawn = run.spawn;
            if (!in.f32("deadTimer", dead, 0.0f, Save::kMaxTimerSeconds) || !in.f32("spawnAge", age, 0.0f, 1.0e7f) || !in.boolean("hasSpawn", has) || !in.vec3("spawn", spawn, Save::kMaxAbsPosition))
                return;
            run.deadTimer = dead;
            run.spawnAge  = age;
            run.hasSpawn  = has;
            run.spawn     = spawn;
        }

        void captureMotor(const void* component, void* writer)
        {
            const auto& motor = static_cast<const PlayerMotorComponent*>(component)->motor;
            auto&       out   = *static_cast<SaveWriter*>(writer);
            out.string("state", motorStateName(motor.state()));
            out.vec3("velocity", motor.velocity());
            out.f32("airTime", motor.airTime());
            out.f32("coyote", motor.coyoteLeft());
            out.boolean("didFirstJump", motor.didFirstJump());
            out.boolean("didDoubleJump", motor.didDoubleJump());
            out.f32("dodgeLeft", motor.dodgeTimeLeft());
            out.vec3("dodgeWish", motor.dodgeWish());
        }

        void applyMotor(void* component, void* reader, uint16_t)
        {
            auto& motor = static_cast<PlayerMotorComponent*>(component)->motor;
            auto& in    = *static_cast<SaveReader*>(reader);
            std::string stateName = motorStateName(motor.state());
            Math::Vector3f velocity = motor.velocity();
            float air = motor.airTime();
            float coyote = 0.0f;
            bool first = motor.didFirstJump();
            bool second = motor.didDoubleJump();
            float dodge = motor.dodgeTimeLeft();
            Math::Vector3f wish{};
            if (!in.string("state", stateName, 32) || !in.vec3("velocity", velocity, Save::kMaxAbsPosition) || !in.f32("airTime", air, 0.0f, Save::kMaxTimerSeconds) || !in.f32("coyote", coyote, 0.0f, Save::kMaxTimerSeconds) || !in.boolean("didFirstJump", first) || !in.boolean("didDoubleJump", second) || !in.f32("dodgeLeft", dodge, 0.0f, Save::kMaxTimerSeconds) || !in.vec3("dodgeWish", wish, Save::kMaxAbsPosition))
                return;
            PlayerMoveState state = motor.state();
            if (!motorStateFromName(stateName, state))
            {
                DE_LOG_WARN("Save: unknown motor state '{}'", stateName);
                state = motor.state();
            }
            motor.restoreProgress(state, velocity, air, coyote, first, second, dodge, wish);
        }

    const PersistFns TransformComponent::kPersist{ "Transform", TransformComponent::kSaveVersion, 10, nullptr, &captureTransform, &applyTransform, nullptr };
    const PersistFns HealthComponent::kPersist{ "Health", HealthComponent::kSaveVersion, 100, nullptr, &captureHealth, &applyHealth, nullptr };
    const PersistFns HitReactionComponent::kPersist{ "HitReaction", HitReactionComponent::kSaveVersion, 100, nullptr, &captureHit, &applyHit, nullptr };
    const PersistFns SkillComponent::kPersist{ "Skill", SkillComponent::kSaveVersion, 100, nullptr, &captureSkill, &applySkill, nullptr };
    const PersistFns HealthPackComponent::kPersist{ "HealthPack", HealthPackComponent::kSaveVersion, 100, nullptr, &capturePack, &applyPack, nullptr };
    const PersistFns CoinComponent::kPersist{ "Coin", CoinComponent::kSaveVersion, 100, nullptr, &captureCoin, &applyCoin, nullptr };
    const PersistFns LookComponent::kPersist{ "Look", LookComponent::kSaveVersion, 100, nullptr, &captureLook, &applyLook, nullptr };
    const PersistFns WorldClockComponent::kPersist{ "WorldClock", WorldClockComponent::kSaveVersion, 100, nullptr, &captureClock, &applyClock, nullptr };
    const PersistFns AiClockComponent::kPersist{ "AiClock", AiClockComponent::kSaveVersion, 100, nullptr, &captureAiClock, &applyAiClock, &bindAiClock };
    const PersistFns RunProgressComponent::kPersist{ "RunProgress", RunProgressComponent::kSaveVersion, 100, nullptr, &captureRun, &applyRun, nullptr };
    const PersistFns PlayerMotorComponent::kPersist{ "PlayerMotor", PlayerMotorComponent::kSaveVersion, 100, nullptr, &captureMotor, &applyMotor, nullptr };

    namespace
    {
        struct PersistReg
        {
            PersistReg()
            {
                Save::bindPersist<TransformComponent>();
                Save::bindPersist<HealthComponent>();
                Save::bindPersist<HitReactionComponent>();
                Save::bindPersist<SkillComponent>();
                Save::bindPersist<HealthPackComponent>();
                Save::bindPersist<CoinComponent>();
                Save::bindPersist<LookComponent>();
                Save::bindPersist<WorldClockComponent>();
                Save::bindPersist<AiClockComponent>();
                Save::bindPersist<RunProgressComponent>();
                Save::bindPersist<PlayerMotorComponent>();
            }
        };

        const PersistReg g_persistCore;
    } // namespace

} // namespace Dark
