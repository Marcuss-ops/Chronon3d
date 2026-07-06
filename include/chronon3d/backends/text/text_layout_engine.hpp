#pragma once

#include <chronon3d/scene/model/shape/shape.hpp>
#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/backends/text/bidi_segmenter.hpp>

// ── Extracted modules ────────────────────────────────────────────────────
// text_unicode_utils.hpp: UTF-8 codec + grapheme cluster utilities
// text_layout_types.hpp: TextLayoutRun/Input/Line/Result data structures
// text_layout_single.hpp: measurement helpers + layout_single_run()
// text_layout_inline.hpp: layout_inline_runs() with per-run styling
#include "text_unicode_utils.hpp"
#include "text_layout_types.hpp"
#include "text_layout_single.hpp"
#include "text_layout_inline.hpp"

namespace chronon3d {

/// Text layout engine — single public entry point.
///
/// Dispatches to layout_single_run() or layout_inline_runs() based on
/// whether the input carries per-run styling (bidi-segmented or
/// inline-formatted text).  All measurement + layout logic lives in
/// the extracted module headers above; this class is a thin shim.
class TextLayoutEngine {
public:
    [[nodiscard]] static TextLayoutResult layout(const TextLayoutInput& input) {
        TextLayoutInput current_input = input;
        if (current_input.runs.empty() &&
            current_input.style.shaping.direction == TextDirection::Auto) {
            auto bidi_runs = segment_bidi_runs(current_input.text);
            if (!bidi_runs.empty()) {
                bool has_rtl = false;
                for (const auto& br : bidi_runs) {
                    if (br.direction == TextDirection::RTL) {
                        has_rtl = true;
                        break;
                    }
                }
                if (has_rtl) {
                    for (const auto& br : bidi_runs) {
                        TextLayoutRun r;
                        r.text = br.text;
                        r.style = current_input.style;
                        r.style.shaping.direction = br.direction;
                        current_input.runs.push_back(std::move(r));
                    }
                }
            }
        }
        const bool auto_fit = input.style.auto_fit || input.style.auto_scale;

        const auto measure_with_size = [&](float size) {
            TextLayoutInput test_input = current_input;
            test_input.style.size = size;
            return test_input.runs.empty()
                ? detail::text_layout::layout_single_run(test_input)
                : detail::text_layout::layout_inline_runs(test_input);
        };

        if (auto_fit && input.box.enabled && input.box.size.x > 0.0f && input.box.size.y > 0.0f) {
            float low = std::max(1.0f, input.style.min_size);
            float high = std::max(low, input.style.size);
            if (input.style.max_size > 0.0f) {
                high = std::min(high, input.style.max_size);
            }
            float best_size = low;

            for (int step = 0; step < 8; ++step) {
                const float mid = (low + high) * 0.5f;
                TextLayoutResult temp = measure_with_size(mid);
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

        return current_input.runs.empty()
            ? detail::text_layout::layout_single_run(current_input)
            : detail::text_layout::layout_inline_runs(current_input);
    }
};

} // namespace chronon3d
