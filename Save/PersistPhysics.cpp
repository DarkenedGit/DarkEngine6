#include "ECS/World.h"
#include "Physics/PhysicsBodyComponent.h"
#include "Save/SaveBinding.h"
#include "Save/SaveJson.h"
#include "Save/SaveTypes.h"

namespace Dark
{
        using Save::SaveReader;
        using Save::SaveWriter;

        bool includeBody(const void* component)
        {
            return static_cast<const PhysicsBodyComponent*>(component)->mode == PhysicsBodyMode::Dynamic;
        }

        void captureBody(const void* component, void* writer)
        {
            const auto& body = *static_cast<const PhysicsBodyComponent*>(component);
            auto&       out  = *static_cast<SaveWriter*>(writer);
            out.vec3("linear", body.linearVelocity);
            out.vec3("angular", body.angularVelocity);
        }

        void applyBody(void* component, void* reader, uint16_t)
        {
            auto& body = *static_cast<PhysicsBodyComponent*>(component);
            auto& in   = *static_cast<SaveReader*>(reader);
            Math::Vector3f linear = body.linearVelocity;
            Math::Vector3f angular = body.angularVelocity;
            if (!in.vec3("linear", linear, Save::kMaxAbsPosition) || !in.vec3("angular", angular, Save::kMaxAbsPosition))
                return;
            body.linearVelocity  = linear;
            body.angularVelocity = angular;
        }

    const PersistFns PhysicsBodyComponent::kPersist{ "PhysicsBody", PhysicsBodyComponent::kSaveVersion, 110, includeBody, captureBody, applyBody, nullptr };

    namespace
    {
        struct PersistReg
        {
            PersistReg() { Save::bindPersist<PhysicsBodyComponent>(); }
        };
        const PersistReg g_persistPhysics;
    }

} // namespace Dark
