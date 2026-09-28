#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace Dark
{
    // One record per placeable type. Paths are content-relative (models/human.gltf).
    // An empty string means that type has no file of that kind.
    struct EntityMaster
    {
        int         version = 1;
        std::string type;
        std::string gltf;
        std::string anim;
        std::string physics;
        std::string hsm;
    };

    // "player" -> "entities/player.entity.json"
    std::string entityMasterVirtualPath(std::string_view typeName);

    // nlohmann::json::parse(..., false). On failure `out` is unchanged.
    bool parseEntityMaster(std::string_view jsonText, EntityMaster& out);
    bool loadEntityMasterFile(const std::filesystem::path& path, EntityMaster& out);

    // First content root that contains the virtual path. Empty master (type empty) on failure.
    EntityMaster loadEntityMaster(std::string_view virtualPath);
    EntityMaster loadEntityMasterType(std::string_view typeName);

    // Existing file under a content root, or empty.
    std::filesystem::path resolveContentFile(std::string_view relative);
    // authoring content root + relative, whether or not the file exists yet.
    std::filesystem::path authoringContentFile(std::string_view relative);
}
