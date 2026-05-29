#pragma once

#include <chronon3d/scene/shape.hpp>

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace chronon3d {

struct TextLayoutInput {
    std::string text;
    TextStyle style{};
    TextBox box{};
    const void* char_width_ctx{nullptr};
    float (*char_width_fn)(const void* ctx, char c, float font_size){nullptr};
};

struct TextLayoutLine {
    std::string text;
    Vec2 position{0.0f, 0.0f};
    float width{0.0f};
};

struct TextLayoutResult {
    Vec2 size{0.0f, 0.0f};
    std::vector<TextLayoutLine> lines;
    float font_size{0.0f};
};

class TextLayoutEngine {
private:
    [[nodiscard]] static TextLayoutResult layout_single_run(const TextLayoutInput& input) {
        TextLayoutResult result;
        const float font_size = std::max(1.0f, input.style.size);
        result.font_size = font_size;
        const float line_height = std::max(1.0f, font_size * input.style.line_height);
        const float max_width = input.box.enabled && input.box.size.x > 0.0f
            ? input.box.size.x
            : 0.0f;

        auto measure_char = [&](char c) {
            if (input.char_width_fn) {
                return std::max(0.0f, input.char_width_fn(input.char_width_ctx, c, font_size));
            }
            return font_size * 0.6f;
        };

        auto measure_string = [&](std::string_view s) {
            float w = 0.0f;
            for (char c : s) {
                w += measure_char(c) + input.style.tracking;
            }
            return w;
        };

        std::vector<std::string> raw_lines;
        std::string current_line;
        float current_width = 0.0f;

        auto push_current_line = [&]() {
            raw_lines.push_back(std::move(current_line));
            current_line.clear();
            current_width = 0.0f;
        };

        // Tokenize text by words/whitespace
        std::vector<std::pair<std::string, bool>> tokens; // {text, is_whitespace}
        std::string current_token;
        bool in_space = false;

        for (char c : input.text) {
            if (c == '\n') {
                if (!current_token.empty()) {
                    tokens.push_back({current_token, in_space});
                    current_token.clear();
                }
                tokens.push_back({"\n", false});
            } else if (c == ' ' || c == '\t') {
                if (!current_token.empty() && !in_space) {
                    tokens.push_back({current_token, false});
                    current_token.clear();
                }
                in_space = true;
                current_token.push_back(c);
            } else {
                if (!current_token.empty() && in_space) {
                    tokens.push_back({current_token, true});
                    current_token.clear();
                }
                in_space = false;
                current_token.push_back(c);
            }
        }
        if (!current_token.empty()) {
            tokens.push_back({current_token, in_space});
        }

        // Layout lines
        const bool wrapping_enabled = max_width > 0.0f && input.style.wrap != TextWrap::None;

        for (const auto& token_pair : tokens) {
            const std::string& token = token_pair.first;
            bool is_space = token_pair.second;

            if (token == "\n") {
                push_current_line();
                continue;
            }

            if (!wrapping_enabled) {
                current_line += token;
                current_width += measure_string(token);
                continue;
            }

            float token_w = measure_string(token);

            if (input.style.wrap == TextWrap::Word) {
                // If it doesn't fit on current line
                if (current_width + token_w > max_width) {
                    if (!current_line.empty()) {
                        push_current_line();
                        // If it's a space at start of new line, skip it
                        if (is_space) continue;
                    }
                    // If the single token itself is wider than max_width, character wrap it
                    if (token_w > max_width) {
                        for (char c : token) {
                            float cw = measure_char(c) + input.style.tracking;
                            if (current_width + cw > max_width && !current_line.empty()) {
                                push_current_line();
                            }
                            current_line.push_back(c);
                            current_width += cw;
                        }
                    } else {
                        current_line = token;
                        current_width = token_w;
                    }
                } else {
                    current_line += token;
                    current_width += token_w;
                }
            } else if (input.style.wrap == TextWrap::Character) {
                for (char c : token) {
                    float cw = measure_char(c) + input.style.tracking;
                    if (current_width + cw > max_width && !current_line.empty()) {
                        push_current_line();
                    }
                    current_line.push_back(c);
                    current_width += cw;
                }
            }
        }

        if (!current_line.empty() || raw_lines.empty()) {
            raw_lines.push_back(std::move(current_line));
        }

        // Handle max_lines and ellipsis
        const int max_allowed_lines = input.style.max_lines;
        const bool apply_ellipsis = input.style.ellipsis || input.style.overflow == TextOverflow::Ellipsis;

        if (max_allowed_lines > 0 && static_cast<int>(raw_lines.size()) > max_allowed_lines) {
            raw_lines.resize(max_allowed_lines);
            if (apply_ellipsis && !raw_lines.empty()) {
                std::string& last_line = raw_lines.back();
                const float ellipsis_w = measure_string("...");
                
                // Shrink line until ellipsis fits
                while (!last_line.empty() && measure_string(last_line) + ellipsis_w > max_width) {
                    last_line.pop_back();
                }
                last_line += "...";
            }
        }

        // Final result assembly & alignment
        float max_seen_width = 0.0f;
        std::vector<float> line_widths;
        for (const auto& line_str : raw_lines) {
            float w = measure_string(line_str);
            line_widths.push_back(w);
            max_seen_width = std::max(max_seen_width, w);
        }

        result.size.x = max_seen_width;
        result.size.y = static_cast<float>(raw_lines.size()) * line_height;

        for (size_t i = 0; i < raw_lines.size(); ++i) {
            TextLayoutLine line;
            line.text = std::move(raw_lines[i]);
            line.width = line_widths[i];

            float dx = 0.0f;
            if (input.style.align == TextAlign::Center) {
                dx = (max_seen_width - line.width) * 0.5f;
            } else if (input.style.align == TextAlign::Right) {
                dx = max_seen_width - line.width;
            }
            line.position = {dx, static_cast<float>(i) * line_height};
            result.lines.push_back(std::move(line));
        }

        return result;
    }

public:
    [[nodiscard]] static TextLayoutResult layout(const TextLayoutInput& input) {
        TextLayoutInput current_input = input;
        const bool auto_fit = input.style.auto_fit || input.style.auto_scale;

        if (auto_fit && input.box.enabled && input.box.size.x > 0.0f && input.box.size.y > 0.0f) {
            float low = std::max(1.0f, input.style.min_size);
            float high = std::max(low, input.style.size);
            if (input.style.max_size > 0.0f) {
                high = std::min(high, input.style.max_size);
            }
            float best_size = low;

            for (int step = 0; step < 8; ++step) {
                float mid = (low + high) * 0.5f;
                current_input.style.size = mid;
                TextLayoutResult temp = layout_single_run(current_input);
                if (temp.size.x <= input.box.size.x && temp.size.y <= input.box.size.y) {
                    best_size = mid;
                    low = mid + 0.1f;
                } else {
                    high = mid - 0.1f;
                }
                if (high < low) break;
            }
            current_input.style.size = best_size;
        }

        return layout_single_run(current_input);
    }
};

} // namespace chronon3d
