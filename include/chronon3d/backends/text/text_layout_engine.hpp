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
#include <algorithm>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace chronon3d {

struct TextLine {
    std::string text;
    float       width{0.0f};
    Vec2        position{0.0f, 0.0f};
};

struct TextLayoutResult {
    std::vector<TextLine> lines;
    float resolved_font_size{0.0f};
    Vec2  size{0.0f, 0.0f};
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
    [[nodiscard]] static TextLayoutResult layout(const TextLayoutInput& in) {
        TextLayoutResult out;
        if (in.text.empty()) {
            return out;
        }

        const float font_size = std::max(1.0f, in.style.size);
        const float line_height = std::max(0.1f, in.style.line_height);
        const float advance_scale = std::max(0.01f, in.style.tracking);
        const auto measure_char = [&](char c) {
            if (in.char_width) {
                return std::max(0.0f, in.char_width(c, font_size));
            }
            return font_size * 0.6f;
        };

        std::vector<std::string> paragraphs;
        {
            std::istringstream iss(in.text);
            std::string line;
            while (std::getline(iss, line)) {
                paragraphs.push_back(line);
            }
            if (paragraphs.empty()) {
                paragraphs.push_back(in.text);
            }
        }

        const bool wrap = in.box.enabled && in.box.size.x > 1.0f;
        const float max_width = in.box.size.x > 0.0f ? in.box.size.x : 0.0f;
        const int max_lines = in.style.max_lines > 0 ? in.style.max_lines : 0;

        auto push_line = [&](std::string text, float width) {
            if (max_lines > 0 && static_cast<int>(out.lines.size()) >= max_lines) {
                out.clipped = true;
                return false;
            }
            TextLine line;
            line.text = std::move(text);
            line.width = width;
            out.lines.push_back(std::move(line));
            return true;
        };

        for (const auto& paragraph : paragraphs) {
            if (!wrap) {
                float width = 0.0f;
                for (char c : paragraph) {
                    width += measure_char(c) + in.style.tracking;
                }
                width = std::max(0.0f, width);
                if (!push_line(paragraph, width)) break;
                continue;
            }

            std::istringstream words_stream(paragraph);
            std::string word;
            std::string current;
            float current_width = 0.0f;

            auto flush_current = [&]() -> bool {
                if (current.empty()) {
                    return true;
                }
                const bool ok = push_line(current, current_width);
                current.clear();
                current_width = 0.0f;
                return ok;
            };

            while (words_stream >> word) {
                float word_width = 0.0f;
                for (char c : word) {
                    word_width += measure_char(c) + in.style.tracking;
                }

                const float space_width = current.empty() ? 0.0f : measure_char(' ') + in.style.tracking;
                const float next_width = current_width + space_width + word_width;

                if (!current.empty() && next_width > max_width && current_width > 0.0f) {
                    if (!flush_current()) break;
                }

                if (!current.empty()) {
                    current.push_back(' ');
                    current_width += space_width;
                }
                current += word;
                current_width += word_width;
            }

            if (!current.empty() && !flush_current()) break;
        }

        if (out.lines.empty()) {
            out.lines.push_back(TextLine{.text = in.text, .width = 0.0f, .position = {0.0f, 0.0f}});
        }

        float max_line_width = 0.0f;
        for (auto& line : out.lines) {
            max_line_width = std::max(max_line_width, line.width);
        }

        for (usize i = 0; i < out.lines.size(); ++i) {
            auto& line = out.lines[i];
            if (in.style.align == TextAlign::Center) {
                line.position.x = (max_line_width - line.width) * 0.5f;
            } else if (in.style.align == TextAlign::Right) {
                line.position.x = max_line_width - line.width;
            } else {
                line.position.x = 0.0f;
            }
            line.position.y = static_cast<float>(i) * font_size * line_height;
        }

        out.size.x = max_line_width;
        out.size.y = static_cast<float>(out.lines.size()) * font_size * line_height;
        out.resolved_font_size = font_size;
        return out;
    }
};

} // namespace chronon3d
