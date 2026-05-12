#pragma once

#include <chronon3d/core/types.hpp>
#include <filesystem>
#include <string>

namespace chronon3d {

enum class AssetType {
    Image,
    Font,
    Video,
    Audio,
    Mesh,
    Unknown
};

// Color encoding of the asset as stored on disk.
enum class ColorSpace {
    SRGB,        // gamma-encoded sRGB (most PNGs/JPEGs)
    LinearSRGB   // linear light (EXR, HDR, internally rendered buffers)
};

// How the alpha channel relates to the RGB values.
enum class AlphaMode {
    None,           // no alpha channel
    Straight,       // RGB independent of alpha (most source files)
    Premultiplied   // RGB already multiplied by alpha (compositing-ready)
};

// Immutable metadata about a registered asset.
struct AssetMetadata {
    AssetType              type{AssetType::Unknown};
    std::filesystem::path  path;
    std::string            path_hash;   // hex string of XXH3_64 of path bytes

    // Populated lazily by the renderer/loader on first use.
    i32  width{0};
    i32  height{0};

    float duration_seconds{0.0f};  // video/audio only
    float fps{0.0f};               // video only

    ColorSpace color_space{ColorSpace::SRGB};
    AlphaMode  alpha_mode{AlphaMode::Straight};
};

} // namespace chronon3d
