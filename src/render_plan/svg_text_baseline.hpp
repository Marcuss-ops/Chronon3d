#pragma once

#include <filesystem>
#include <optional>
#include <string_view>

namespace chronon3d::render_plan::detail {

/// Resolve an SVG horizontal dominant-baseline offset using the font's
/// OpenType BASE table and HarfBuzz's script-aware fallback rules. Result is
/// measured in logical pixels, positive down from the Roman baseline.
[[nodiscard]] std::optional<float> svg_script_baseline_offset(
    const std::filesystem::path& font_path,
    float font_size,
    std::string_view text,
    std::string_view baseline);

} // namespace chronon3d::render_plan::detail
