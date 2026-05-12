#include <chronon3d/renderer/text_layout_engine.hpp>
#include <sstream>
#include <algorithm>

namespace chronon3d {

namespace {

float measure_word(const std::string& word, float font_size,
                   float tracking,
                   const std::function<float(char, float)>& char_width) {
    float w = 0.0f;
    for (char c : word)
        w += char_width(c, font_size) + tracking;
    return w;
}

float measure_space(float font_size,
                    const std::function<float(char, float)>& char_width) {
    return char_width(' ', font_size);
}

// Break text into lines that fit within box_width.
// Returns lines and whether the text was clipped by max_lines.
std::pair<std::vector<TextLine>, bool>
word_wrap(const std::string& text,
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
        if (max_lines > 0 && (int)lines.size() >= max_lines) {
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
            current_line  = word;
            current_width = ww;
        } else {
            if (current_width + space_w + ww <= box_width) {
                current_line  += ' ' + word;
                current_width += space_w + ww;
            } else {
                if (!push_line()) break;
                current_line  = word;
                current_width = ww;
            }
        }
    }

    if (!current_line.empty() && !clipped) {
        if (max_lines == 0 || (int)lines.size() < max_lines)
            lines.push_back({current_line, current_width});
        else
            clipped = true;
    }

    return {lines, clipped};
}

} // namespace

TextLayoutResult TextLayoutEngine::layout(const TextLayoutInput& in) {
    // Provide a fallback char-width function if none supplied.
    auto cw = in.char_width ? in.char_width
                            : [](char /*c*/, float sz) -> float { return sz * 0.55f; };

    float font_size = in.style.size;
    const float tracking = in.style.tracking;

    if (!in.box.enabled || in.box.size.x <= 0.0f) {
        // No box: single line, no wrapping.
        TextLayoutResult r;
        r.resolved_font_size = font_size;
        float w = measure_word(in.text, font_size, tracking, cw);
        r.lines.push_back({in.text, w});
        return r;
    }

    const float box_w = in.box.size.x;
    const int   max_l = in.style.max_lines;

    if (in.style.auto_scale) {
        // Binary search for largest font size that fits.
        float lo = in.style.min_size;
        float hi = std::min(font_size, in.style.max_size);
        float best = lo;

        for (int iter = 0; iter < 12; ++iter) {
            float mid = (lo + hi) * 0.5f;
            auto [lines, clipped] = word_wrap(in.text, mid, box_w, tracking, max_l, cw);
            if (!clipped) { best = mid; lo = mid; }
            else           hi = mid;
        }
        font_size = best;
    }

    auto [lines, clipped] = word_wrap(in.text, font_size, box_w, tracking, max_l, cw);

    TextLayoutResult r;
    r.resolved_font_size = font_size;
    r.lines              = std::move(lines);
    r.clipped            = clipped;
    return r;
}

} // namespace chronon3d
