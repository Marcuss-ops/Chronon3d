#pragma once

#include <chronon3d/scene/shape.hpp>

#include <algorithm>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace chronon3d {

struct TextLayoutInput {
    std::string text;
    TextStyle style{};
    TextBox box{};
    std::function<float(char, float)> char_width;
};

struct TextLayoutLine {
    std::string text;
    Vec2 position{0.0f, 0.0f};
    float width{0.0f};
};

struct TextLayoutResult {
    Vec2 size{0.0f, 0.0f};
    std::vector<TextLayoutLine> lines;
};

class TextLayoutEngine {
public:
    [[nodiscard]] static TextLayoutResult layout(const TextLayoutInput& input) {
        TextLayoutResult result;
        const float font_size = std::max(1.0f, input.style.size);
        const float line_height = std::max(1.0f, font_size * input.style.line_height);
        const float max_width = input.box.enabled && input.box.size.x > 0.0f
            ? input.box.size.x
            : 0.0f;

        auto measure_char = [&](char c) {
            if (input.char_width) {
                return std::max(0.0f, input.char_width(c, font_size));
            }
            return font_size * 0.6f;
        };

        auto push_line = [&](std::string line_text, float line_width) {
            TextLayoutLine line;
            line.text = std::move(line_text);
            line.position = {0.0f, static_cast<float>(result.lines.size()) * line_height};
            line.width = line_width;
            result.size.x = std::max(result.size.x, line_width);
            result.lines.push_back(std::move(line));
        };

        std::string current;
        float current_width = 0.0f;
        auto flush_current = [&]() {
            push_line(std::move(current), current_width);
            current.clear();
            current_width = 0.0f;
        };

        const auto emit_wrapped_token = [&](std::string_view token, bool is_space) {
            if (token.empty() && !is_space) {
                return;
            }

            float token_width = 0.0f;
            for (char c : token) {
                token_width += measure_char(c) + input.style.tracking;
            }

            const bool needs_wrap = max_width > 0.0f && !current.empty() && (current_width + token_width > max_width);
            if (needs_wrap) {
                flush_current();
            }

            current.append(token.begin(), token.end());
            current_width += token_width;

            if (is_space && !token.empty() && max_width > 0.0f && current_width > max_width && !current.empty()) {
                flush_current();
            }
        };

        std::string token;
        for (size_t i = 0; i < input.text.size(); ++i) {
            const char c = input.text[i];
            if (c == '\n') {
                emit_wrapped_token(token, false);
                token.clear();
                flush_current();
                continue;
            }

            if (c == ' ' || c == '\t') {
                emit_wrapped_token(token, false);
                token.clear();
                std::string space(1, c);
                emit_wrapped_token(space, true);
                continue;
            }

            token.push_back(c);
        }

        emit_wrapped_token(token, false);
        if (current.empty() && result.lines.empty()) {
            push_line({}, 0.0f);
        } else if (!current.empty()) {
            flush_current();
        }

        if (result.lines.empty()) {
            push_line({}, 0.0f);
        }

        result.size.y = static_cast<float>(result.lines.size()) * line_height;
        return result;
    }
};

} // namespace chronon3d
