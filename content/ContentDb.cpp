#include "Content/ContentDb.h"

#include "Content/ArchetypeParams.h"
#include "Core/ContentRoots.h"
#include "Core/Log.h"
#include "Core/Paths.h"
#include "ECS/World.h"
#include "Save/SaveTypes.h"
#include "Scene/EntityMaster.h"

#include "third_party/nlohmann/json.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <queue>
#include <sstream>
#include <unordered_map>
#include <utility>

namespace Dark
{
    namespace
    {
        using json = nlohmann::json;

        struct Database
        {
            std::vector<ContentPackage>            packages;
            std::vector<Archetype>                 archetypes;
            std::vector<ContentIssue>              issues;
            std::vector<std::filesystem::path>     overlays;
            std::vector<std::string>               replication;
            std::unordered_map<std::string, size_t> byName;
            std::unordered_map<uint64_t, size_t>   byHash;
            std::unordered_map<std::string, std::string> aliases;
            std::vector<std::filesystem::path>     watched;
            std::unordered_map<std::string, std::filesystem::file_time_type> mtimes;
            uint64_t                               setHash = 0;
            bool                                   loaded  = false;
            bool                                   builtins = false;
        };

        Database gDb;
        std::vector<ComponentInfo> gComponents;
        std::vector<BehaviorInfo> gBehaviors;

        constexpr std::uintmax_t kMaxJsonBytes = 256u * 1024u;

        void issue(const std::string& packageId, const std::string& message)
        {
            gDb.issues.push_back(ContentIssue{ packageId, message });
            DE_LOG_WARN(LogCategory::Core, "Content: {}", message);
        }

        bool readFile(const std::filesystem::path& path, std::string& out)
        {
            std::error_code ec;
            if (path.empty() || !std::filesystem::is_regular_file(path, ec) || ec)
                return false;
            const auto sz = std::filesystem::file_size(path, ec);
            if (ec || sz > kMaxJsonBytes)
                return false;
            std::ifstream in(path, std::ios::binary);
            if (!in)
                return false;
            std::ostringstream ss;
            ss << in.rdbuf();
            out = ss.str();
            return true;
        }

        void watch(const std::filesystem::path& path)
        {
            std::error_code ec;
            const auto      when = std::filesystem::last_write_time(path, ec);
            if (ec)
                return;
            gDb.watched.push_back(path);
            gDb.mtimes[path.generic_string()] = when;
        }

        std::string jsonString(const json& object, const char* key)
        {
            const auto it = object.find(key);
            if (it == object.end())
                return {};
            if (const auto* s = it->get_ptr<const json::string_t*>())
                return *s;
            return {};
        }

        void rememberAlias(std::string_view from, std::string_view to)
        {
            if (from.empty() || to.empty())
                return;
            gDb.aliases[std::string(from)] = std::string(to);
        }

        void seedLegacyAliases()
        {
            const char* rows[][2] = {
                { "cube", "core:cube" },
                { "sphere", "core:sphere" },
                { "particle_emitter", "core:particle_emitter" },
                { "emitter", "core:particle_emitter" },
                { "particle", "core:particle_emitter" },
                { "platform", "core:platform" },
                { "coin", "core:coin" },
                { "spawn", "core:spawn" },
                { "player_spawn", "core:spawn" },
                { "point_light", "core:point_light" },
                { "spot_light", "core:spot_light" },
                { "ambient_light", "core:ambient_light" },
                { "ambient", "core:ambient_light" },
                { "directional_light", "core:directional_light" },
                { "directional", "core:directional_light" },
                { "sun", "core:directional_light" },
                { "player", "core:player" },
                { "hunter", "core:hunter" },
                { "enemy", "core:hunter" },
                { "wolf", "core:wolf" },
                { "model", "core:model" },
                { "gltf", "core:model" },
                { "water", "core:water" },
                { "cloud_volume", "core:cloud_volume" },
                { "cloud", "core:cloud_volume" },
                { "clouds", "core:cloud_volume" },
                { "stream", "core:stream" },
                { "spawner", "core:spawner" },
                { "movie", "core:movie" },
                { "crow", "core:crow" },
            };
            for (const auto& row : rows)
            {
                rememberAlias(row[0], row[1]);
                rememberAlias(row[1], row[1]);
            }
        }

        bool engineAccepted(const std::string& spec, const std::string& packageId)
        {
            if (spec.empty())
                return true;
            if (spec.rfind(">=", 0) != 0)
            {
                issue(packageId, "package '" + packageId + "' has an unrecognized engine constraint '" + spec + "'");
                return true;
            }
            const double major = std::atof(spec.c_str() + 2);
            if (major > 6.0)
            {
                issue(packageId, "package '" + packageId + "' needs engine " + spec);
                return false;
            }
            return true;
        }

        bool parsePackageFile(const std::filesystem::path& root, ContentPackage& out)
        {
            const std::filesystem::path file = root / "package.json";
            std::string                 text;
            if (!readFile(file, text))
            {
                out.id      = root.filename().string();
                out.version = "0";
                out.root    = root;
                return !out.id.empty();
            }
            watch(file);
            const json rootJson = json::parse(text, nullptr, false);
            if (rootJson.is_discarded() || !rootJson.is_object())
                return false;
            out.id      = jsonString(rootJson, "id");
            out.version = jsonString(rootJson, "version");
            out.engine  = jsonString(rootJson, "engine");
            out.root    = root;
            if (out.id.empty())
                out.id = root.filename().string();
            if (rootJson.contains("depends") && rootJson["depends"].is_array())
            {
                for (const json& item : rootJson["depends"])
                {
                    if (item.is_string())
                        out.depends.push_back(item.get<std::string>());
                }
            }
            if (rootJson.contains("loadAfter") && rootJson["loadAfter"].is_array())
            {
                for (const json& item : rootJson["loadAfter"])
                {
                    if (item.is_string())
                        out.loadAfter.push_back(item.get<std::string>());
                }
            }
            return !out.id.empty();
        }

        void readStringList(const json& object, const char* key, std::vector<std::string>& out)
        {
            const auto it = object.find(key);
            if (it == object.end() || !it->is_array())
                return;
            for (const json& item : *it)
            {
                if (item.is_string())
                    out.push_back(item.get<std::string>());
            }
        }

        bool parseArchetypeText(const std::string& text, const std::string& packageId, Archetype& out, ContentError& err)
        {
            const json root = json::parse(text, nullptr, false);
            if (root.is_discarded() || !root.is_object())
            {
                err.message = "archetype is not a JSON object";
                return false;
            }
            out.packageId = packageId;
            out.name      = canonicalContentId(jsonString(root, "id"));
            if (out.name.empty())
            {
                err.message = "archetype is missing id";
                return false;
            }
            out.baseName = canonicalContentId(jsonString(root, "base"));
            if (jsonString(root, "base").empty())
                out.baseName.clear();
            const auto ver = root.find("version");
            if (ver != root.end() && ver->is_number())
                out.version = ver->get<int>();
            if (root.contains("editor") && root["editor"].is_object())
            {
                const json& editor = root["editor"];
                out.editor.label      = jsonString(editor, "label");
                out.editor.category   = jsonString(editor, "category");
                out.editor.icon       = jsonString(editor, "icon");
                out.editor.hotkey     = jsonString(editor, "hotkey");
                out.editor.matchModel = jsonString(editor, "matchModel");
                readStringList(editor, "modes", out.editor.modes);
                const auto scale = editor.find("defaultScale");
                if (scale != editor.end() && scale->is_array() && scale->size() >= 3)
                {
                    int n = 0;
                    for (const json& item : *scale)
                    {
                        if (n >= 3)
                            break;
                        if (item.is_number())
                            out.editor.defaultScale[n++] = item.get<float>();
                    }
                    out.editor.hasDefaultScale = n == 3;
                }
                const auto color = editor.find("defaultColor");
                if (color != editor.end() && color->is_array() && color->size() >= 3)
                {
                    int n = 0;
                    for (const json& item : *color)
                    {
                        if (n >= 4)
                            break;
                        if (item.is_number())
                            out.editor.defaultColor[n++] = item.get<float>();
                    }
                    out.editor.hasDefaultColor = n >= 3;
                }
            }
            if (out.editor.label.empty())
            {
                const auto colon = out.name.rfind(':');
                out.editor.label = colon == std::string::npos ? out.name : out.name.substr(colon + 1);
            }
            if (root.contains("components") && root["components"].is_object())
            {
                for (auto it = root["components"].begin(); it != root["components"].end(); ++it)
                {
                    ArchetypeComponent comp;
                    comp.name = it.key();
                    if (!findComponent(comp.name))
                    {
                        err.message = "archetype '" + out.name + "' uses unknown component '" + comp.name + "'";
                        return false;
                    }
                    comp.paramsJson = it.value().is_null() ? std::string("{}") : it.value().dump();
                    out.components.push_back(std::move(comp));
                }
            }
            if (root.contains("behaviors") && root["behaviors"].is_array())
            {
                for (const json& item : root["behaviors"])
                {
                    ArchetypeBehavior behavior;
                    if (item.is_string())
                        behavior.name = item.get<std::string>();
                    else if (item.is_object())
                    {
                        behavior.name = jsonString(item, "name");
                        if (item.contains("params"))
                            behavior.paramsJson = item["params"].dump();
                    }
                    if (behavior.name.empty())
                        continue;
                    if (!findBehavior(behavior.name))
                    {
                        err.message = "archetype '" + out.name + "' uses unknown behavior '" + behavior.name + "'";
                        return false;
                    }
                    out.behaviors.push_back(std::move(behavior));
                }
            }
            if (root.contains("net") && root["net"].is_object())
            {
                const auto rep = root["net"].find("replicate");
                if (rep != root["net"].end() && rep->is_boolean())
                    out.replicate = rep->get<bool>();
                out.netPrefab = jsonString(root["net"], "prefab");
            }
            return true;
        }

        void adopt(Archetype archetype, std::unordered_map<uint64_t, std::string>& taken)
        {
            ContentError err;
            archetype.id = hashContentId(archetype.name);
            if (!claimContentHash(taken, archetype.id.hash, archetype.name, err))
            {
                issue(archetype.packageId, err.message);
                archetype.enabled = false;
                return;
            }
            const auto existing = gDb.byName.find(archetype.name);
            if (existing != gDb.byName.end())
            {
                issue(archetype.packageId, "package '" + archetype.packageId + "' overrides '" + archetype.name + "'");
                gDb.archetypes[existing->second] = std::move(archetype);
                gDb.byHash[gDb.archetypes[existing->second].id.hash] = existing->second;
                return;
            }
            gDb.byName.emplace(archetype.name, gDb.archetypes.size());
            gDb.byHash.emplace(archetype.id.hash, gDb.archetypes.size());
            gDb.archetypes.push_back(std::move(archetype));
        }

        void loadAliases(const std::filesystem::path& root, const std::string& packageId)
        {
            const std::filesystem::path file = root / "archetypes" / "legacy_aliases.json";
            std::string                 text;
            if (!readFile(file, text))
                return;
            watch(file);
            const json rootJson = json::parse(text, nullptr, false);
            if (rootJson.is_discarded() || !rootJson.is_object() || !rootJson.contains("aliases") || !rootJson["aliases"].is_object())
            {
                issue(packageId, "legacy_aliases.json in '" + packageId + "' is not an aliases object");
                return;
            }
            for (auto it = rootJson["aliases"].begin(); it != rootJson["aliases"].end(); ++it)
            {
                if (!it.value().is_string())
                    continue;
                rememberAlias(it.key(), canonicalContentId(it.value().get<std::string>()));
            }
        }

        void loadArchetypeDir(const ContentPackage& package, std::unordered_map<uint64_t, std::string>& taken)
        {
            const std::filesystem::path dir = package.root / "archetypes";
            std::error_code             ec;
            if (!std::filesystem::is_directory(dir, ec) || ec)
                return;
            for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(dir, ec))
            {
                if (ec)
                    break;
                if (!entry.is_regular_file())
                    continue;
                const std::string name = entry.path().filename().string();
                if (name == "legacy_aliases.json" || !name.ends_with(".archetype.json"))
                    continue;
                watch(entry.path());
                std::string text;
                if (!readFile(entry.path(), text))
                {
                    issue(package.id, "could not read " + name);
                    continue;
                }
                Archetype    archetype;
                ContentError err;
                if (!parseArchetypeText(text, package.id, archetype, err))
                {
                    issue(package.id, err.message.empty() ? name + " failed to parse" : err.message);
                    continue;
                }
                adopt(std::move(archetype), taken);
            }
        }

        void synthesizeEntityMasters(const ContentPackage& package, std::unordered_map<uint64_t, std::string>& taken)
        {
            const std::filesystem::path dir = package.root / "entities";
            std::error_code             ec;
            if (!std::filesystem::is_directory(dir, ec) || ec)
                return;
            for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(dir, ec))
            {
                if (ec)
                    break;
                if (!entry.is_regular_file())
                    continue;
                const std::string fileName = entry.path().filename().string();
                if (!fileName.ends_with(".entity.json"))
                    continue;
                std::string text;
                if (!readFile(entry.path(), text))
                    continue;
                EntityMaster master;
                if (!parseEntityMaster(text, master) || master.type.empty())
                    continue;
                const std::string id = canonicalContentId(master.type);
                if (gDb.byName.contains(id))
                    continue;
                Archetype archetype;
                archetype.packageId = package.id;
                archetype.name      = id;
                archetype.version   = master.version;
                archetype.editor.label = master.type;
                archetype.editor.category = "Creatures";
                archetype.editor.icon = "bug";
                archetype.editor.modes.push_back("3d");
                archetype.editor.matchModel = master.type;
                if (!master.gltf.empty() || !master.anim.empty())
                {
                    json model = json::object();
                    if (!master.gltf.empty())
                        model["gltf"] = master.gltf;
                    if (!master.anim.empty())
                        model["anim"] = master.anim;
                    archetype.components.push_back(ArchetypeComponent{ "Model", model.dump() });
                }
                if (!master.physics.empty())
                {
                    json physics = json::object();
                    physics["file"] = master.physics;
                    archetype.components.push_back(ArchetypeComponent{ "Physics", physics.dump() });
                }
                archetype.behaviors.push_back(ArchetypeBehavior{ "ai.animal", "{}" });
                adopt(std::move(archetype), taken);
            }
        }

        void mergeComponents(std::vector<ArchetypeComponent>& dst, std::vector<ArchetypeComponent> src)
        {
            for (ArchetypeComponent& comp : src)
            {
                bool replaced = false;
                for (ArchetypeComponent& existing : dst)
                {
                    if (existing.name == comp.name)
                    {
                        existing  = std::move(comp);
                        replaced  = true;
                        break;
                    }
                }
                if (!replaced)
                    dst.push_back(std::move(comp));
            }
        }

        void mergeBehaviors(std::vector<ArchetypeBehavior>& dst, std::vector<ArchetypeBehavior> src)
        {
            for (ArchetypeBehavior& behavior : src)
            {
                bool replaced = false;
                for (ArchetypeBehavior& existing : dst)
                {
                    if (existing.name == behavior.name)
                    {
                        existing = std::move(behavior);
                        replaced = true;
                        break;
                    }
                }
                if (!replaced)
                    dst.push_back(std::move(behavior));
            }
        }

        // Flatten base first so a child sees the parent's already-merged components.
        void resolveBases()
        {
            bool progress = true;
            int  guard    = 0;
            while (progress && guard < 64)
            {
                progress = false;
                ++guard;
                for (Archetype& archetype : gDb.archetypes)
                {
                    if (!archetype.enabled || archetype.baseName.empty())
                        continue;
                    const auto it = gDb.byName.find(archetype.baseName);
                    if (it == gDb.byName.end())
                    {
                        issue(archetype.packageId, "archetype '" + archetype.name + "' is missing base '" + archetype.baseName + "'");
                        archetype.enabled  = false;
                        archetype.baseName.clear();
                        progress = true;
                        continue;
                    }
                    Archetype& base = gDb.archetypes[it->second];
                    if (!base.enabled)
                    {
                        issue(archetype.packageId, "archetype '" + archetype.name + "' bases disabled '" + base.name + "'");
                        archetype.enabled  = false;
                        archetype.baseName.clear();
                        progress = true;
                        continue;
                    }
                    if (!base.baseName.empty())
                        continue;
                    std::vector<ArchetypeComponent> merged = base.components;
                    mergeComponents(merged, std::move(archetype.components));
                    archetype.components = std::move(merged);
                    std::vector<ArchetypeBehavior> behaviors = base.behaviors;
                    mergeBehaviors(behaviors, std::move(archetype.behaviors));
                    archetype.behaviors = std::move(behaviors);
                    if (archetype.editor.icon.empty())
                        archetype.editor.icon = base.editor.icon;
                    if (archetype.editor.hotkey.empty())
                        archetype.editor.hotkey = base.editor.hotkey;
                    if (archetype.editor.category.empty())
                        archetype.editor.category = base.editor.category;
                    if (archetype.editor.matchModel.empty())
                        archetype.editor.matchModel = base.editor.matchModel;
                    if (archetype.editor.modes.empty())
                        archetype.editor.modes = base.editor.modes;
                    if (!archetype.editor.hasDefaultScale && base.editor.hasDefaultScale)
                    {
                        archetype.editor.hasDefaultScale = true;
                        for (int i = 0; i < 3; ++i)
                            archetype.editor.defaultScale[i] = base.editor.defaultScale[i];
                    }
                    if (!archetype.editor.hasDefaultColor && base.editor.hasDefaultColor)
                    {
                        archetype.editor.hasDefaultColor = true;
                        for (int i = 0; i < 4; ++i)
                            archetype.editor.defaultColor[i] = base.editor.defaultColor[i];
                    }
                    if (archetype.netPrefab.empty())
                        archetype.netPrefab = base.netPrefab;
                    archetype.baseName.clear();
                    progress = true;
                }
            }
            for (Archetype& archetype : gDb.archetypes)
            {
                if (archetype.enabled && !archetype.baseName.empty())
                {
                    issue(archetype.packageId, "archetype '" + archetype.name + "' has a cyclic base");
                    archetype.enabled = false;
                }
            }
        }

        void rebuildIndexes()
        {
            gDb.byName.clear();
            gDb.byHash.clear();
            gDb.replication.clear();
            std::unordered_map<uint64_t, std::string> taken;
            std::vector<Archetype> kept;
            kept.reserve(gDb.archetypes.size());
            for (Archetype& archetype : gDb.archetypes)
            {
                ContentError err;
                archetype.id = hashContentId(archetype.name);
                if (!claimContentHash(taken, archetype.id.hash, archetype.name, err))
                {
                    issue(archetype.packageId, err.message);
                    archetype.enabled = false;
                }
                gDb.byName.emplace(archetype.name, kept.size());
                gDb.byHash.emplace(archetype.id.hash, kept.size());
                kept.push_back(std::move(archetype));
            }
            gDb.archetypes = std::move(kept);
            std::vector<std::string> replicated;
            for (const Archetype& archetype : gDb.archetypes)
            {
                if (archetype.enabled && archetype.replicate)
                    replicated.push_back(archetype.name);
            }
            std::sort(replicated.begin(), replicated.end());
            gDb.replication = std::move(replicated);

            std::string blob;
            std::vector<const Archetype*> ordered;
            ordered.reserve(gDb.archetypes.size());
            for (const Archetype& archetype : gDb.archetypes)
                ordered.push_back(&archetype);
            std::sort(ordered.begin(), ordered.end(), [](const Archetype* a, const Archetype* b) { return a->name < b->name; });
            for (const Archetype* archetype : ordered)
            {
                if (!archetype->enabled)
                    continue;
                blob += archetype->name;
                blob += '\n';
                blob += std::to_string(archetype->version);
                blob += '\n';
                for (const ArchetypeComponent& comp : archetype->components)
                {
                    blob += comp.name;
                    blob += comp.paramsJson;
                    blob += '\n';
                }
                for (const ArchetypeBehavior& behavior : archetype->behaviors)
                {
                    blob += behavior.name;
                    blob += behavior.paramsJson;
                    blob += '\n';
                }
                blob += archetype->replicate ? '1' : '0';
                blob += archetype->netPrefab;
                blob += '\n';
            }
            gDb.setHash = Save::fnv1a64(blob.data(), blob.size());
        }

        void addModFolders(std::vector<ContentPackage>& out, const std::filesystem::path& modsDir)
        {
            std::error_code ec;
            if (modsDir.empty() || !std::filesystem::is_directory(modsDir, ec) || ec)
                return;
            for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(modsDir, ec))
            {
                if (ec || !entry.is_directory())
                    continue;
                ContentPackage package;
                if (!parsePackageFile(entry.path(), package))
                    continue;
                if (!engineAccepted(package.engine, package.id))
                    continue;
                out.push_back(std::move(package));
            }
        }

        std::filesystem::path localAppData()
        {
            char*  env = nullptr;
            size_t len = 0;
            if (_dupenv_s(&env, &len, "LOCALAPPDATA") != 0 || !env || env[0] == '\0')
            {
                free(env);
                return {};
            }
            std::filesystem::path path(env);
            free(env);
            return path;
        }

        std::vector<std::string> enabledFromModsJson()
        {
            std::vector<std::filesystem::path> candidates;
            const std::filesystem::path        exe = executableDirectory();
            std::error_code                    ec;
            const std::filesystem::path        cwd = std::filesystem::current_path(ec);
            candidates.push_back(exe / "mods.json");
            if (!cwd.empty())
                candidates.push_back(cwd / "mods.json");
            const std::filesystem::path local = localAppData();
            if (!local.empty())
                candidates.push_back(local / "DarkEngine6" / "mods.json");
            for (const std::filesystem::path& path : candidates)
            {
                std::string text;
                if (!readFile(path, text))
                    continue;
                watch(path);
                const json root = json::parse(text, nullptr, false);
                if (root.is_discarded() || !root.is_object() || !root.contains("enable") || !root["enable"].is_array())
                    continue;
                std::vector<std::string> ids;
                for (const json& item : root["enable"])
                {
                    if (item.is_string())
                        ids.push_back(item.get<std::string>());
                }
                return ids;
            }
            return {};
        }

        std::vector<ContentPackage> orderPackages(std::vector<ContentPackage> packages)
        {
            std::unordered_map<std::string, ContentPackage> byId;
            for (ContentPackage& package : packages)
            {
                if (byId.contains(package.id))
                    issue(package.id, "duplicate package id '" + package.id + "' from " + package.root.string());
                byId[package.id] = std::move(package);
            }
            std::unordered_map<std::string, int> indegree;
            std::unordered_map<std::string, std::vector<std::string>> after;
            for (const auto& [id, package] : byId)
            {
                indegree[id] = 0;
                (void)package;
            }
            for (const auto& [id, package] : byId)
            {
                std::vector<std::string> edges = package.depends;
                edges.insert(edges.end(), package.loadAfter.begin(), package.loadAfter.end());
                for (const std::string& need : edges)
                {
                    if (!byId.contains(need))
                    {
                        issue(id, "package '" + id + "' depends on missing '" + need + "'");
                        continue;
                    }
                    after[need].push_back(id);
                    indegree[id] += 1;
                }
            }
            std::queue<std::string> ready;
            for (const auto& [id, degree] : indegree)
            {
                if (degree == 0)
                    ready.push(id);
            }
            std::vector<ContentPackage> ordered;
            while (!ready.empty())
            {
                const std::string id = ready.front();
                ready.pop();
                ordered.push_back(byId[id]);
                for (const std::string& next : after[id])
                {
                    indegree[next] -= 1;
                    if (indegree[next] == 0)
                        ready.push(next);
                }
            }
            if (ordered.size() != byId.size())
            {
                for (const auto& [id, degree] : indegree)
                {
                    if (degree > 0)
                        issue(id, "package '" + id + "' is in a dependency cycle and was not loaded");
                }
            }
            return ordered;
        }

        void discover(std::vector<ContentPackage>& out)
        {
            ContentPackage core;
            std::filesystem::path coreRoot = authoringContentRoot();
            std::error_code       ec;
            if (coreRoot.empty() || !std::filesystem::is_directory(coreRoot, ec) || ec)
            {
                for (const std::filesystem::path& candidate : contentRootCandidates())
                {
                    if (std::filesystem::is_directory(candidate, ec) && !ec)
                    {
                        coreRoot = candidate;
                        break;
                    }
                }
            }
            if (!coreRoot.empty())
            {
                if (!parsePackageFile(coreRoot, core) || core.id.empty())
                {
                    core.id   = "core";
                    core.root = coreRoot;
                }
                if (core.id != "core")
                    core.id = "core";
                core.root = coreRoot;
                out.push_back(core);
            }

            const std::filesystem::path exe = executableDirectory();
            std::error_code             cwdEc;
            const std::filesystem::path cwd = std::filesystem::current_path(cwdEc);
            addModFolders(out, exe / "mods");
            if (!cwd.empty())
                addModFolders(out, cwd / "mods");
            addModFolders(out, exe / ".." / ".." / ".." / "mods");
            addModFolders(out, exe / ".." / ".." / "mods");
            const std::filesystem::path local = localAppData();
            if (!local.empty())
                addModFolders(out, local / "DarkEngine6" / "mods");
        }

        void applyEnableList(std::vector<ContentPackage>& packages)
        {
            const std::vector<std::string> enabled = enabledFromModsJson();
            if (enabled.empty())
                return;
            for (ContentPackage& package : packages)
            {
                if (package.id == "core")
                    continue;
                const bool on = std::find(enabled.begin(), enabled.end(), package.id) != enabled.end();
                package.enabled = on;
            }
            for (const std::string& id : enabled)
            {
                bool found = false;
                for (const ContentPackage& package : packages)
                {
                    if (package.id == id)
                        found = true;
                }
                if (!found)
                    issue(id, "mods.json enables missing package '" + id + "'");
            }
        }

        void watchCatalogFiles(const std::filesystem::path& root)
        {
            watch(root / "combat" / "statuses.json");
            watch(root / "player" / "loadout.json");
            watch(root / "upgrades.json");
            std::error_code ec;
            const std::filesystem::path items = root / "items";
            if (std::filesystem::is_directory(items, ec) && !ec)
            {
                for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(items, ec))
                {
                    if (ec || !entry.is_regular_file())
                        continue;
                    if (entry.path().filename().string().ends_with(".items.json"))
                        watch(entry.path());
                }
            }
            ec.clear();
            const std::filesystem::path entities = root / "entities";
            if (std::filesystem::is_directory(entities, ec) && !ec)
            {
                for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(entities, ec))
                {
                    if (ec || !entry.is_regular_file())
                        continue;
                    if (entry.path().filename().string().ends_with(".entity.json"))
                        watch(entry.path());
                }
            }
        }

        void fillOverlays()
        {
            gDb.overlays.clear();
            for (auto it = gDb.packages.rbegin(); it != gDb.packages.rend(); ++it)
            {
                if (!it->enabled)
                    continue;
                if (it->id == "core")
                    continue;
                gDb.overlays.push_back(it->root);
            }
        }

        bool buildFromPackages(std::vector<ContentPackage> packages, ContentError* err)
        {
            if (!gDb.builtins)
            {
                registerBuiltinContent();
                gDb.builtins = true;
            }
            gDb.packages.clear();
            gDb.archetypes.clear();
            gDb.issues.clear();
            gDb.byName.clear();
            gDb.byHash.clear();
            gDb.aliases.clear();
            gDb.watched.clear();
            gDb.mtimes.clear();
            gDb.replication.clear();
            gDb.overlays.clear();
            gDb.setHash = 0;
            seedLegacyAliases();
            packages = orderPackages(std::move(packages));
            applyEnableList(packages);
            std::unordered_map<uint64_t, std::string> taken;
            for (ContentPackage& package : packages)
            {
                if (!package.enabled)
                {
                    gDb.packages.push_back(std::move(package));
                    continue;
                }
                loadAliases(package.root, package.id);
                loadArchetypeDir(package, taken);
                synthesizeEntityMasters(package, taken);
                gDb.packages.push_back(std::move(package));
            }
            resolveBases();
            rebuildIndexes();
            fillOverlays();
            for (const ContentPackage& package : gDb.packages)
            {
                if (package.enabled)
                    watchCatalogFiles(package.root);
            }
            gDb.loaded = true;
            DE_LOG_INFO(LogCategory::Core, "Content: {} package(s), {} archetype(s)", gDb.packages.size(), gDb.archetypes.size());
            if (err && !gDb.issues.empty())
                err->message = gDb.issues.front().message;
            return true;
        }

        const std::string* aliasTarget(std::string_view text)
        {
            const auto it = gDb.aliases.find(std::string(text));
            if (it == gDb.aliases.end())
                return nullptr;
            return &it->second;
        }
    } // namespace

    bool registerComponent(const ComponentInfo& info)
    {
        if (info.name.empty())
            return false;
        for (ComponentInfo& existing : gComponents)
        {
            if (existing.name == info.name)
            {
                existing = info;
                return true;
            }
        }
        gComponents.push_back(info);
        return true;
    }

    bool registerBehavior(const BehaviorInfo& info)
    {
        if (info.name.empty() || !info.attach)
            return false;
        for (BehaviorInfo& existing : gBehaviors)
        {
            if (existing.name == info.name)
            {
                existing = info;
                return true;
            }
        }
        gBehaviors.push_back(info);
        return true;
    }

    const ComponentInfo* findComponent(std::string_view name)
    {
        for (const ComponentInfo& info : gComponents)
        {
            if (info.name == name)
                return &info;
        }
        return nullptr;
    }

    const BehaviorInfo* findBehavior(std::string_view name)
    {
        for (const BehaviorInfo& info : gBehaviors)
        {
            if (info.name == name)
                return &info;
        }
        return nullptr;
    }

    bool loadContentDatabase(ContentError* err)
    {
        std::vector<ContentPackage> found;
        discover(found);
        return buildFromPackages(std::move(found), err);
    }

    bool reloadContentDatabase(ContentError* err)
    {
        clearEntityMasterCache();
        return loadContentDatabase(err);
    }

    bool pollContentChanges()
    {
        if (!gDb.loaded)
            return loadContentDatabase(nullptr);
        bool changed = gDb.watched.empty();
        for (const std::filesystem::path& path : gDb.watched)
        {
            std::error_code ec;
            const auto      when = std::filesystem::last_write_time(path, ec);
            if (ec)
            {
                changed = true;
                break;
            }
            const auto it = gDb.mtimes.find(path.generic_string());
            if (it == gDb.mtimes.end() || it->second != when)
            {
                changed = true;
                break;
            }
        }
        if (!changed)
            return false;
        return reloadContentDatabase(nullptr);
    }

    void resetContentDatabase()
    {
        gDb = Database{};
        clearEntityMasterCache();
    }

    bool loadContentPackagesForTest(const std::vector<std::filesystem::path>& rootsInOrder, ContentError* err)
    {
        std::vector<ContentPackage> packages;
        for (const std::filesystem::path& root : rootsInOrder)
        {
            ContentPackage package;
            if (!parsePackageFile(root, package))
            {
                package.id   = root.filename().string();
                package.root = root;
            }
            package.root = root;
            if (package.id.empty())
                package.id = root.filename().string();
            packages.push_back(std::move(package));
        }
        gDb = Database{};
        gDb.builtins = !gComponents.empty();
        if (!gDb.builtins)
        {
            registerBuiltinContent();
            gDb.builtins = true;
        }
        seedLegacyAliases();
        std::unordered_map<uint64_t, std::string> taken;
        for (ContentPackage& package : packages)
        {
            loadAliases(package.root, package.id);
            loadArchetypeDir(package, taken);
            synthesizeEntityMasters(package, taken);
            gDb.packages.push_back(package);
        }
        resolveBases();
        rebuildIndexes();
        fillOverlays();
        gDb.loaded = true;
        if (err && !gDb.issues.empty())
            err->message = gDb.issues.front().message;
        return true;
    }

    const Archetype* findArchetype(std::string_view idOrShort)
    {
        if (idOrShort.empty() || !gDb.loaded)
            return nullptr;
        const std::string canonical = canonicalContentId(idOrShort);
        auto              it        = gDb.byName.find(canonical);
        if (it == gDb.byName.end())
        {
            if (const std::string* aliased = aliasTarget(idOrShort))
                it = gDb.byName.find(*aliased);
        }
        if (it == gDb.byName.end())
            return nullptr;
        const Archetype& archetype = gDb.archetypes[it->second];
        if (!archetype.enabled)
            return nullptr;
        return &archetype;
    }

    const Archetype* findArchetype(ContentId id)
    {
        const auto it = gDb.byHash.find(id.hash);
        if (it == gDb.byHash.end())
            return nullptr;
        const Archetype& archetype = gDb.archetypes[it->second];
        if (!archetype.enabled)
            return nullptr;
        return &archetype;
    }

    const std::vector<Archetype>& archetypes()
    {
        return gDb.archetypes;
    }

    const std::vector<ContentPackage>& contentPackages()
    {
        return gDb.packages;
    }

    const std::vector<ContentIssue>& contentIssues()
    {
        return gDb.issues;
    }

    bool contentHasArchetype(std::string_view idOrShort)
    {
        return findArchetype(idOrShort) != nullptr;
    }

    const Archetype* archetypeMatchingModelStem(std::string_view stem)
    {
        if (stem.empty())
            return nullptr;
        for (const Archetype& archetype : gDb.archetypes)
        {
            if (!archetype.enabled)
                continue;
            const std::string& model = archetype.editor.matchModel;
            if (model.size() == stem.size())
            {
                bool same = true;
                for (size_t i = 0; i < stem.size(); ++i)
                {
                    const unsigned char a = static_cast<unsigned char>(model[i]);
                    const unsigned char b = static_cast<unsigned char>(stem[i]);
                    if (std::tolower(a) != std::tolower(b))
                    {
                        same = false;
                        break;
                    }
                }
                if (same)
                    return &archetype;
            }
        }
        return nullptr;
    }

    uint64_t contentSetHash()
    {
        return gDb.setHash;
    }

    uint16_t replicationIndex(std::string_view archetypeName)
    {
        const std::string canonical = canonicalContentId(archetypeName);
        for (size_t i = 0; i < gDb.replication.size(); ++i)
        {
            if (gDb.replication[i] == canonical)
            {
                const size_t index = i + 1;
                if (index > 0xffffu)
                    return 0;
                return static_cast<uint16_t>(index);
            }
        }
        return 0;
    }

    const std::string* replicationName(uint16_t index)
    {
        if (index == 0 || index > gDb.replication.size())
            return nullptr;
        return &gDb.replication[static_cast<size_t>(index - 1)];
    }

    std::vector<std::filesystem::path> contentOverlayRoots()
    {
        return gDb.overlays;
    }

} // namespace Dark
