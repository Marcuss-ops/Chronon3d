#pragma once

#include <chronon3d/effects/effect_params.hpp>

#include <string_view>

namespace chronon3d::effects {

/// Parse the deterministic, RGB 3D subset of the Iridas .cube format.
/// LUT_1D_SIZE and unknown directives are rejected.
[[nodiscard]] Lut3DParams parse_cube_lut(std::string_view source,
                                         float amount = 1.0f);

} // namespace chronon3d::effects
