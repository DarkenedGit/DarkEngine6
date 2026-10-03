#pragma once

#include "ECS/Entity.h"
#include "Math/Quaternion.h"
#include "Math/Vector3f.h"
#include "Save/SaveTypes.h"

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace Dark
{
    class World;

    namespace Save
    {
        struct SavePose
        {
            Math::Vector3f   position{};
            Math::Quaternion rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
            Math::Vector3f   scale{ 1.0f, 1.0f, 1.0f };
        };

        // Function pointers so Save/ does not include Sandbox or Editor.
        // netRole null means Idle. peerCount null means 0.
        struct SaveHost
        {
            const char* id = "";
            bool (*canSaveNow)(void* user, SaveResult& why)                                              = nullptr;
            void (*beginLoad)(void* user, World& world)                                                   = nullptr;
            void (*endLoad)(void* user, World& world)                                                     = nullptr;
            Entity (*respawn)(void* user, World& world, std::string_view archetype, const SavePose& pose) = nullptr;
            void (*destroyEntity)(void* user, World& world, Entity e)                                     = nullptr;
            bool (*skipEntity)(void* user, World& world, Entity e)                                        = nullptr;
            int (*netRole)(void* user)                                                                    = nullptr;
            uint32_t (*peerCount)(void* user)                                                             = nullptr;
            void (*beforeCapture)(void* user, World& world)                                               = nullptr;
            void (*afterApply)(void* user, World& world)                                                  = nullptr;
            void* user                                                                                     = nullptr;
        };

        class SaveSystem
        {
        public:
            SaveSystem();
            ~SaveSystem();
            SaveSystem(const SaveSystem&)            = delete;
            SaveSystem& operator=(const SaveSystem&) = delete;

            void setHost(const SaveHost& host);
            void setWorldIdentity(std::string path, std::string hash, std::string procedural);
            void captureBaseline(World& world);

            // Queues work for service(). Manual and quick return UnsafeMoment or NotHost immediately.
            SaveResult requestSave(SaveKind kind, std::string_view displayName);
            SaveResult requestLoadName(std::string_view fileName);
            SaveResult requestLoadNewest(SaveKind kind);
            void       service(World& world);

            SaveResult lastResult() const;
            bool       busy() const;

            // Synchronous. No host gate. Used by tests and by the load snapshot.
            SaveResult capturePayload(World& world, std::string& jsonOut);
            SaveResult applyPayloadText(World& world, std::string_view text);

            SaveResult saveNow(World& world, const std::filesystem::path& path, SaveKind kind, std::string_view displayName);
            SaveResult loadNow(World& world, const std::filesystem::path& path);

        private:
            struct Impl;
            std::unique_ptr<Impl> m;
        };

    } // namespace Save
} // namespace Dark
