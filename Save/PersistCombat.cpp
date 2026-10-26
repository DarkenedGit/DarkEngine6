#include "Combat/DefenseComponent.h"
#include "Combat/HitVolumes.h"
#include "Combat/JumpAttackComponent.h"
#include "Combat/PoiseComponent.h"
#include "Combat/StatusDef.h"
#include "Combat/StatusEffectComponent.h"
#include "ECS/World.h"
#include "Gameplay/ThrowableBelt.h"
#include "Gameplay/ThrowableCatalog.h"
#include "Save/SaveBinding.h"
#include "Save/SaveJson.h"
#include "Save/SaveTypes.h"
#include "Weapons/WeaponLoadoutComponent.h"

#include <string>
#include <vector>

namespace Dark
{
    namespace
    {
        const char* ccName(Combat::CcCategory category)
        {
            switch (category)
            {
            case Combat::CcCategory::Stun:      return "Stun";
            case Combat::CcCategory::Root:      return "Root";
            case Combat::CcCategory::Fear:      return "Fear";
            case Combat::CcCategory::Knockdown: return "Knockdown";
            default:                            return "ailment";
            }
        }

        bool ccFromName(std::string_view name, Combat::CcCategory& out)
        {
            for (int i = 0; i <= static_cast<int>(Combat::CcCategory::Count); ++i)
            {
                const auto category = static_cast<Combat::CcCategory>(i);
                if (name == ccName(category))
                {
                    out = category;
                    return true;
                }
            }
            return false;
        }

        Combat::StatusId statusFromName(std::string_view name)
        {
            for (int i = 1; i < Combat::kStatusIdCount; ++i)
            {
                const auto           id  = static_cast<Combat::StatusId>(i);
                const Combat::StatusDef* def = Combat::statusDef(id);
                if (def && def->name && name == def->name)
                    return id;
            }
            return Combat::StatusId::None;
        }
    } // namespace

        using Save::SaveReader;
        using Save::SaveWriter;

        void captureDefense(const void* component, void* writer)
        {
            const auto& defense = *static_cast<const Combat::DefenseComponent*>(component);
            auto&       out     = *static_cast<SaveWriter*>(writer);
            out.f32("stamina", defense.stamina);
            out.boolean("blocking", defense.blocking);
            out.boolean("parrying", defense.parrying);
            out.boolean("chargedParry", defense.chargedParry);
            out.f32("iframe", defense.iframeSecondsLeft);
            out.f32("parryWindow", defense.parryWindowLeft);
        }

        void applyDefense(void* component, void* reader, uint16_t)
        {
            auto& defense = *static_cast<Combat::DefenseComponent*>(component);
            auto& in      = *static_cast<SaveReader*>(reader);
            float stamina = defense.stamina;
            bool blocking = defense.blocking;
            bool parrying = defense.parrying;
            bool charged = defense.chargedParry;
            float iframe = defense.iframeSecondsLeft;
            float window = defense.parryWindowLeft;
            if (!in.f32("stamina", stamina, 0.0f, 100000.0f) || !in.boolean("blocking", blocking) || !in.boolean("parrying", parrying) || !in.boolean("chargedParry", charged) || !in.f32("iframe", iframe, 0.0f, Save::kMaxTimerSeconds) || !in.f32("parryWindow", window, 0.0f, Save::kMaxTimerSeconds))
                return;
            defense.stamina           = stamina;
            defense.blocking          = blocking;
            defense.parrying          = parrying;
            defense.chargedParry      = charged;
            defense.iframeSecondsLeft = iframe;
            defense.parryWindowLeft   = window;
        }

        void capturePoise(const void* component, void* writer)
        {
            const auto& poise = *static_cast<const Combat::PoiseComponent*>(component);
            auto&       out   = *static_cast<SaveWriter*>(writer);
            out.f32("poise", poise.poise);
            out.f32("sinceDamage", poise.sinceDamage);
            out.boolean("hyperArmor", poise.hyperArmor);
        }

        void applyPoise(void* component, void* reader, uint16_t)
        {
            auto& poise = *static_cast<Combat::PoiseComponent*>(component);
            auto& in    = *static_cast<SaveReader*>(reader);
            float value = poise.poise;
            float since = poise.sinceDamage;
            bool  armor = poise.hyperArmor;
            if (!in.f32("poise", value, 0.0f, 100000.0f) || !in.f32("sinceDamage", since, 0.0f, Save::kMaxTimerSeconds) || !in.boolean("hyperArmor", armor))
                return;
            poise.poise       = value;
            poise.sinceDamage = since;
            poise.hyperArmor  = armor;
        }

        struct StatusCapture
        {
            const Combat::StatusEffectComponent* status = nullptr;
        };

        void writeSlot(SaveWriter& item, int index, void* user)
        {
            const auto& slot = static_cast<StatusCapture*>(user)->status->slots[index];
            const Combat::StatusDef* def = Combat::statusDef(static_cast<Combat::StatusId>(slot.id));
            item.string("name", def && def->name ? def->name : "");
            item.f32("magnitude", slot.magnitude);
            item.f32("remaining", slot.remaining);
            item.boolean("hard", slot.hard);
            item.string("category", ccName(slot.category));
            item.i32("stacks", slot.stacks);
            item.f32("tickAcc", slot.tickAcc);
            item.entity("source", slot.source);
        }

        void writeDr(SaveWriter& item, int index, void* user)
        {
            const auto& dr = static_cast<StatusCapture*>(user)->status->dr[index];
            item.string("category", ccName(static_cast<Combat::CcCategory>(index)));
            item.i32("applications", dr.applications);
            item.f32("resetAt", dr.resetAt);
        }

        void captureStatusFx(const void* component, void* writer)
        {
            const auto* status = static_cast<const Combat::StatusEffectComponent*>(component);
            auto&       out    = *static_cast<SaveWriter*>(writer);
            StatusCapture ctx{ status };
            out.arrayObjects("slots", status->count, &writeSlot, &ctx);
            out.arrayObjects("dr", Combat::kCcCategoryCount, &writeDr, &ctx);
            out.f32("now", status->now);
        }

        struct StatusStage
        {
            Combat::StatusInstance slots[Combat::StatusEffectComponent::kMaxStatus]{};
            int count = 0;
            Combat::CcDrState dr[Combat::kCcCategoryCount]{};
            bool drSeen[Combat::kCcCategoryCount]{};
        };

        void readSlot(SaveReader& item, int index, void* user)
        {
            auto& stage = *static_cast<StatusStage*>(user);
            std::string name;
            std::string categoryName;
            Combat::StatusInstance slot{};
            float magnitude = 0.0f;
            float remaining = 0.0f;
            bool hard = false;
            int stacks = 1;
            float tick = 0.0f;
            if (!item.string("name", name, Save::kMaxStringBytes) || !item.f32("magnitude", magnitude, -1.0e6f, 1.0e6f) || !item.f32("remaining", remaining, 0.0f, Save::kMaxTimerSeconds) || !item.boolean("hard", hard) || !item.string("category", categoryName, 32) || !item.i32("stacks", stacks, 1, 255) || !item.f32("tickAcc", tick, 0.0f, Save::kMaxTimerSeconds))
                return;
            const Combat::StatusId id = statusFromName(name);
            if (id == Combat::StatusId::None)
            {
                DE_LOG_WARN("Save: unknown status '{}'", name);
                return;
            }
            Combat::CcCategory category = Combat::CcCategory::Count;
            if (!ccFromName(categoryName, category))
            {
                DE_LOG_WARN("Save: unknown status category '{}'", categoryName);
                category = Combat::CcCategory::Count;
            }
            slot.id        = static_cast<uint8_t>(id);
            slot.magnitude = magnitude;
            slot.remaining = remaining;
            slot.hard      = hard;
            slot.category  = category;
            slot.stacks    = static_cast<uint8_t>(stacks);
            slot.tickAcc   = tick;
            stage.slots[index] = slot;
            if (index + 1 > stage.count)
                stage.count = index + 1;
        }

        void readDr(SaveReader& item, int, void* user)
        {
            auto& stage = *static_cast<StatusStage*>(user);
            std::string categoryName;
            int applications = 0;
            float resetAt = 0.0f;
            if (!item.string("category", categoryName, 32) || !item.i32("applications", applications, 0, 1000) || !item.f32("resetAt", resetAt, 0.0f, 1.0e7f))
                return;
            Combat::CcCategory category = Combat::CcCategory::Count;
            if (!ccFromName(categoryName, category) || category == Combat::CcCategory::Count)
                return;
            const int index = static_cast<int>(category);
            stage.dr[index].applications = applications;
            stage.dr[index].resetAt      = resetAt;
            stage.drSeen[index]          = true;
        }

        void applyStatusFx(void* component, void* reader, uint16_t)
        {
            auto& status = *static_cast<Combat::StatusEffectComponent*>(component);
            auto& in     = *static_cast<SaveReader*>(reader);
            StatusStage stage{};
            int slotCount = -1;
            int drCount = -1;
            if (!in.arrayObjects("slots", Combat::StatusEffectComponent::kMaxStatus, slotCount, &readSlot, &stage))
                return;
            if (!in.arrayObjects("dr", Combat::kCcCategoryCount, drCount, &readDr, &stage))
                return;
            float now = status.now;
            if (!in.f32("now", now, 0.0f, 1.0e7f))
                return;
            if (slotCount >= 0)
            {
                status.count = stage.count;
                for (int i = 0; i < Combat::StatusEffectComponent::kMaxStatus; ++i)
                    status.slots[i] = i < stage.count ? stage.slots[i] : Combat::StatusInstance{};
                status.replayFxForLoad();
            }
            if (drCount >= 0)
            {
                for (int i = 0; i < Combat::kCcCategoryCount; ++i)
                {
                    if (stage.drSeen[i])
                        status.dr[i] = stage.dr[i];
                }
            }
            status.now = now;
        }

        struct SourceBind
        {
            Combat::StatusEffectComponent* status = nullptr;
        };

        void bindSlot(SaveReader& item, int index, void* user)
        {
            auto* status = static_cast<SourceBind*>(user)->status;
            if (index >= status->count)
                return;
            Entity source = status->slots[index].source;
            if (!item.entityRef("source", source))
                return;
            status->slots[index].source = source;
        }

        void bindStatusFx(void* component, void* reader)
        {
            auto& status = *static_cast<Combat::StatusEffectComponent*>(component);
            auto& in     = *static_cast<SaveReader*>(reader);
            SourceBind ctx{ &status };
            int count = 0;
            in.arrayObjects("slots", Combat::StatusEffectComponent::kMaxStatus, count, &bindSlot, &ctx);
        }

        void captureJump(const void* component, void* writer)
        {
            const auto& jump = static_cast<const JumpAttackComponent*>(component)->jump;
            static_cast<SaveWriter*>(writer)->f32("cooldown", jump.cooldownLeft());
        }

        void applyJump(void* component, void* reader, uint16_t)
        {
            auto& jump = static_cast<JumpAttackComponent*>(component)->jump;
            auto& in   = *static_cast<SaveReader*>(reader);
            float cooldown = jump.cooldownLeft();
            if (!in.f32("cooldown", cooldown, 0.0f, Save::kMaxTimerSeconds))
                return;
            jump.restoreCooldown(cooldown);
        }

        struct ShotWrite
        {
            const std::vector<LiveProjectile>* shots = nullptr;
        };

        void writeShot(SaveWriter& item, int index, void* user)
        {
            const auto& shot = (*static_cast<ShotWrite*>(user)->shots)[static_cast<size_t>(index)];
            item.vec3("pos", shot.position);
            item.vec3("prev", shot.prevPosition);
            item.vec3("vel", shot.velocity);
            item.f32("age", shot.age);
            item.f32("traveled", shot.traveled);
            item.f32("damageScale", shot.damageScale);
            item.boolean("alive", shot.alive);
        }

        constexpr int kMaxThrowCounts = 64;
        constexpr int kMaxThrowFlights = 64;
        constexpr int kMaxThrowCount = 1000000;

        struct ThrowCountWrite
        {
            const std::vector<ThrowableCountRecord>* counts = nullptr;
        };

        void writeThrowCount(SaveWriter& item, int index, void* user)
        {
            const ThrowableCountRecord& row = (*static_cast<ThrowCountWrite*>(user)->counts)[static_cast<size_t>(index)];
            item.string("id", row.id);
            item.i32("count", row.count);
        }

        struct ThrowFlightWrite
        {
            const std::vector<ThrowableFlightSnapshot>* flights = nullptr;
        };

        void writeThrowFlight(SaveWriter& item, int index, void* user)
        {
            const ThrowableFlightSnapshot& flight = (*static_cast<ThrowFlightWrite*>(user)->flights)[static_cast<size_t>(index)];
            item.string("id", flight.itemId);
            item.string("clip", flight.impactClip);
            item.f32("weight", flight.weight);
            item.f32("distance", flight.throwDistance);
            item.vec3("pos", flight.position);
            item.vec3("vel", flight.velocity);
            item.f32("gravity", flight.gravity);
            item.f32("age", flight.age);
            item.f32("life", flight.life);
            item.boolean("cleared", flight.clearOfThrower);
        }

        void captureLoadout(const void* component, void* writer)
        {
            const auto& loadout = *static_cast<const WeaponLoadoutComponent*>(component);
            auto&       out     = *static_cast<SaveWriter*>(writer);
            const int   slot    = loadout.loadout ? loadout.loadout->slot() : loadout.slot;
            out.i32("slot", slot);
            ThrowCountWrite counts{ &loadout.throwCounts };
            out.arrayObjects("counts", static_cast<int>(loadout.throwCounts.size()), &writeThrowCount, &counts);
            out.string("hand", loadout.throwHand);
            ThrowFlightWrite flights{ &loadout.throwFlights };
            out.arrayObjects("flights", static_cast<int>(loadout.throwFlights.size()), &writeThrowFlight, &flights);
            if (!loadout.loadout)
                return;
            out.f32("meleeCd", loadout.loadout->melee().cooldownLeft());
            out.f32("projectileCd", loadout.loadout->projectile().cooldownLeft());
            const auto& shots = loadout.loadout->projectile().live();
            ShotWrite ctx{ &shots };
            out.arrayObjects("shots", static_cast<int>(shots.size()), &writeShot, &ctx);
        }

        struct ShotRead
        {
            std::vector<LiveProjectile>* shots = nullptr;
        };

        void readShot(SaveReader& item, int, void* user)
        {
            LiveProjectile shot{};
            if (!item.vec3("pos", shot.position, Save::kMaxAbsPosition) || !item.vec3("prev", shot.prevPosition, Save::kMaxAbsPosition) || !item.vec3("vel", shot.velocity, Save::kMaxAbsPosition) || !item.f32("age", shot.age, 0.0f, Save::kMaxTimerSeconds) || !item.f32("traveled", shot.traveled, 0.0f, Save::kMaxAbsPosition) || !item.f32("damageScale", shot.damageScale, 0.0f, 100.0f) || !item.boolean("alive", shot.alive))
                return;
            static_cast<ShotRead*>(user)->shots->push_back(shot);
        }

        struct ThrowCountRead
        {
            std::vector<ThrowableCountRecord>* counts = nullptr;
        };

        void readThrowCount(SaveReader& item, int, void* user)
        {
            ThrowableCountRecord row;
            int count = 0;
            if (!item.string("id", row.id, Save::kMaxStringBytes) || !item.i32("count", count, 0, kMaxThrowCount))
                return;
            if (row.id.empty())
                return;
            row.count = count;
            static_cast<ThrowCountRead*>(user)->counts->push_back(std::move(row));
        }

        struct ThrowFlightRead
        {
            std::vector<ThrowableFlightSnapshot>* flights = nullptr;
        };

        void readThrowFlight(SaveReader& item, int, void* user)
        {
            ThrowableFlightSnapshot flight;
            if (!item.string("id", flight.itemId, Save::kMaxStringBytes) || !item.string("clip", flight.impactClip, Save::kMaxStringBytes) || !item.f32("weight", flight.weight, 0.0f, 1.0e6f) || !item.f32("distance", flight.throwDistance, 0.0f, Save::kMaxAbsPosition) || !item.vec3("pos", flight.position, Save::kMaxAbsPosition) || !item.vec3("vel", flight.velocity, Save::kMaxAbsPosition) || !item.f32("gravity", flight.gravity, 0.0f, 1.0e6f) || !item.f32("age", flight.age, 0.0f, Save::kMaxTimerSeconds) || !item.f32("life", flight.life, 0.0f, Save::kMaxTimerSeconds) || !item.boolean("cleared", flight.clearOfThrower))
                return;
            if (flight.itemId.empty())
                return;
            static_cast<ThrowFlightRead*>(user)->flights->push_back(std::move(flight));
        }

        bool anyThrowCount(const std::vector<ThrowableCountRecord>& counts)
        {
            for (const ThrowableCountRecord& row : counts)
            {
                if (row.count > 0)
                    return true;
            }
            return false;
        }

        void applyLoadout(void* component, void* reader, uint16_t)
        {
            auto& loadout = *static_cast<WeaponLoadoutComponent*>(component);
            auto& in      = *static_cast<SaveReader*>(reader);
            int slot = loadout.loadout ? loadout.loadout->slot() : loadout.slot;
            if (!in.i32("slot", slot, 0, 2))
                return;
            float melee = loadout.loadout ? loadout.loadout->melee().cooldownLeft() : 0.0f;
            float projectile = loadout.loadout ? loadout.loadout->projectile().cooldownLeft() : 0.0f;
            std::vector<LiveProjectile> shots;
            ShotRead ctx{ &shots };
            int shotCount = -1;
            if (loadout.loadout)
            {
                if (!in.f32("meleeCd", melee, 0.0f, Save::kMaxTimerSeconds) || !in.f32("projectileCd", projectile, 0.0f, Save::kMaxTimerSeconds))
                    return;
                if (!in.arrayObjects("shots", 64, shotCount, &readShot, &ctx))
                    return;
            }
            std::vector<ThrowableCountRecord> counts;
            std::string hand;
            std::vector<ThrowableFlightSnapshot> flights;
            ThrowCountRead countCtx{ &counts };
            ThrowFlightRead flightCtx{ &flights };
            int countRows = -1;
            int flightRows = -1;
            if (!in.arrayObjects("counts", kMaxThrowCounts, countRows, &readThrowCount, &countCtx))
                return;
            if (!in.string("hand", hand, Save::kMaxStringBytes))
                return;
            if (!in.arrayObjects("flights", kMaxThrowFlights, flightRows, &readThrowFlight, &flightCtx))
                return;
            (void)countRows;
            (void)flightRows;
            // A throwable selection with nothing carried is the sword. Do not leave the kind on throwable.
            if (slot == 2 && !anyThrowCount(counts))
            {
                slot = 0;
                hand.clear();
            }
            loadout.slot = slot;
            loadout.throwCounts = std::move(counts);
            loadout.throwHand = std::move(hand);
            loadout.throwFlights = std::move(flights);
            if (!loadout.loadout)
                return;
            loadout.loadout->selectSlot(slot);
            loadout.loadout->melee().setCooldownLeft(melee);
            loadout.loadout->projectile().setCooldownLeft(projectile);
            if (shotCount >= 0)
                loadout.loadout->projectile().restoreShots(shots);
        }

    void syncThrowableLoadout(World& world, Entity actor, ThrowableFlights& flights, ThrowableLoadoutSync dir)
    {
        if (!actor.valid() || !world.alive(actor))
            return;
        WeaponLoadoutComponent* wlc = world.get<WeaponLoadoutComponent>(actor);
        if (!wlc)
            return;

        if (dir == ThrowableLoadoutSync::OntoRecord)
        {
            wlc->throwCounts.clear();
            wlc->throwHand.clear();
            if (const ThrowableBeltComponent* beltComp = world.get<ThrowableBeltComponent>(actor))
            {
                const ThrowableBelt& belt = beltComp->belt;
                for (const ThrowableDef& item : throwableCatalog().items())
                {
                    const int n = belt.count(item.id);
                    if (n <= 0)
                        continue;
                    ThrowableCountRecord row;
                    row.id    = item.id;
                    row.count = n;
                    wlc->throwCounts.push_back(std::move(row));
                }
                wlc->throwHand.assign(belt.hand());
            }
            flights.copyLive(wlc->throwFlights);
            return;
        }

        const bool wantBelt = !wlc->throwCounts.empty() || !wlc->throwHand.empty();
        ThrowableBeltComponent* beltComp = world.get<ThrowableBeltComponent>(actor);
        if (beltComp || wantBelt)
        {
            ThrowableBelt& belt = beltComp ? beltComp->belt : throwableBelt(world, actor);
            for (const ThrowableDef& item : throwableCatalog().items())
                belt.setCount(item.id, 0);
            for (const ThrowableCountRecord& row : wlc->throwCounts)
            {
                if (row.count <= 0)
                    continue;
                belt.setCount(row.id, row.count);
            }
            belt.setHand("");
            if (!wlc->throwHand.empty())
                belt.setHand(wlc->throwHand);
        }
        flights.restoreLive(wlc->throwFlights);
    }

    namespace
    {
        constexpr const char* kHitVolumeDisableKey[Combat::HitVolumesComponent::kSlotCount] = { "d0", "d1", "d2", "d3", "d4", "d5", "d6" };
        constexpr const char* kHitVolumeWeightKey[Combat::HitVolumesComponent::kSlotCount]  = { "w0", "w1", "w2", "w3", "w4", "w5", "w6" };

        void captureHitVolumes(const void* component, void* writer)
        {
            const auto& volumes = *static_cast<const Combat::HitVolumesComponent*>(component);
            auto&       out     = *static_cast<SaveWriter*>(writer);
            for (int i = 0; i < Combat::HitVolumesComponent::kSlotCount; ++i)
            {
                out.f32(kHitVolumeDisableKey[i], volumes.disableTimer[i]);
                out.f32(kHitVolumeWeightKey[i], volumes.impactWeight[i]);
            }
        }

        void applyHitVolumes(void* component, void* reader, uint16_t)
        {
            auto& volumes = *static_cast<Combat::HitVolumesComponent*>(component);
            auto& in      = *static_cast<SaveReader*>(reader);
            float timers[Combat::HitVolumesComponent::kSlotCount];
            float weights[Combat::HitVolumesComponent::kSlotCount];
            for (int i = 0; i < Combat::HitVolumesComponent::kSlotCount; ++i)
            {
                timers[i]  = volumes.disableTimer[i];
                weights[i] = volumes.impactWeight[i];
                if (!in.f32(kHitVolumeDisableKey[i], timers[i], 0.0f, Save::kMaxTimerSeconds)
                    || !in.f32(kHitVolumeWeightKey[i], weights[i], 0.0f, Save::kMaxTimerSeconds))
                    return;
            }
            for (int i = 0; i < Combat::HitVolumesComponent::kSlotCount; ++i)
            {
                volumes.disableTimer[i] = timers[i];
                volumes.impactWeight[i] = weights[i];
            }
        }
    }

    const PersistFns Combat::HitVolumesComponent::kPersist{ "HitVolumes", Combat::HitVolumesComponent::kSaveVersion, 100, nullptr, captureHitVolumes, applyHitVolumes, nullptr };
    const PersistFns Combat::DefenseComponent::kPersist{ "Defense", Combat::DefenseComponent::kSaveVersion, 100, nullptr, captureDefense, applyDefense, nullptr };
    const PersistFns Combat::PoiseComponent::kPersist{ "Poise", Combat::PoiseComponent::kSaveVersion, 100, nullptr, capturePoise, applyPoise, nullptr };
    const PersistFns Combat::StatusEffectComponent::kPersist{ "StatusEffect", Combat::StatusEffectComponent::kSaveVersion, 100, nullptr, captureStatusFx, applyStatusFx, bindStatusFx };
    const PersistFns JumpAttackComponent::kPersist{ "JumpAttack", JumpAttackComponent::kSaveVersion, 100, nullptr, captureJump, applyJump, nullptr };
    const PersistFns WeaponLoadoutComponent::kPersist{ "WeaponLoadout", WeaponLoadoutComponent::kSaveVersion, 100, nullptr, captureLoadout, applyLoadout, nullptr };

    namespace
    {
        struct PersistReg
        {
            PersistReg()
            {
                Save::bindPersist<Combat::HitVolumesComponent>();
                Save::bindPersist<Combat::DefenseComponent>();
                Save::bindPersist<Combat::PoiseComponent>();
                Save::bindPersist<Combat::StatusEffectComponent>();
                Save::bindPersist<JumpAttackComponent>();
                Save::bindPersist<WeaponLoadoutComponent>();
            }
        };

        const PersistReg g_persistCombat;
    }

} // namespace Dark
