#include "Save/SaveSystem.h"

#include "Core/Log.h"
#include "Core/Version.h"
#include "ECS/Persist.h"
#include "ECS/World.h"
#include "Network/NetTypes.h"
#include "Save/AtomicFile.h"
#include "Save/PersistentId.h"
#include "Save/ProgressComponents.h"
#include "Save/SaveBinding.h"
#include "Save/SaveJson.h"
#include "Save/SavePaths.h"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

namespace Dark::Save
{
    namespace
    {
        struct CompRef
        {
            const PersistFns* fns = nullptr;
            void*             ptr = nullptr;
            EntityID          id  = 0;
        };

        struct SavedEntity
        {
            uint64_t                 pid = 0;
            PersistOrigin            origin = PersistOrigin::Authored;
            std::string              archetype;
            const nlohmann::ordered_json* components = nullptr;
            SavePose                 pose{};
        };

        void visitCollect(EntityID id, void* component, void* user)
        {
            auto* pack = static_cast<std::pair<std::vector<CompRef>*, const PersistFns*>*>(user);
            if (!pack || !pack->second || !component)
                return;
            if (pack->second->include && !pack->second->include(component))
                return;
            pack->first->push_back(CompRef{ pack->second, component, id });
        }

        std::string trimDisplay(std::string_view text)
        {
            if (text.size() <= static_cast<size_t>(kMaxDisplayBytes))
                return std::string(text);
            size_t n = static_cast<size_t>(kMaxDisplayBytes);
            while (n > 0 && (static_cast<unsigned char>(text[n]) & 0xC0) == 0x80)
                --n;
            return std::string(text.substr(0, n));
        }

        std::string utcNow()
        {
            SYSTEMTIME st{};
            GetSystemTime(&st);
            return std::format("{:04}-{:02}-{:02}T{:02}:{:02}:{:02}Z", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        }

        const nlohmann::ordered_json* findChild(const nlohmann::ordered_json& obj, const char* key)
        {
            if (!obj.is_object() || !key)
                return nullptr;
            const auto it = obj.find(key);
            if (it == obj.end())
                return nullptr;
            return &(*it);
        }

        bool readText(const nlohmann::ordered_json& obj, const char* key, std::string& out)
        {
            const auto* node = findChild(obj, key);
            if (!node)
                return false;
            const auto* text = node->get_ptr<const std::string*>();
            if (!text)
                return false;
            out = *text;
            return true;
        }

        double playTimeOf(World& world)
        {
            double seconds = 0.0;
            world.each<WorldClockComponent>([&seconds](Entity, WorldClockComponent& clock) {
                seconds = clock.playTimeSec;
            });
            return seconds;
        }

        bool skipped(const SaveHost& host, World& world, Entity e)
        {
            return host.skipEntity && host.skipEntity(host.user, world, e);
        }

        void destroySaved(const SaveHost& host, World& world, Entity e)
        {
            if (!e.valid() || !world.alive(e))
                return;
            if (host.destroyEntity)
                host.destroyEntity(host.user, world, e);
            else
                world.destroyEntity(e);
        }

        void readPose(const nlohmann::ordered_json& components, SavePose& pose)
        {
            const auto* node = findChild(components, "Transform");
            if (!node || !node->is_object())
                return;
            SaveReader in(*node, nullptr);
            Math::Vector3f pos = pose.position;
            Math::Quaternion rot = pose.rotation;
            Math::Vector3f scl = pose.scale;
            if (!in.vec3("pos", pos, kMaxAbsPosition) || !in.quat("rot", rot) || !in.vec3("scl", scl, kMaxAbsPosition))
                return;
            pose.position = pos;
            pose.rotation = rot;
            pose.scale    = scl;
        }

        struct LoadedSave
        {
            std::vector<std::string>     removed;
            std::vector<SavedEntity>     rows;
            std::unordered_set<uint64_t> fileIds;
        };

        // Pointers in rows alias `root`.
        SaveResult readSavedRows(const nlohmann::ordered_json& root, LoadedSave& loaded)
        {
            loaded = {};
            const auto* payload = findChild(root, "payload");
            if (!payload)
                return SaveResult::BadFormat;

            if (const auto* list = findChild(*payload, "removed"))
            {
                if (!list->is_array())
                    return SaveResult::BadFormat;
                for (auto it = list->begin(); it != list->end(); ++it)
                {
                    const auto* text = it->get_ptr<const std::string*>();
                    uint64_t    pid  = 0;
                    if (!text || !fromHex16(*text, pid))
                        return SaveResult::BadFormat;
                    loaded.removed.push_back(*text);
                }
            }

            if (const auto* list = findChild(*payload, "entities"))
            {
                if (!list->is_array())
                    return SaveResult::BadFormat;
                if (static_cast<int>(list->size()) > kMaxEntities)
                    return SaveResult::LimitExceeded;
                for (auto it = list->begin(); it != list->end(); ++it)
                {
                    if (!it->is_object())
                        return SaveResult::BadFormat;
                    std::string hex;
                    if (!readText(*it, "id", hex))
                        return SaveResult::BadFormat;
                    uint64_t pid = 0;
                    if (!fromHex16(hex, pid))
                        return SaveResult::BadFormat;
                    if (!loaded.fileIds.insert(pid).second)
                        return SaveResult::DuplicateId;
                    SavedEntity row;
                    row.pid = pid;
                    std::string origin;
                    if (readText(*it, "origin", origin))
                        tryParsePersistOrigin(origin, row.origin);
                    readText(*it, "archetype", row.archetype);
                    if (row.archetype.size() > 23)
                        row.archetype.resize(23);
                    row.components = findChild(*it, "components");
                    if (row.components && !row.components->is_object())
                        return SaveResult::BadFormat;
                    if (row.components)
                        readPose(*row.components, row.pose);
                    loaded.rows.push_back(std::move(row));
                }
            }
            return SaveResult::Ok;
        }
    } // namespace

    struct SaveSystem::Impl
    {
        struct Job
        {
            enum class Op
            {
                None,
                Save,
                Load
            };
            Op                    op = Op::None;
            std::string           bytes;
            std::filesystem::path path;
            SaveKind              kind = SaveKind::Manual;
        };

        struct Intent
        {
            bool                                      active  = false;
            bool                                      loading = false;
            bool                                      waiting = false;
            SaveKind                                  kind    = SaveKind::Quick;
            std::string                               display;
            std::string                               fileName;
            std::chrono::steady_clock::time_point     since{};
        };

        SaveHost                         host{};
        std::string                      scenePath;
        std::string                      sceneHash;
        std::string                      procedural;
        std::unordered_set<uint64_t>     baseline;
        SaveResult                       last = SaveResult::Ok;
        std::mutex                       mu;
        std::condition_variable          cv;
        std::thread                      thread;
        bool                             stop     = false;
        bool                             hasJob   = false;
        bool                             hasDone  = false;
        bool                             inflight = false;
        Job                              job;
        Job                              done;
        SaveResult                       doneResult = SaveResult::Ok;
        Intent                           intent;

        ~Impl()
        {
            {
                std::lock_guard<std::mutex> lock(mu);
                stop = true;
            }
            cv.notify_all();
            if (thread.joinable())
                thread.join();
        }

        void ensureThread()
        {
            if (thread.joinable())
                return;
            thread = std::thread([this] { loop(); });
        }

        void loop()
        {
            for (;;)
            {
                Job next;
                {
                    std::unique_lock<std::mutex> lock(mu);
                    cv.wait(lock, [this] { return stop || hasJob; });
                    if (stop && !hasJob)
                        return;
                    next   = std::move(job);
                    hasJob = false;
                }
                SaveResult result = SaveResult::Ok;
                if (next.op == Job::Op::Save)
                {
                    result = writeAtomic(next.path, next.bytes);
                    next.bytes.clear();
                    next.bytes.shrink_to_fit();
                }
                else if (next.op == Job::Op::Load)
                    result = readCapped(next.path, next.bytes, kMaxFileBytes);
                {
                    std::lock_guard<std::mutex> lock(mu);
                    done       = std::move(next);
                    doneResult = result;
                    hasDone    = true;
                }
            }
        }

        int role() const
        {
            return host.netRole ? host.netRole(host.user) : 0;
        }

        uint32_t peers() const
        {
            return host.peerCount ? host.peerCount(host.user) : 0;
        }

        bool allowSave(SaveResult& why) const
        {
            const int net = role();
            if (net == static_cast<int>(NetRole::Joining) || net == static_cast<int>(NetRole::Client))
            {
                why = SaveResult::NotHost;
                return false;
            }
            if (host.canSaveNow && !host.canSaveNow(host.user, why))
            {
                if (why == SaveResult::Ok)
                    why = SaveResult::UnsafeMoment;
                return false;
            }
            why = SaveResult::Ok;
            return true;
        }

        SaveResult allowLoad() const
        {
            const int net = role();
            if (net == static_cast<int>(NetRole::Joining) || net == static_cast<int>(NetRole::Client))
                return SaveResult::NotHost;
            if (net == static_cast<int>(NetRole::Host) && peers() > 0)
                return SaveResult::NotHost;
            return SaveResult::Ok;
        }

        void indexIds(World& world, std::unordered_map<uint32_t, uint64_t>& toPid, std::unordered_map<uint64_t, Entity>& toEntity)
        {
            std::unordered_set<uint64_t> seen;
            world.each<PersistentIdComponent>([&](Entity e, PersistentIdComponent& id) {
                if (static_cast<uint64_t>(id.id) == 0 || skipped(host, world, e))
                    return;
                const uint64_t pid = static_cast<uint64_t>(id.id);
                if (!seen.insert(pid).second)
                {
                    DE_LOG_ERROR("Save: duplicate persistent id {}", toHex16(pid));
                    return;
                }
                toPid.emplace(e.id(), pid);
                toEntity.emplace(pid, e);
            });
        }

        SaveResult buildDocument(World& world, SaveKind kind, std::string_view display, std::string& jsonOut)
        {
            if (host.beforeCapture)
                host.beforeCapture(host.user, world);

            std::unordered_map<uint32_t, uint64_t> toPid;
            std::unordered_map<uint64_t, Entity>   toEntity;
            indexIds(world, toPid, toEntity);
            if (static_cast<int>(toEntity.size()) > kMaxEntities)
                return SaveResult::LimitExceeded;

            std::vector<CompRef> refs;
            world.forEachPool([&](IComponentPool& pool) {
                const PersistFns* fns = pool.persist();
                if (!fns || !fns->capture || !fns->key)
                    return;
                std::pair<std::vector<CompRef>*, const PersistFns*> pack{ &refs, fns };
                pool.visit(&visitCollect, &pack);
            });

            std::unordered_map<uint32_t, std::vector<CompRef>> byEntity;
            for (const CompRef& ref : refs)
            {
                if (!toPid.contains(ref.id))
                    continue;
                byEntity[ref.id].push_back(ref);
            }

            std::vector<uint64_t> removed;
            for (uint64_t id : baseline)
            {
                if (!toEntity.contains(id))
                    removed.push_back(id);
            }
            std::sort(removed.begin(), removed.end());

            std::vector<std::pair<std::string, EntityID>> order;
            order.reserve(toEntity.size());
            for (const auto& [pid, entity] : toEntity)
                order.push_back({ toHex16(pid), entity.id() });
            std::sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

            int                    sanitized = 0;
            int                    componentCount = 0;
            nlohmann::ordered_json payload = nlohmann::ordered_json::object();
            nlohmann::ordered_json removedJson = nlohmann::ordered_json::array();
            for (uint64_t id : removed)
                removedJson.push_back(toHex16(id));
            payload["removed"] = std::move(removedJson);

            nlohmann::ordered_json entities = nlohmann::ordered_json::array();
            for (const auto& [hex, entityId] : order)
            {
                const auto live = toEntity.find(fromHexOrZero(hex));
                if (live == toEntity.end())
                    continue;
                const PersistentIdComponent* pid = world.get<PersistentIdComponent>(live->second);
                if (!pid)
                    continue;
                auto comps = byEntity.find(entityId);
                std::vector<CompRef> local;
                if (comps != byEntity.end())
                    local = comps->second;
                std::sort(local.begin(), local.end(), [](const CompRef& a, const CompRef& b) {
                    if (a.fns->order != b.fns->order)
                        return a.fns->order < b.fns->order;
                    return std::strcmp(a.fns->key, b.fns->key) < 0;
                });

                nlohmann::ordered_json components = nlohmann::ordered_json::object();
                for (const CompRef& ref : local)
                {
                    if (components.contains(ref.fns->key))
                        continue;
                    nlohmann::ordered_json obj = nlohmann::ordered_json::object();
                    obj["v"] = ref.fns->version;
                    SaveWriter writer(obj, &toPid);
                    writer.shareSanitize(&sanitized);
                    ref.fns->capture(ref.ptr, &writer);
                    components[ref.fns->key] = std::move(obj);
                    ++componentCount;
                }

                nlohmann::ordered_json row = nlohmann::ordered_json::object();
                row["id"]         = hex;
                row["origin"]     = persistOriginName(pid->origin);
                row["archetype"]  = std::string(pid->archetype);
                row["components"] = std::move(components);
                entities.push_back(std::move(row));
            }
            payload["entities"] = std::move(entities);

            const std::string compact = payload.dump();
            const uint64_t    hash    = fnv1a64(compact.data(), compact.size());

            nlohmann::ordered_json root = nlohmann::ordered_json::object();
            root["format"] = "DarkEngine6.Save";
            root["schema"] = kSchema;
            nlohmann::ordered_json engine = nlohmann::ordered_json::object();
            engine["version"] = kEngineVersion;
            engine["git"]     = kEngineGit;
            root["engine"] = std::move(engine);
            root["host"] = host.id ? host.id : "";
            root["kind"] = toString(kind);
            root["saveId"] = toHex16(static_cast<uint64_t>(UUID{}));
            root["displayName"] = trimDisplay(display);
            root["savedAtUtc"] = utcNow();
            root["playTimeSec"] = playTimeOf(world);
            nlohmann::ordered_json scene = nlohmann::ordered_json::object();
            scene["path"]        = scenePath;
            scene["hash"]        = sceneHash;
            scene["procedural"]  = procedural;
            root["scene"] = std::move(scene);
            root["thumbnail"] = nullptr;
            nlohmann::ordered_json counts = nlohmann::ordered_json::object();
            counts["entities"]   = static_cast<int>(order.size());
            counts["components"] = componentCount;
            counts["sanitized"]  = sanitized;
            root["counts"] = std::move(counts);
            root["payload"] = std::move(payload);
            nlohmann::ordered_json integrity = nlohmann::ordered_json::object();
            integrity["algo"]    = "fnv1a64";
            integrity["payload"] = toHex16(hash);
            integrity["bytes"]   = static_cast<uint64_t>(compact.size());
            root["integrity"] = std::move(integrity);

            jsonOut = root.dump(2);
            return SaveResult::Ok;
        }

        static uint64_t fromHexOrZero(const std::string& hex)
        {
            uint64_t value = 0;
            fromHex16(hex, value);
            return value;
        }

        SaveResult checkEnvelope(const nlohmann::ordered_json& root)
        {
            const auto* format = findChild(root, "format");
            const auto* formatText = format ? format->get_ptr<const std::string*>() : nullptr;
            if (!formatText || *formatText != "DarkEngine6.Save")
                return SaveResult::BadFormat;

            const auto* schema = findChild(root, "schema");
            double schemaValue = 0.0;
            if (!schema || !readJsonNumber(*schema, schemaValue))
                return SaveResult::BadFormat;
            if (schemaValue > static_cast<double>(kSchema))
                return SaveResult::SchemaTooNew;
            if (schemaValue != static_cast<double>(kSchema))
                return SaveResult::BadFormat;

            std::string fileHost;
            readText(root, "host", fileHost);
            const std::string ourHost = host.id ? host.id : "";
            if (fileHost != ourHost)
                return SaveResult::SceneMismatch;

            const auto* scene = findChild(root, "scene");
            if (!scene || !scene->is_object())
                return SaveResult::BadFormat;
            std::string filePath;
            std::string fileHash;
            std::string fileProc;
            readText(*scene, "path", filePath);
            readText(*scene, "hash", fileHash);
            readText(*scene, "procedural", fileProc);
            if (filePath != scenePath || fileProc != procedural)
                return SaveResult::SceneMismatch;
            if (!fileHash.empty() && !sceneHash.empty() && fileHash != sceneHash)
                DE_LOG_WARN("Save: scene hash differs; continuing");

            const auto* payload = findChild(root, "payload");
            if (!payload || !payload->is_object())
                return SaveResult::BadFormat;
            const std::string compact = payload->dump();
            const auto* integrity = findChild(root, "integrity");
            if (!integrity || !integrity->is_object())
                return SaveResult::BadFormat;
            std::string algo;
            std::string expect;
            if (!readText(*integrity, "algo", algo) || algo != "fnv1a64" || !readText(*integrity, "payload", expect))
                return SaveResult::BadFormat;
            const auto* bytes = findChild(*integrity, "bytes");
            double byteCount = 0.0;
            if (!bytes || !readJsonNumber(*bytes, byteCount))
                return SaveResult::BadFormat;
            const uint64_t actual = fnv1a64(compact.data(), compact.size());
            if (static_cast<uint64_t>(byteCount) != compact.size() || expect != toHex16(actual))
                return SaveResult::ChecksumMismatch;
            return SaveResult::Ok;
        }

        SaveResult mutate(World& world, const nlohmann::ordered_json& root)
        {
            LoadedSave loaded;
            const SaveResult structure = readSavedRows(root, loaded);
            if (structure != SaveResult::Ok)
                return structure;
            const std::vector<std::string>&     removed = loaded.removed;
            const std::vector<SavedEntity>&     rows    = loaded.rows;
            const std::unordered_set<uint64_t>& fileIds = loaded.fileIds;

            std::unordered_map<uint64_t, Entity> live;
            {
                std::unordered_map<uint32_t, uint64_t> ignored;
                indexIds(world, ignored, live);
            }

            std::vector<Entity> doomed;
            for (const std::string& hex : removed)
            {
                uint64_t pid = 0;
                fromHex16(hex, pid);
                const auto it = live.find(pid);
                if (it != live.end())
                    doomed.push_back(it->second);
            }
            world.each<PersistentIdComponent>([&](Entity e, PersistentIdComponent& id) {
                if (id.origin != PersistOrigin::Spawned || skipped(host, world, e))
                    return;
                if (!fileIds.contains(static_cast<uint64_t>(id.id)))
                    doomed.push_back(e);
            });
            for (Entity e : doomed)
                destroySaved(host, world, e);

            live.clear();
            {
                std::unordered_map<uint32_t, uint64_t> ignored;
                indexIds(world, ignored, live);
            }

            for (const SavedEntity& row : rows)
            {
                if (live.contains(row.pid))
                    continue;
                if (row.archetype.empty())
                {
                    DE_LOG_WARN("Save: missing {} has no archetype", toHex16(row.pid));
                    continue;
                }
                if (row.origin != PersistOrigin::Spawned && !baseline.contains(row.pid))
                {
                    DE_LOG_WARN("Save: missing {} was not in the baseline", toHex16(row.pid));
                    continue;
                }
                if (!host.respawn)
                {
                    DE_LOG_ERROR("Save: respawn missing for {}", toHex16(row.pid));
                    continue;
                }
                Entity spawned = host.respawn(host.user, world, row.archetype, row.pose);
                if (!spawned.valid())
                {
                    DE_LOG_ERROR("Save: respawn failed for {}", toHex16(row.pid));
                    continue;
                }
                stampPersistentId(world, spawned, UUID{ row.pid }, row.origin, row.archetype);
            }

            live.clear();
            {
                std::unordered_map<uint32_t, uint64_t> ignored;
                indexIds(world, ignored, live);
            }
            std::unordered_map<uint64_t, Entity> pidToEntity = live;

            struct ApplyItem
            {
                int                           order = 100;
                const char*                   key   = "";
                Entity                        entity{};
                const nlohmann::ordered_json* obj = nullptr;
                const SaveBinding*            binding = nullptr;
                uint16_t                      version = 1;
                bool                          rejected = false;
            };
            std::vector<ApplyItem> items;
            static std::unordered_set<std::string> warnedKeys;
            for (const SavedEntity& row : rows)
            {
                const auto found = live.find(row.pid);
                if (found == live.end() || !row.components)
                    continue;
                for (auto it = row.components->begin(); it != row.components->end(); ++it)
                {
                    if (!it->is_object())
                        continue;
                    const std::string key = it.key();
                    const SaveBinding* binding = findSaveBinding(key);
                    if (!binding || !binding->fns || !binding->getOrEmplace || !binding->remove)
                    {
                        if (warnedKeys.insert(key).second)
                            DE_LOG_WARN("Save: no binding for {}", key);
                        continue;
                    }
                    int version = binding->fns->version;
                    SaveReader header(*it, nullptr);
                    if (!header.i32("v", version, 0, 65535))
                    {
                        DE_LOG_WARN("Save: {} on {} is invalid", key, toHex16(row.pid));
                        continue;
                    }
                    if (version > binding->fns->version)
                    {
                        DE_LOG_WARN("Save: {} version {} is newer than {}", key, version, binding->fns->version);
                        continue;
                    }
                    ApplyItem item;
                    item.order    = binding->fns->order;
                    item.key      = binding->fns->key;
                    item.entity   = found->second;
                    item.obj      = &(*it);
                    item.binding  = binding;
                    item.version  = static_cast<uint16_t>(version);
                    items.push_back(item);
                }
            }
            std::sort(items.begin(), items.end(), [](const ApplyItem& a, const ApplyItem& b) {
                if (a.order != b.order)
                    return a.order < b.order;
                const int cmp = std::strcmp(a.key, b.key);
                if (cmp != 0)
                    return cmp < 0;
                return a.entity.id() < b.entity.id();
            });

            for (ApplyItem& item : items)
            {
                bool  inserted  = false;
                void* component = item.binding->getOrEmplace(world, item.entity, &inserted);
                if (!component)
                    continue;
                SaveReader in(*item.obj, &pidToEntity);
                item.binding->fns->apply(component, &in, item.version);
                if (!in.failed())
                    continue;
                item.rejected = true;
                DE_LOG_WARN("Save: {} on {} left unchanged", item.key, item.entity.id());
                if (inserted)
                    item.binding->remove(world, item.entity);
            }
            for (const ApplyItem& item : items)
            {
                if (item.rejected || !item.binding->fns->bindRefs)
                    continue;
                void* component = item.binding->getOrEmplace(world, item.entity, nullptr);
                if (!component)
                    continue;
                SaveReader in(*item.obj, &pidToEntity);
                item.binding->fns->bindRefs(component, &in);
                if (in.unresolvedRefs() > 0)
                    DE_LOG_WARN("Save: {} has {} unresolved refs", item.key, in.unresolvedRefs());
            }
            return SaveResult::Ok;
        }

        SaveResult applyText(World& world, std::string_view text, bool rollback)
        {
            nlohmann::ordered_json root;
            SaveResult             error = SaveResult::Ok;
            if (!parseDocument(text, root, error))
                return error;
            const SaveResult envelope = checkEnvelope(root);
            if (envelope != SaveResult::Ok)
                return envelope;
            if (rollback)
                return mutate(world, root);

            LoadedSave loaded;
            const SaveResult structure = readSavedRows(root, loaded);
            if (structure != SaveResult::Ok)
                return structure;

            std::string snapshot;
            const SaveResult shot = buildDocument(world, SaveKind::Quick, "snapshot", snapshot);
            if (shot != SaveResult::Ok)
                return shot;
            if (host.beginLoad)
                host.beginLoad(host.user, world);
            const SaveResult mutated = mutate(world, root);
            if (mutated != SaveResult::Ok)
            {
                const SaveResult back = applyText(world, snapshot, true);
                if (back != SaveResult::Ok)
                    DE_LOG_ERROR("Save: rollback failed ({})", toString(back));
                if (host.afterApply)
                    host.afterApply(host.user, world);
                if (host.endLoad)
                    host.endLoad(host.user, world);
                return mutated;
            }
            if (host.afterApply)
                host.afterApply(host.user, world);
            if (host.endLoad)
                host.endLoad(host.user, world);
            return SaveResult::Ok;
        }

        void finishSave(const Job& saved)
        {
            if (saved.kind == SaveKind::Manual)
            {
                writeIndex();
                return;
            }
            trimRing(saved.kind, saved.path);
            writeIndex();
        }

        enum class UtcPeek
        {
            Ok,
            Unreadable,
            Corrupt
        };

        UtcPeek peekUtc(const std::filesystem::path& path, std::string& out)
        {
            std::string text;
            if (readCapped(path, text, kMaxFileBytes) != SaveResult::Ok)
                return UtcPeek::Unreadable;
            nlohmann::ordered_json root;
            SaveResult             error = SaveResult::Ok;
            if (!parseDocument(text, root, error) || !readText(root, "savedAtUtc", out))
                return UtcPeek::Corrupt;
            return UtcPeek::Ok;
        }

        void quarantine(const std::filesystem::path& path)
        {
            std::filesystem::path corrupt = path;
            corrupt += L".corrupt";
            std::error_code ec;
            std::filesystem::rename(path, corrupt, ec);
        }

        void trimRing(SaveKind kind, const std::filesystem::path& keep)
        {
            const int cap = kind == SaveKind::Quick ? kQuickRing : kind == SaveKind::Auto ? kAutoRing : 0;
            if (cap <= 0)
                return;
            const char* prefix = kind == SaveKind::Quick ? "quick_" : "auto_";
            std::vector<SaveFileInfo> files;
            if (listSaveFiles(files) != SaveResult::Ok)
                return;
            struct Row
            {
                std::string           utc;
                std::filesystem::path path;
            };
            std::vector<Row> rows;
            for (const SaveFileInfo& info : files)
            {
                if (!info.name.starts_with(prefix))
                    continue;
                Row row;
                row.path = info.path;
                const UtcPeek peek = peekUtc(info.path, row.utc);
                if (peek == UtcPeek::Unreadable)
                    continue;
                if (peek == UtcPeek::Corrupt)
                {
                    if (info.path != keep)
                        quarantine(info.path);
                    continue;
                }
                rows.push_back(std::move(row));
            }
            std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.utc < b.utc; });
            while (static_cast<int>(rows.size()) > cap)
            {
                size_t victim = 0;
                if (rows[victim].path == keep)
                    victim = rows.size() > 1 ? 1 : 0;
                if (rows[victim].path == keep)
                    break;
                std::error_code ec;
                std::filesystem::remove(rows[victim].path, ec);
                rows.erase(rows.begin() + static_cast<std::ptrdiff_t>(victim));
            }
        }

        void writeIndex()
        {
            std::vector<SaveFileInfo> files;
            if (listSaveFiles(files) != SaveResult::Ok)
                return;
            nlohmann::ordered_json root = nlohmann::ordered_json::object();
            nlohmann::ordered_json list = nlohmann::ordered_json::array();
            for (const SaveFileInfo& info : files)
            {
                std::string utc;
                if (peekUtc(info.path, utc) != UtcPeek::Ok)
                    continue;
                nlohmann::ordered_json row = nlohmann::ordered_json::object();
                row["name"] = info.name;
                row["savedAtUtc"] = utc;
                list.push_back(std::move(row));
            }
            root["files"] = std::move(list);
            const auto path = savesDirectory() / "index.json";
            if (writeAtomic(path, root.dump(2)) != SaveResult::Ok)
                DE_LOG_WARN("Save: index.json was not updated");
        }

        std::filesystem::path makeUniquePath(SaveKind kind, SaveResult& error)
        {
            const auto dir = savesDirectory();
            if (dir.empty())
            {
                error = SaveResult::NoSavesDir;
                return {};
            }
            std::error_code ec;
            if (!std::filesystem::is_directory(dir, ec))
            {
                error = SaveResult::NoSavesDir;
                return {};
            }
            for (int attempt = 0; attempt < 8; ++attempt)
            {
                std::string name;
                error = makeSaveName(kind, name);
                if (error != SaveResult::Ok)
                    return {};
                const auto path = dir / name;
                if (!std::filesystem::exists(path, ec) && !ec)
                {
                    error = SaveResult::Ok;
                    return path;
                }
            }
            error = SaveResult::IoOpenFailed;
            return {};
        }
    };

    SaveSystem::SaveSystem() :
        m(std::make_unique<Impl>())
    {
    }

    SaveSystem::~SaveSystem() = default;

    void SaveSystem::setHost(const SaveHost& host)
    {
        m->host = host;
    }

    void SaveSystem::setWorldIdentity(std::string path, std::string hash, std::string procedural)
    {
        m->scenePath   = std::move(path);
        m->sceneHash   = std::move(hash);
        m->procedural  = std::move(procedural);
    }

    void SaveSystem::captureBaseline(World& world)
    {
        m->baseline.clear();
        world.each<PersistentIdComponent>([&](Entity e, PersistentIdComponent& id) {
            if (id.origin == PersistOrigin::Spawned || static_cast<uint64_t>(id.id) == 0)
                return;
            if (skipped(m->host, world, e))
                return;
            m->baseline.insert(static_cast<uint64_t>(id.id));
        });
    }

    SaveResult SaveSystem::requestSave(SaveKind kind, std::string_view displayName)
    {
        if (m->inflight)
        {
            if (kind == SaveKind::Auto)
            {
                m->intent.active  = true;
                m->intent.loading = false;
                m->intent.kind    = SaveKind::Auto;
                m->intent.display = trimDisplay(displayName);
                return SaveResult::Ok;
            }
            return SaveResult::Busy;
        }
        if (m->intent.active && !m->intent.waiting && !m->intent.loading)
        {
            if (kind == SaveKind::Auto && m->intent.kind == SaveKind::Auto)
            {
                m->intent.display = trimDisplay(displayName);
                return SaveResult::Ok;
            }
            return SaveResult::Busy;
        }

        SaveResult why = SaveResult::Ok;
        if (!m->allowSave(why))
        {
            if (kind != SaveKind::Auto)
                return why;
            if (!m->intent.waiting)
                m->intent.since = std::chrono::steady_clock::now();
            m->intent.active  = true;
            m->intent.waiting = true;
            m->intent.loading = false;
            m->intent.kind    = SaveKind::Auto;
            m->intent.display = trimDisplay(displayName);
            return SaveResult::Ok;
        }

        m->intent.active  = true;
        m->intent.waiting = false;
        m->intent.loading = false;
        m->intent.kind    = kind;
        m->intent.display = trimDisplay(displayName);
        return SaveResult::Ok;
    }

    SaveResult SaveSystem::requestLoadName(std::string_view fileName)
    {
        if (!validSaveFileName(fileName))
            return SaveResult::PathInvalid;
        if (m->inflight || (m->intent.active && !m->intent.waiting))
            return SaveResult::Busy;
        const SaveResult gate = m->allowLoad();
        if (gate != SaveResult::Ok)
            return gate;
        const auto dir = savesDirectory();
        if (dir.empty())
            return SaveResult::NoSavesDir;
        m->intent.active   = true;
        m->intent.waiting  = false;
        m->intent.loading  = true;
        m->intent.fileName = std::string(fileName);
        return SaveResult::Ok;
    }

    SaveResult SaveSystem::requestLoadNewest(SaveKind kind)
    {
        std::vector<SaveFileInfo> files;
        const SaveResult listed = listSaveFiles(files);
        if (listed != SaveResult::Ok)
            return listed;
        const char* prefix = kind == SaveKind::Quick ? "quick_" : kind == SaveKind::Auto ? "auto_" : "save_";
        std::string bestName;
        std::string bestUtc;
        bool        sawUnreadable = false;
        for (const SaveFileInfo& info : files)
        {
            if (!info.name.starts_with(prefix))
                continue;
            std::string utc;
            const Impl::UtcPeek peek = m->peekUtc(info.path, utc);
            if (peek == Impl::UtcPeek::Unreadable)
            {
                sawUnreadable = true;
                continue;
            }
            if (peek == Impl::UtcPeek::Corrupt)
            {
                m->quarantine(info.path);
                continue;
            }
            if (utc >= bestUtc)
            {
                bestUtc  = utc;
                bestName = info.name;
            }
        }
        if (sawUnreadable)
            return SaveResult::IoReadFailed;
        if (bestName.empty())
            return SaveResult::PathInvalid;
        return requestLoadName(bestName);
    }

    void SaveSystem::service(World& world)
    {
        Impl::Job finished;
        bool      have = false;
        {
            std::lock_guard<std::mutex> lock(m->mu);
            if (m->hasDone)
            {
                finished    = std::move(m->done);
                m->last     = m->doneResult;
                m->hasDone  = false;
                m->inflight = false;
                have        = true;
            }
        }
        if (have)
        {
            if (m->last == SaveResult::Ok && finished.op == Impl::Job::Op::Save)
                m->finishSave(finished);
            else if (finished.op == Impl::Job::Op::Load)
            {
                if (m->last != SaveResult::Ok)
                    DE_LOG_WARN("Save: read failed ({})", toString(m->last));
                else
                {
                    const SaveResult gate = m->allowLoad();
                    if (gate != SaveResult::Ok)
                        m->last = gate;
                    else
                        m->last = m->applyText(world, finished.bytes, false);
                }
            }
        }

        if (m->inflight || !m->intent.active)
            return;

        if (m->intent.waiting)
        {
            SaveResult why = SaveResult::Ok;
            if (!m->allowSave(why))
            {
                const auto elapsed = std::chrono::steady_clock::now() - m->intent.since;
                if (elapsed > std::chrono::seconds(10))
                {
                    DE_LOG_WARN("Save: skipped autosave ({})", toString(why));
                    m->intent = {};
                    m->last   = why;
                }
                return;
            }
            m->intent.waiting = false;
        }

        if (m->intent.loading)
        {
            const SaveResult gate = m->allowLoad();
            if (gate != SaveResult::Ok)
            {
                m->last   = gate;
                m->intent = {};
                return;
            }
            m->ensureThread();
            {
                std::lock_guard<std::mutex> lock(m->mu);
                m->job.op    = Impl::Job::Op::Load;
                m->job.path  = savesDirectory() / m->intent.fileName;
                m->job.bytes.clear();
                m->hasJob    = true;
                m->inflight  = true;
            }
            m->intent = {};
            m->cv.notify_one();
            return;
        }

        SaveResult why = SaveResult::Ok;
        if (!m->allowSave(why))
        {
            m->last   = why;
            m->intent = {};
            return;
        }
        std::string json;
        const SaveResult built = m->buildDocument(world, m->intent.kind, m->intent.display, json);
        if (built != SaveResult::Ok)
        {
            m->last   = built;
            m->intent = {};
            return;
        }
        SaveResult pathError = SaveResult::Ok;
        const auto path = m->makeUniquePath(m->intent.kind, pathError);
        if (path.empty())
        {
            m->last   = pathError;
            m->intent = {};
            return;
        }
        m->ensureThread();
        {
            std::lock_guard<std::mutex> lock(m->mu);
            m->job.op    = Impl::Job::Op::Save;
            m->job.path  = path;
            m->job.bytes = std::move(json);
            m->job.kind  = m->intent.kind;
            m->hasJob    = true;
            m->inflight  = true;
        }
        m->intent = {};
        m->cv.notify_one();
    }

    SaveResult SaveSystem::lastResult() const
    {
        return m->last;
    }

    bool SaveSystem::busy() const
    {
        return m->inflight || (m->intent.active && !m->intent.waiting);
    }

    SaveResult SaveSystem::capturePayload(World& world, std::string& jsonOut)
    {
        return m->buildDocument(world, SaveKind::Manual, "", jsonOut);
    }

    SaveResult SaveSystem::applyPayloadText(World& world, std::string_view text)
    {
        const SaveResult result = m->applyText(world, text, false);
        m->last = result;
        return result;
    }

    SaveResult SaveSystem::saveNow(World& world, const std::filesystem::path& path, SaveKind kind, std::string_view displayName)
    {
        const auto name = path.filename().string();
        if (!validSaveFileName(name))
            return SaveResult::PathInvalid;
        SaveResult why = SaveResult::Ok;
        if (!m->allowSave(why))
            return why;
        std::string json;
        const SaveResult built = m->buildDocument(world, kind, displayName, json);
        if (built != SaveResult::Ok)
            return built;
        const SaveResult wrote = writeAtomic(path, json);
        m->last = wrote;
        if (wrote == SaveResult::Ok && path.parent_path() == savesDirectory())
            m->finishSave(Impl::Job{ Impl::Job::Op::Save, {}, path, kind });
        return wrote;
    }

    SaveResult SaveSystem::loadNow(World& world, const std::filesystem::path& path)
    {
        const auto name = path.filename().string();
        if (!validSaveFileName(name))
            return SaveResult::PathInvalid;
        const SaveResult gate = m->allowLoad();
        if (gate != SaveResult::Ok)
            return gate;
        std::string text;
        const SaveResult read = readCapped(path, text, kMaxFileBytes);
        if (read != SaveResult::Ok)
        {
            m->last = read;
            return read;
        }
        const SaveResult applied = m->applyText(world, text, false);
        m->last = applied;
        return applied;
    }

} // namespace Dark::Save
