#pragma once

#include <chronon3d/scene/model/shape/shape.hpp>
#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <string>
#include <vector>

namespace chronon3d {

struct TextLayoutRun {
    std::string text;
    TextStyle style{};
    bool is_line_break{false};
    bool is_space{false};
    bool is_decorative_star{false};
    bool use_advance_override{false};
    float advance_override{0.0f};
    float star_inner_radius{0.0f};
    float star_outer_radius{0.0f};
    int star_points{8};
};

struct TextLayoutLineRun {
    std::string text;
    Vec2 position{0.0f, 0.0f};
    float width{0.0f};
    TextStyle style{};
    bool is_space{false};
    bool is_decorative_star{false};
    float star_inner_radius{0.0f};
    float star_outer_radius{0.0f};
    int star_points{8};
};

struct TextLayoutInput {
    std::string text;
    std::vector<TextLayoutRun> runs;
    TextStyle style{};
    TextBox box{};
    // Legacy per-char callback (used when font_engine is null)
    const void* char_width_ctx{nullptr};
    float (*char_width_fn)(const void* ctx, char c, float font_size){nullptr};
    // FreeType/HarfBuzz-based measurement (used by TextAnimator for per-glyph bbox)
    const FontEngine* font_engine{nullptr};
    FontSpec font_spec{};
    // Blend2D-based measurement (preferred for layout; eliminates FT+HB vs B2D
    // discrepancy since the same engine measures and renders).  bl_font_ptr
    // points to a BLFontFace; bl_measure_fn creates BLFont at requested size.
    const void* bl_font_ptr{nullptr};
    float (*bl_measure_fn)(const void* font_face, std::string_view text, float font_size){nullptr};
};

struct TextLayoutLine {
    std::string text;
    Vec2 position{0.0f, 0.0f};
    float width{0.0f};
    float ascent{0.0f};
    float descent{0.0f};
    float baseline{0.0f};
    std::vector<TextLayoutLineRun> runs;
};

struct TextLayoutResult {
    Vec2 size{0.0f, 0.0f};
    std::vector<TextLayoutLine> lines;
    float font_size{0.0f};
};

} // namespace chronon3d
