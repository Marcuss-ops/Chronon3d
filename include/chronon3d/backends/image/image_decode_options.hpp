#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <string>
#include <chronon3d/assets/asset_metadata.hpp>

namespace chronon3d {

enum class PremultiplyAlpha : unsigned char {
    No,
    Yes,
};

enum class OrientationPolicy : unsigned char {
    IgnoreExif,
    ApplyExif,
};

struct ImageDecodeOptions {
    ColorSpace color_space{ColorSpace::SRGB};
    PremultiplyAlpha premultiply{PremultiplyAlpha::Yes};
    OrientationPolicy orientation{OrientationPolicy::ApplyExif};
    // Optional per-image OCIO input transform. The declared output space is
    // the project working space and must be scene-linear BT.709-compatible.
    std::string ocio_config_path;
    std::string ocio_input_space;
    std::string ocio_working_space;

    friend constexpr bool operator==(const ImageDecodeOptions&, const ImageDecodeOptions&) = default;
};

struct ImageAssetKey {
    std::filesystem::path canonical_path;
    ImageDecodeOptions options{};

    friend bool operator==(const ImageAssetKey&, const ImageAssetKey&) = default;
};

struct ImageAssetKeyHash {
    std::size_t operator()(const ImageAssetKey& key) const noexcept {
        std::size_t hash = std::hash<std::filesystem::path>{}(key.canonical_path);
        const auto mix = [&hash](std::size_t value) {
            hash ^= value + static_cast<std::size_t>(0x9e3779b9u) +
                    (hash << 6u) + (hash >> 2u);
        };
        mix(static_cast<std::size_t>(key.options.color_space));
        mix(static_cast<std::size_t>(key.options.premultiply));
        mix(static_cast<std::size_t>(key.options.orientation));
        mix(std::hash<std::string>{}(key.options.ocio_config_path));
        mix(std::hash<std::string>{}(key.options.ocio_input_space));
        mix(std::hash<std::string>{}(key.options.ocio_working_space));
        return hash;
    }
};

} // namespace chronon3d
