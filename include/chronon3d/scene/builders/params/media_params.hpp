#pragma once

#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/media/media_placement.hpp>

#include <string>

namespace chronon3d {

struct ImageParams {
    std::string asset_path{};

    [[deprecated("Use asset_path instead; it is the manifest-clean field")]]
    std::string path{};

    Vec2 size{100.0f, 100.0f};
    Vec3 pos{0.0f, 0.0f, 0.0f};
    FitMode fit{FitMode::Cover};
    Vec2 focal_point{0.5f, 0.5f};
    ImageCrop crop{};
    f32 opacity{1.0f};
    f32 radius{0.0f};
};

namespace detail {

[[nodiscard]] inline std::string
image_params_resolve_path(const ImageParams& params) {
    return !params.asset_path.empty() ? params.asset_path : params.path;
}

} // namespace detail
} // namespace chronon3d
