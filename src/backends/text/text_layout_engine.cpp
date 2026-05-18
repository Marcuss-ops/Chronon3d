#include <chronon3d/backends/text/text_layout_engine.hpp>
#include <algorithm>
#include <cmath>
#include <sstream>

namespace chronon3d {

namespace {

float measure_word(const std::string& word, float font_size,
                   float tracking,
                   const std::function<float(char, float)>& char_width) {
    float w = 0.0f;
    for (char c : word) {
        w += char_width(c, font_size) + tracking;
    }
    return w;
}

float measure_space(float font_size,
                    const std::function<float(char, float)>& char_width) {
    return char_width(' ', font_size);
}

std::vector<std::string> split_paragraphs_preserve_empty(const std::string& text) {
    std::vector<std::string> paragraphs;
    std::string::size_type start = 0;

    while (start <= text.size()) {
        const auto end = text.find('\n', start);
        if (end == std::string::npos) {
            paragraphs.emplace_back(text.substr(start));
            break;
        }

        paragraphs.emplace_back(text.substr(start, end - start));
        start = end + 1;

        if (start == text.size()) {
            paragraphs.emplace_back("");
            break;
        }
    }

    if (paragraphs.empty()) {
        paragraphs.emplace_back("");
    }

    return paragraphs;
}

std::pair<std::vector<TextLine>, bool>
wrap_paragraph(const std::string& text,
               float font_size,
               float box_width,
               float tracking,
               int max_lines,
               const std::function<float(char, float)>& cw) {
    std::vector<TextLine> lines;
    bool clipped = false;

    std::istringstream ss(text);
    std::string word;
    std::string current_line;
    float current_width = 0.0f;
    const float space_w = measure_space(font_size, cw);

    auto push_line = [&]() -> bool {
        if (max_lines > 0 && static_cast<int>(lines.size()) >= max_lines) {
            clipped = true;
            return false;
        }
        lines.push_back({current_line, current_width});
        current_line.clear();
        current_width = 0.0f;
        return true;
    };

    while (ss >> word) {
        const float ww = measure_word(word, font_size, tracking, cw);

        if (current_line.empty()) {
            current_line = word;
            current_width = ww;
        } else if (current_width + space_w + ww <= box_width) {
            current_line += ' ';
            current_line += word;
            current_width += space_w + ww;
        } else {
            if (!push_line()) {
                break;
            }
            current_line = word;
            current_width = ww;
        }
    }

    if (!current_line.empty() && !clipped) {
        if (max_lines == 0 || static_cast<int>(lines.size()) < max_lines) {
            lines.push_back({current_line, current_width});
        } else {
            clipped = true;
        }
    }

    return {lines, clipped};
}

} // namespace

TextLayoutResult TextLayoutEngine::layout(const TextLayoutInput& in) {
    auto cw = in.char_width
        ? in.char_width
        : [](char /*c*/, float sz) -> float { return sz * 0.55f; };

    const auto paragraphs = split_paragraphs_preserve_empty(in.text);
    const float base_font_size = in.style.size;
    const float tracking = in.style.tracking;

    auto build_layout = [&](float font_size) {
        TextLayoutResult result;
        result.resolved_font_size = font_size;

        const float line_h = font_size * in.style.line_height;
        int max_lines = in.style.max_lines;

        if (in.box.enabled && in.box.size.y > 0.0f && line_h > 0.0f) {
            const int by_height = static_cast<int>(std::floor(in.box.size.y / line_h));
            if (by_height > 0) {
                max_lines = (max_lines == 0) ? by_height : std::min(max_lines, by_height);
            }
        }

        std::vector<TextLine> lines;
        bool clipped = false;

        auto append_line = [&](TextLine line) {
            if (max_lines > 0 && static_cast<int>(lines.size()) >= max_lines) {
                clipped = true;
                return false;
            }
            lines.push_back(std::move(line));
            return true;
        };

        for (const std::string& paragraph : paragraphs) {
            if (max_lines > 0 && static_cast<int>(lines.size()) >= max_lines) {
                clipped = true;
                break;
            }

            if (!in.box.enabled || in.box.size.x <= 0.0f) {
                TextLine line;
                line.text = paragraph;
                line.width = measure_word(paragraph, font_size, tracking, cw);
                if (!append_line(std::move(line))) {
                    break;
                }
                continue;
            }

            const int remaining = (max_lines == 0)
                ? 0
                : std::max(0, max_lines - static_cast<int>(lines.size()));

            auto [wrapped_lines, paragraph_clipped] =
                wrap_paragraph(paragraph, font_size, in.box.size.x, tracking, remaining, cw);

            if (paragraph.empty() && wrapped_lines.empty()) {
                wrapped_lines.push_back({"", 0.0f});
            }

            for (auto& line : wrapped_lines) {
                if (!append_line(std::move(line))) {
                    break;
                }
            }

            clipped = clipped || paragraph_clipped;
        }

        if (lines.empty()) {
            lines.push_back({"", 0.0f});
        }

        float content_width = 0.0f;
        for (const auto& line : lines) {
            content_width = std::max(content_width, line.width);
        }

        const float align_extent =
            (in.box.enabled && in.box.size.x > 0.0f) ? in.box.size.x : content_width;

        for (usize i = 0; i < lines.size(); ++i) {
            auto& line = lines[i];
            float x = 0.0f;
            switch (in.style.align) {
                case TextAlign::Center:
                    x = std::max(0.0f, (align_extent - line.width) * 0.5f);
                    break;
                case TextAlign::Right:
                    x = std::max(0.0f, align_extent - line.width);
                    break;
                default:
                    x = 0.0f;
                    break;
            }
            line.position = {x, static_cast<float>(i) * line_h};
        }

        result.lines = std::move(lines);
        result.size = {content_width, static_cast<float>(result.lines.size()) * line_h};
        result.clipped = clipped;
        return result;
    };

    if (in.style.auto_scale && in.box.enabled && in.box.size.x > 0.0f) {
        float lo = in.style.min_size;
        float hi = std::min(base_font_size, in.style.max_size);
        float best = lo;

        for (int iter = 0; iter < 12; ++iter) {
            const float mid = (lo + hi) * 0.5f;
            auto candidate = build_layout(mid);

            const bool fits_width = candidate.size.x <= in.box.size.x + 0.5f;
            const bool fits_height = !in.box.enabled || in.box.size.y <= 0.0f
                || candidate.size.y <= in.box.size.y + 0.5f;

            if (!candidate.clipped && fits_width && fits_height) {
                best = mid;
                lo = mid;
            } else {
                hi = mid;
            }
        }

        return build_layout(best);
    }

    return build_layout(base_font_size);
}

} // namespace chronon3d
