#pragma once

#include "Math/Vector2f.h"
#include "Math/Vector3f.h"
#include "ECS/Entity.h"
#include "Scene/SceneTypes.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Dark
{

    // One solid span along a wall edge, in metres: along the edge from a to b, from height y0 to y1.
    struct KitSpan
    {
        float a  = 0.0f;
        float b  = 0.0f;
        float y0 = 0.0f;
        float y1 = 0.0f;
    };

    struct KitPiece
    {
        std::vector<KitSpan> spans;
        bool                 hasLeaf = false;
        KitSpan              leaf;
        std::string          lock;
    };

    struct KitProp
    {
        Math::Vector3f size{ 1.0f, 1.0f, 1.0f };
        float          color[4]{ 0.5f, 0.5f, 0.5f, 1.0f };
        bool           hide = false;
        std::string    label;
    };

    // content/buildings/<kit>.kit.json: the parts every building of that style is made from.
    struct BuildingKit
    {
        float                                     cell      = 4.0f;
        float                                     height    = 3.2f;
        float                                     thickness = 0.3f;
        float                                     wallColor[4]{ 0.62f, 0.64f, 0.66f, 1.0f };
        float                                     floorColor[4]{ 0.32f, 0.33f, 0.35f, 1.0f };
        std::unordered_map<char, std::string>     symbols;
        std::unordered_map<std::string, KitPiece> pieces;
        std::unordered_map<std::string, KitProp>  props;
    };

    struct BuildingLoot
    {
        std::string item;
        int         count = 1;
    };

    struct BuildingPropDef
    {
        std::string               prop;
        Math::Vector2f            pos{};
        float                     yaw = 0.0f;
        std::vector<BuildingLoot> loot;
    };

    struct BuildingPickupDef
    {
        std::string    item;
        Math::Vector2f pos{};
        int            count = 1;
    };

    struct BuildingGuardDef
    {
        Math::Vector2f              pos{};
        std::vector<Math::Vector2f> patrol;
        std::string                 squad;
    };

    struct BuildingCameraDef
    {
        Math::Vector2f pos{};
        float          height = 2.8f;
        float          yaw    = 0.0f;
        float          sweep  = 60.0f;
        float          range  = 14.0f;
        std::string    circuit;
    };

    struct BuildingPowerDef
    {
        Math::Vector2f pos{};
        std::string    circuit;
    };

    // content/buildings/<id>.building.json. Layout is a top-down text plan, see the kit file for symbols.
    struct BuildingDef
    {
        std::string                    id;
        std::string                    kit = "facility";
        std::vector<std::string>       layout;
        std::vector<BuildingPropDef>   props;
        std::vector<BuildingPickupDef> pickups;
        std::vector<BuildingGuardDef>  guards;
        std::vector<BuildingCameraDef> cameras;
        std::vector<BuildingPowerDef>  powerBoxes;
    };

    // A camera placed in the world. yaw is world degrees: 0 looks along +z, 90 along +x.
    struct BuildingCamera
    {
        Math::Vector3f pos{};
        float          yaw   = 0.0f;
        float          sweep = 60.0f;
        float          range = 14.0f;
        std::string    circuit;
    };

    struct SecurityCameraComponent
    {
        static constexpr const char* kTypeName = "SecurityCamera";

        BuildingCamera def;
        float          phase = 0.0f;
        float          seen  = 0.0f;
        float          aimYaw = 0.0f;
        Entity         light{};
    };

    struct BuildingContainer
    {
        Math::Vector3f            pos{};
        std::string               label;
        std::vector<BuildingLoot> loot;
    };

    // A sliding door leaf. The doorway stays open for pathfinding; the app blocks it while the door is shut.
    struct BuildingDoor
    {
        Math::Vector3f center{};
        Math::Vector3f size{};
        std::string    lock;
    };

    struct DoorComponent
    {
        static constexpr const char* kTypeName = "Door";

        Math::Vector3f closedPos{};
        Math::Vector3f size{};
        std::string    lock;
        float          open     = 0.0f;
        bool           wantOpen = false;
    };

    struct BuildingHideSpot
    {
        Math::Vector3f pos{};
        Math::Vector3f front{};
        std::string    label;
    };

    // A spot the player can step into with G. front is the step-out offset from pos.
    struct HideSpotComponent
    {
        static constexpr const char* kTypeName = "HideSpot";

        Math::Vector3f front{};
        std::string    label;
    };

    struct BuildingExpansion
    {
        std::vector<SceneObjectData>   objects;
        std::vector<BuildingContainer> containers;
        std::vector<BuildingHideSpot>  hides;
        std::vector<BuildingDoor>      doors;
        std::vector<BuildingCamera>    cameras;
    };

    bool parseBuildingKit(std::string_view jsonText, BuildingKit& out, std::string* error = nullptr);
    bool parseBuilding(std::string_view jsonText, BuildingDef& out, std::string* error = nullptr);
    bool loadBuildingKit(const std::filesystem::path& path, BuildingKit& out, std::string* error = nullptr);
    bool loadBuilding(const std::filesystem::path& path, BuildingDef& out, std::string* error = nullptr);
    std::string buildingVirtualPath(std::string_view id);
    std::string buildingKitVirtualPath(std::string_view kit);

    // Turns one placed building into scene cubes, pickups and hunters plus the searchable and hide spots.
    // yaw is rounded to a quarter turn so every box stays axis aligned.
    bool expandBuilding(const BuildingDef& def, const BuildingKit& kit, const SceneBuildingDesc& place, BuildingExpansion& out, std::string* error = nullptr);

    // Loads every building the scene places (content/buildings/...), appends their cubes, pickups and hunters to
    // scene.objects, and returns the searchable containers and hide spots for the app to spawn. Both apps call this.
    int expandSceneBuildings(SceneFileData& scene, std::vector<BuildingContainer>& containers, std::vector<BuildingHideSpot>& hides,
                             std::vector<BuildingDoor>* doors = nullptr, std::vector<BuildingCamera>* cameras = nullptr);

} // namespace Dark
