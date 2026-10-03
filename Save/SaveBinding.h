#pragma once

#include "ECS/Entity.h"
#include "ECS/Persist.h"
#include "ECS/World.h"

#include <string_view>

namespace Dark
{
    namespace Save
    {
        struct SaveBinding
        {
            const PersistFns* fns = nullptr;
            // inserted is optional. It is true only when this call default-constructed the component.
            void* (*getOrEmplace)(World& world, Entity e, bool* inserted) = nullptr;
            void (*remove)(World& world, Entity e)                        = nullptr;
        };

        bool              bindPersistRaw(const SaveBinding& binding);
        const SaveBinding* findSaveBinding(std::string_view key);

        template <typename T> bool bindPersist()
        {
            if (!T::kPersist.key || !T::kTypeName || std::string_view(T::kPersist.key) != T::kTypeName)
                return false;
            if (!T::kPersist.capture || !T::kPersist.apply)
                return false;
            SaveBinding binding;
            binding.fns = &T::kPersist;
            binding.getOrEmplace = [](World& world, Entity e, bool* inserted) -> void* {
                if (T* existing = world.get<T>(e))
                {
                    if (inserted)
                        *inserted = false;
                    return existing;
                }
                if (inserted)
                    *inserted = true;
                return &world.emplace<T>(e);
            };
            binding.remove = [](World& world, Entity e) {
                world.remove<T>(e);
            };
            return bindPersistRaw(binding);
        }

    } // namespace Save
} // namespace Dark
