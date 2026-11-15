#pragma once

#include "Content/ContentId.h"
#include "ECS/Entity.h"
#include "Weapons/Weapon.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Dark
{

    class World;
    class AssetManager;
    class AssetPinTable;
    class Renderer;
    class AiSystem;
    struct CrowModels;

    namespace Audio
    {
        class AudioSystem;
    }
    namespace Terrain
    {
        class TerrainGround;
        class TerrainGrid;
    }

    // Services a behavior may use. Null services mean that behavior cannot run.
    struct ArchetypeSpawnHost
    {
        World*                        world    = nullptr;
        AssetManager*                 assets   = nullptr;
        AssetPinTable*                pins     = nullptr;
        Renderer*                     renderer = nullptr;
        Audio::AudioSystem*           audio    = nullptr;
        const Terrain::TerrainGround* ground   = nullptr;
        AiSystem*                     ai       = nullptr;
        bool (*ensureMesh)(void* user, Entity entity) = nullptr;
        // primitive is "cube", "sphere", "cross", or "none".
        bool (*ensurePrimitive)(void* user, Entity entity, const char* primitive) = nullptr;
        bool (*ensureMarker)(void* user, Entity entity) = nullptr;
        bool (*bindParticleMaterial)(void* user, Entity entity, bool ribbon) = nullptr;
        WeaponHitFn onWeaponHit = nullptr;
        void*        weaponHitUser = nullptr;
        // Scene models in Sandbox refuse a glTF that has no skeleton.
        bool modelAnimatedOnly = false;
        void* user = nullptr;
        // ai.crow reads these. A null flock means that behavior cannot attach.
        CrowModels*                 crows       = nullptr;
        const Terrain::TerrainGrid* crowTerrain = nullptr;
        bool                        crowHomeSet = false;
        float                       crowHomeX   = 0.0f;
        float                       crowHomeZ   = 0.0f;
        uint32_t                    crowSeed    = 1;
        int                         crowSlot    = 0;
    };

    struct EditorMeta
    {
        std::string              label;
        std::string              category;
        std::string              icon;
        std::string              hotkey;
        std::vector<std::string> modes;
        std::string              matchModel;
        std::string              tooltip;
        bool                     hasDefaultScale = false;
        float                    defaultScale[3]{ 1.0f, 1.0f, 1.0f };
        bool                     hasDefaultColor = false;
        float                    defaultColor[4]{ 1.0f, 1.0f, 1.0f, 1.0f };
    };

    struct ArchetypeComponent
    {
        std::string name;
        std::string paramsJson;
    };

    struct ArchetypeBehavior
    {
        std::string name;
        std::string paramsJson;
    };

    struct Archetype
    {
        ContentId                         id;
        std::string                       name;
        std::string                       baseName;
        int                               version = 1;
        std::vector<ArchetypeComponent>   components;
        std::vector<ArchetypeBehavior>    behaviors;
        EditorMeta                        editor;
        bool                              replicate = false;
        // cube, sphere, platform, or coin. Empty means this archetype is not a NetPrefab prop.
        std::string                       netPrefab;
        bool                              enabled   = true;
        std::string                       packageId;
    };

    // Empty modes means the archetype is offered in every scene mode.
    inline bool archetypeVisibleInMode(const Archetype& archetype, std::string_view modeName)
    {
        if (!archetype.enabled)
            return false;
        if (archetype.editor.modes.empty())
            return true;
        for (const std::string& mode : archetype.editor.modes)
        {
            if (mode == modeName)
                return true;
        }
        return false;
    }

    struct ContentPackage
    {
        std::string                      id;
        std::string                      version;
        std::string                      engine;
        std::filesystem::path            root;
        std::vector<std::string>         depends;
        std::vector<std::string>         loadAfter;
        bool                             enabled = true;
    };

    struct ContentIssue
    {
        std::string packageId;
        std::string message;
    };

    struct ComponentInfo
    {
        std::string_view name;
        uint16_t         version = 1;
        bool (*read)(World& world, Entity entity, std::string_view jsonText, ContentError& err) = nullptr;
        bool (*has)(const World& world, Entity entity)                                          = nullptr;
    };

    struct BehaviorInfo
    {
        std::string_view name;
        bool (*attach)(const ArchetypeSpawnHost& host, Entity entity, std::string_view paramsJson, ContentError& err) = nullptr;
    };

    bool                registerComponent(const ComponentInfo& info);
    bool                registerBehavior(const BehaviorInfo& info);
    const ComponentInfo* findComponent(std::string_view name);
    const BehaviorInfo* findBehavior(std::string_view name);
    void                registerBuiltinContent();

    // Discovers content/ plus mods/, builds the archetype table, and records overlay roots.
    bool loadContentDatabase(ContentError* err = nullptr);
    bool reloadContentDatabase(ContentError* err = nullptr);
    // True when a watched file changed and the database was rebuilt.
    bool pollContentChanges();
    void resetContentDatabase();

    // Test helper. Each directory is one package, already in load order. Later roots override.
    bool loadContentPackagesForTest(const std::vector<std::filesystem::path>& rootsInOrder, ContentError* err);

    const Archetype*              findArchetype(std::string_view idOrShort);
    const Archetype*              findArchetype(ContentId id);
    const std::vector<Archetype>& archetypes();
    const std::vector<ContentPackage>& contentPackages();
    const std::vector<ContentIssue>& contentIssues();

    bool        contentHasArchetype(std::string_view idOrShort);
    // False until loadContentDatabase succeeds. Distinct from a missing id.
    bool        contentDatabaseLoaded();
    const Archetype* archetypeMatchingModelStem(std::string_view stem);

    uint64_t           contentSetHash();
    uint16_t           replicationIndex(std::string_view archetypeName);
    const std::string* replicationName(uint16_t index);

    // Highest priority first. Empty until the database has been loaded.
    std::vector<std::filesystem::path> contentOverlayRoots();

} // namespace Dark
