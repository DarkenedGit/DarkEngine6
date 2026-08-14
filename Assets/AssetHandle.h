#pragma once
#include <cstdint>
#include <memory>

namespace Dark
{

    using AssetID                = uint64_t;
    constexpr AssetID NULL_ASSET = 0;

    enum class AssetType : uint8_t
    {
        None = 0,
        Mesh,
        Texture2D,
        Material,
        Shader,
        Audio,
        Scene,
    };

    // Forward-declare base Asset
    struct Asset
    {
        AssetID   id     = NULL_ASSET;
        AssetType type   = AssetType::None;
        virtual ~Asset() = default;
    };

    template <typename T> using AssetRef = std::shared_ptr<T>;

    // Weak handle (never keeps the asset alive)
    template <typename T> using AssetWeakRef = std::weak_ptr<T>;

} // namespace Dark
