#include "Gameplay/LootMark.h"
#include "Gameplay/PowerComponents.h"
#include "Play/AttachedLights.h"
#include "Play/Carry.h"
#include "Play/Interaction.h"
#include "Core/Log.h"
#include "Save/SaveBinding.h"
#include "Save/SaveJson.h"
#include "Save/SaveTypes.h"

#include <string>

namespace Dark
{
    using Save::SaveReader;
    using Save::SaveWriter;

    void capturePower(const void* component, void* writer)
    {
        const auto& box = *static_cast<const PowerBoxComponent*>(component);
        auto&       out = *static_cast<SaveWriter*>(writer);
        out.string("circuit", box.circuit);
        out.boolean("on", box.on);
        out.f32("fixFor", box.fixFor);
        out.entity("fixer", box.fixer);
        out.vec3("service", box.service);
        out.entity("checker", box.checker);
        out.vec3("check0", box.checks[0]);
        out.vec3("check1", box.checks[1]);
        out.vec3("check2", box.checks[2]);
        out.i32("checkIdx", box.checkIdx);
        out.f32("checkFor", box.checkFor);
        out.f32("checkTime", box.checkTime);
        out.vec3("pos", box.pos);
    }

    void applyPower(void* component, void* reader, uint16_t)
    {
        auto& box = *static_cast<PowerBoxComponent*>(component);
        auto& in  = *static_cast<SaveReader*>(reader);
        std::string circuit = box.circuit;
        bool        on      = box.on;
        float       fixFor  = box.fixFor;
        Math::Vector3f service = box.service;
        Math::Vector3f check0  = box.checks[0];
        Math::Vector3f check1  = box.checks[1];
        Math::Vector3f check2  = box.checks[2];
        int            checkIdx = box.checkIdx;
        float          checkFor = box.checkFor;
        float          checkTime = box.checkTime;
        Math::Vector3f pos = box.pos;
        if (!in.string("circuit", circuit, Save::kMaxStringBytes) || !in.boolean("on", on) || !in.f32("fixFor", fixFor, 0.0f, Save::kMaxTimerSeconds))
            return;
        if (!in.vec3("service", service, Save::kMaxAbsPosition) || !in.vec3("check0", check0, Save::kMaxAbsPosition) || !in.vec3("check1", check1, Save::kMaxAbsPosition) || !in.vec3("check2", check2, Save::kMaxAbsPosition))
            return;
        if (!in.i32("checkIdx", checkIdx, 0, PowerBoxComponent::kCheckCount) || !in.f32("checkFor", checkFor, 0.0f, Save::kMaxTimerSeconds) || !in.f32("checkTime", checkTime, 0.0f, Save::kMaxTimerSeconds))
            return;
        if (!in.vec3("pos", pos, Save::kMaxAbsPosition))
            return;
        box.circuit    = std::move(circuit);
        box.on         = on;
        box.fixFor     = fixFor;
        box.service    = service;
        box.checks[0]  = check0;
        box.checks[1]  = check1;
        box.checks[2]  = check2;
        box.checkIdx   = checkIdx;
        box.checkFor   = checkFor;
        box.checkTime  = checkTime;
        box.pos        = pos;
    }

    void bindPower(void* component, void* reader)
    {
        auto& box = *static_cast<PowerBoxComponent*>(component);
        auto& in  = *static_cast<SaveReader*>(reader);
        Entity fixer   = box.fixer;
        Entity checker = box.checker;
        if (!in.entityRef("fixer", fixer) || !in.entityRef("checker", checker))
            return;
        box.fixer   = fixer;
        box.checker = checker;
    }

    void captureCircuit(const void* component, void* writer)
    {
        const auto& member = *static_cast<const CircuitMemberComponent*>(component);
        static_cast<SaveWriter*>(writer)->string("circuit", member.circuit);
    }

    void applyCircuit(void* component, void* reader, uint16_t)
    {
        auto& member = *static_cast<CircuitMemberComponent*>(component);
        auto& in     = *static_cast<SaveReader*>(reader);
        std::string circuit = member.circuit;
        if (!in.string("circuit", circuit, Save::kMaxStringBytes))
            return;
        member.circuit = std::move(circuit);
    }

    void captureLoot(const void* component, void* writer)
    {
        (void)component;
        static_cast<SaveWriter*>(writer)->boolean("dropped", true);
    }

    void applyLoot(void* component, void* reader, uint16_t)
    {
        (void)component;
        (void)reader;
    }

    void captureAttached(const void* component, void* writer)
    {
        const auto& att = *static_cast<const AttachedLightComponent*>(component);
        auto&       out = *static_cast<SaveWriter*>(writer);
        out.entity("owner", att.owner);
        out.boolean("nightOnly", att.nightOnly);
        out.f32("intensity", att.intensity);
        out.f32("range", att.range);
        out.f32("innerConeDeg", att.innerConeDeg);
        out.f32("outerConeDeg", att.outerConeDeg);
        out.f32("height", att.height);
        out.f32("forward", att.forward);
        out.f32("aimDrop", att.aimDrop);
    }

    void applyAttached(void* component, void* reader, uint16_t)
    {
        auto& att = *static_cast<AttachedLightComponent*>(component);
        auto& in  = *static_cast<SaveReader*>(reader);
        bool  night = att.nightOnly;
        float intensity = att.intensity;
        float range = att.range;
        float inner = att.innerConeDeg;
        float outer = att.outerConeDeg;
        float height = att.height;
        float forward = att.forward;
        float aim = att.aimDrop;
        if (!in.boolean("nightOnly", night) || !in.f32("intensity", intensity, 0.0f, 1.0e7f) || !in.f32("range", range, 0.0f, 1.0e5f))
            return;
        if (!in.f32("innerConeDeg", inner, 0.0f, 180.0f) || !in.f32("outerConeDeg", outer, 0.0f, 180.0f))
            return;
        if (!in.f32("height", height, -100.0f, 100.0f) || !in.f32("forward", forward, -100.0f, 100.0f) || !in.f32("aimDrop", aim, -100.0f, 100.0f))
            return;
        att.nightOnly    = night;
        att.intensity    = intensity;
        att.range        = range;
        att.innerConeDeg = inner;
        att.outerConeDeg = outer;
        att.height       = height;
        att.forward      = forward;
        att.aimDrop      = aim;
    }

    void bindAttached(void* component, void* reader)
    {
        auto& att = *static_cast<AttachedLightComponent*>(component);
        auto& in  = *static_cast<SaveReader*>(reader);
        Entity owner = att.owner;
        if (!in.entityRef("owner", owner))
            return;
        att.owner = owner;
    }

    void captureCarried(const void* component, void* writer)
    {
        const auto& carried = *static_cast<const CarriedByComponent*>(component);
        auto&       out     = *static_cast<SaveWriter*>(writer);
        out.entity("carrier", carried.carrier);
        out.f32("floor", carried.floor);
    }

    void applyCarried(void* component, void* reader, uint16_t)
    {
        auto& carried = *static_cast<CarriedByComponent*>(component);
        auto& in      = *static_cast<SaveReader*>(reader);
        float floor = carried.floor;
        if (!in.f32("floor", floor, -Save::kMaxAbsPosition, Save::kMaxAbsPosition))
            return;
        carried.floor = floor;
    }

    void bindCarried(void* component, void* reader)
    {
        auto& carried = *static_cast<CarriedByComponent*>(component);
        auto& in      = *static_cast<SaveReader*>(reader);
        Entity carrier = carried.carrier;
        if (!in.entityRef("carrier", carrier))
            return;
        carried.carrier = carrier;
    }

    void captureConceal(const void* component, void* writer)
    {
        const auto& vol = *static_cast<const ConcealVolumeComponent*>(component);
        auto&       out = *static_cast<SaveWriter*>(writer);
        out.vec3("min", vol.box.Min);
        out.vec3("max", vol.box.Max);
    }

    void applyConceal(void* component, void* reader, uint16_t)
    {
        auto& vol = *static_cast<ConcealVolumeComponent*>(component);
        auto& in  = *static_cast<SaveReader*>(reader);
        Math::Vector3f min = vol.box.Min;
        Math::Vector3f max = vol.box.Max;
        if (!in.vec3("min", min, Save::kMaxAbsPosition) || !in.vec3("max", max, Save::kMaxAbsPosition))
            return;
        vol.box.Min = min;
        vol.box.Max = max;
    }

    static const char* interactKindName(InteractKind kind)
    {
        switch (kind)
        {
        case InteractKind::Loot:
            return "Loot";
        case InteractKind::Power:
            return "Power";
        case InteractKind::Guard:
            return "Guard";
        case InteractKind::Item:
            break;
        }
        return "Item";
    }

    static bool parseInteractKind(const std::string& name, InteractKind& out)
    {
        if (name == "Item")
            out = InteractKind::Item;
        else if (name == "Loot")
            out = InteractKind::Loot;
        else if (name == "Power")
            out = InteractKind::Power;
        else if (name == "Guard")
            out = InteractKind::Guard;
        else
            return false;
        return true;
    }

    void captureInteract(const void* component, void* writer)
    {
        const auto& mark = *static_cast<const InteractableComponent*>(component);
        auto&       out  = *static_cast<SaveWriter*>(writer);
        out.string("kind", interactKindName(mark.kind));
        out.f32("range", mark.range);
    }

    void applyInteract(void* component, void* reader, uint16_t)
    {
        auto& mark = *static_cast<InteractableComponent*>(component);
        auto& in   = *static_cast<SaveReader*>(reader);
        std::string kind = interactKindName(mark.kind);
        float       range = mark.range;
        if (!in.string("kind", kind, 16) || !in.f32("range", range, 0.0f, 1000.0f))
            return;
        InteractKind parsed = mark.kind;
        if (!parseInteractKind(kind, parsed))
        {
            DE_LOG_WARN("Save: unknown interact kind '{}'", kind);
            return;
        }
        mark.kind  = parsed;
        mark.range = range;
    }

    const PersistFns PowerBoxComponent::kPersist{ "PowerBox", PowerBoxComponent::kSaveVersion, 100, nullptr, &capturePower, &applyPower, &bindPower };
    const PersistFns CircuitMemberComponent::kPersist{ "CircuitMember", CircuitMemberComponent::kSaveVersion, 100, nullptr, &captureCircuit, &applyCircuit, nullptr };
    const PersistFns LootDroppedComponent::kPersist{ "LootDropped", LootDroppedComponent::kSaveVersion, 100, nullptr, &captureLoot, &applyLoot, nullptr };
    const PersistFns AttachedLightComponent::kPersist{ "AttachedLight", AttachedLightComponent::kSaveVersion, 100, nullptr, &captureAttached, &applyAttached, &bindAttached };
    const PersistFns CarriedByComponent::kPersist{ "CarriedBy", CarriedByComponent::kSaveVersion, 100, nullptr, &captureCarried, &applyCarried, &bindCarried };
    const PersistFns ConcealVolumeComponent::kPersist{ "ConcealVolume", ConcealVolumeComponent::kSaveVersion, 100, nullptr, &captureConceal, &applyConceal, nullptr };
    const PersistFns InteractableComponent::kPersist{ "Interactable", InteractableComponent::kSaveVersion, 100, nullptr, &captureInteract, &applyInteract, nullptr };

    namespace
    {
        struct PersistReg
        {
            PersistReg()
            {
                Save::bindPersist<PowerBoxComponent>();
                Save::bindPersist<CircuitMemberComponent>();
                Save::bindPersist<LootDroppedComponent>();
                Save::bindPersist<AttachedLightComponent>();
                Save::bindPersist<CarriedByComponent>();
                Save::bindPersist<ConcealVolumeComponent>();
                Save::bindPersist<InteractableComponent>();
            }
        };

        const PersistReg g_persistPlay;
    }

} // namespace Dark
