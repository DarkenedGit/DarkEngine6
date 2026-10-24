#pragma once

#include "ECS/Entity.h"
#include "Weapons/Weapon.h"

#include <string>
#include <string_view>

namespace Dark
{

    class AssetManager;
    class AssetPinTable;
    class Model;
    class Renderer;
    class ThrowableFlights;
    class World;

    inline constexpr const char* kThrowableHandJoint = "R_Hand";
    inline constexpr const char* kThrowableUnitCube  = "models/unit_cube.gltf";

    // Pure hand decision shared by sandbox and editor play. Shown only for WeaponKind::Throwable
    // with a catalog model that is not the unit cube. The joint is always R_Hand.
    struct ThrowableHandChoice
    {
        bool        shown     = false;
        const char* jointName = kThrowableHandJoint;
        std::string modelPath;
    };

    // attachMesh is false for an empty path, the unit cube, or a model that is not on disk.
    // allowThrow stays true: a missing mesh does not fail the throw.
    struct ThrowableMeshChoice
    {
        bool attachMesh = false;
        bool allowThrow = true;
    };

    struct ThrowableHandComponent
    {
        static constexpr const char* kTypeName = "ThrowableHand";

        Entity      mesh{};
        int         joint = -1;
        std::string modelPath;
        float       scale = 1.0f;
        bool        shown = false;
    };

    struct ThrowableFlightVisualComponent
    {
        static constexpr const char* kTypeName = "ThrowableFlightVisual";

        std::string itemId;
        std::string modelPath;
        float       scale = 1.0f;
    };

    ThrowableHandChoice throwableHandChoice(WeaponKind kind, std::string_view handId);
    // Catalog model for a throwable id. Empty when the id is unknown, the model field is empty,
    // or the authored model is the unit cube. Pickup and in-flight draw both use this.
    std::string throwableVisualModelPath(std::string_view itemId);
    ThrowableMeshChoice throwableMeshChoice(std::string_view modelPath);

    // False leaves the entity without a mesh and logs. The caller still keeps the entity.
    bool attachThrowableMesh(World& world, AssetPinTable& pins, AssetManager& assets, Renderer& renderer, Entity entity, std::string_view modelPath, bool inHand);

    void syncThrowableHands(World& world, AssetPinTable& pins, AssetManager& assets, Renderer& renderer);
    void syncThrowableFlightVisuals(World& world, AssetPinTable& pins, AssetManager& assets, Renderer& renderer, const ThrowableFlights& flights);
    void clearThrowablePlayVisuals(World& world, AssetPinTable& pins);

} // namespace Dark
