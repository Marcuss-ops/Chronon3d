#pragma once

#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/media/media_placement.hpp>

#include <string>

namespace chronon3d {

struct ImageParams {
    std::string asset_path{};

    // Legacy compatibility field. New code must write asset_path. The field
    // cannot carry [[deprecated]] while the canonical fallback helper still
    // reads it under targets that enforce -Werror=deprecated-declarations.
    // Remove both this field and the fallback branch in one caller-migration
    // commit once all pre-asset_path initializers have moved.
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
