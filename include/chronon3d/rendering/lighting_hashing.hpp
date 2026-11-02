#pragma once

#include <chronon3d/rendering/light_context.hpp>
#include <chronon3d/scene/model/shape/material_2_5d.hpp>
#include <cstdint>

namespace chronon3d::rendering {

[[nodiscard]] std::uint64_t hash_light_context_v2(const LightContext& light);
[[nodiscard]] std::uint64_t hash_material_2_5d_v2(const Material2_5D& material);

} // namespace chronon3d::rendering
