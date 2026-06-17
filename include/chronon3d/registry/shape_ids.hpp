#pragma once

#include <string_view>

namespace chronon3d::registry::shape_ids {

inline constexpr std::string_view Rect = "shape.rect";
inline constexpr std::string_view RoundedRect = "shape.rounded_rect";
inline constexpr std::string_view Circle = "shape.circle";
inline constexpr std::string_view Line = "shape.line";
inline constexpr std::string_view Path = "shape.path";
inline constexpr std::string_view Image = "shape.image";
inline constexpr std::string_view TiledImage = "shape.tiled_image";
inline constexpr std::string_view GridBackground = "shape.grid_background";
inline constexpr std::string_view Mesh = "shape.mesh";
inline constexpr std::string_view Text = "shape.text";
inline constexpr std::string_view TextRun = "shape.text_run";

} // namespace chronon3d::registry::shape_ids
