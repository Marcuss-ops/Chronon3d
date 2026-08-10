#pragma once

#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/media/media_placement.hpp>
#include <chronon3d/backends/image/image_decode_options.hpp>
#include <chronon3d/assets/asset_ref.hpp>
#include <chronon3d/scene/model/shape/shape.hpp>

#include <string>

namespace chronon3d {

struct ImageParams {
    /// Canonical asset identity used by new authoring code.
    assets::ImageRef source{};

    // Compatibility spelling retained for existing manifest-oriented callers.
    std::string asset_path{};

    Vec2 size{100.0f, 100.0f};
    Vec3 pos{0.0f, 0.0f, 0.0f};
    FitMode fit{FitMode::Cover};
    Vec2 focal_point{0.5f, 0.5f};
    ImageCrop crop{};
    f32 opacity{1.0f};
    f32 radius{0.0f};
    ImageDecodeOptions decode_options{};
};

namespace detail {

[[nodiscard]] inline std::string
image_params_resolve_path(const ImageParams& params) {
    if (!params.source.path().empty()) return params.source.path();
    return params.asset_path;
}

} // namespace detail
} // namespace chronon3d
