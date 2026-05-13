#pragma once

// TextLayoutEngine -- layout algorithm for multi-line text.
// Decoupled from the actual font renderer: callers supply a char-width
// function so the engine can measure without importing stb_truetype.
//
// Usage (from TextRenderer):
//   auto result = TextLayoutEngine::layout({
//       .text       = shape.text,
//       .style      = shape.style,
//       .box        = shape.box,
//       .char_width = [&](char c, float sz) { return measure(font, c, sz); }
//   });
//   for (const auto& line : result.lines) draw_line(line);

#include <chronon3d/scene/shape.hpp>
#include <functional>
#include <string>
#include <vector>

namespace chronon3d {

struct TextLine {
    std::string text;
    float       width{0.0f};
};

struct TextLayoutResult {
    std::vector<TextLine> lines;
    float resolved_font_size{0.0f};
    bool  clipped{false};  // true if text was cut because max_lines exceeded
};

struct TextLayoutInput {
    std::string  text;
    TextStyle    style;
    TextBox      box;
    // Returns advance width (px) for a single character at the given font size.
    // If not provided, layout falls back to a fixed-width estimate.
    std::function<float(char, float /*font_size*/)> char_width;
};

class TextLayoutEngine {
public:
    [[nodiscard]] static TextLayoutResult layout(const TextLayoutInput& in);
};

} // namespace chronon3d
