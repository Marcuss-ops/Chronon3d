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

        // ── ADR-018 / user-spec: fits_inside(layout_box) gate ──────────
        // Local lambda (Cat-3 safe: no new public symbol; same as the
        // canonical 12-iter impl in src/scene/builders/text_run_builder.cpp
        // compile_or_cache_layout).  Returns true iff `bounds` fit within
        // `box` (component-wise <=, no epsilon nudge — pure binary
        // search is deterministic and requires no floating-point slack).
        const auto fits_inside = [](const Vec2& box, const Vec2& bounds) {
            return bounds.x <= box.x && bounds.y <= box.y;
        };

        const auto measure_with_size = [&](float size) {
            TextLayoutInput test_input = current_input;
            test_input.style.size = size;
            return test_input.runs.empty()
                ? detail::text_layout::layout_single_run(test_input)
                : detail::text_layout::layout_inline_runs(test_input);
        };

        if (auto_fit && input.box.enabled && input.box.size.x > 0.0f && input.box.size.y > 0.0f) {
            // ADR-018: shrink-only.  Authored size is the ceiling;
            // max_size clamps the ceiling; min_size is the floor.
            // Pure binary search — no epsilon nudge (the 0.1f nudge
            // in the prior 8-iter version broke determinism: two
            // calls with identical input could nudge by different
            // amounts due to fp rounding, producing cache-key drift).
            float       low  = std::max(1.0f, input.style.min_size);
            float       high = std::max(low, input.style.size);
            if (input.style.max_size > 0.0f) {
                high = std::min(high, input.style.max_size);
            }
            // best = the largest size in [low, high] that fits.
            // Fallback (no iteration succeeds) = low (smallest allowed).
            float best_size = low;

            // Pre-check: if authored size already fits, no search needed.
            // This is the common case for short text in wide boxes.
            {
                TextLayoutResult probe = measure_with_size(high);
                if (fits_inside(input.box.size, probe.size)) {
                    best_size = high;
                    current_input.style.size = best_size;
                } else {
                    // 12-iter binary search.  Fixed count guarantees
                    // termination (no infinite-loop risk) and full
                    // determinism (same input → same mid sequence →
                    // same cache keys).  2^12 = 4096 steps →
                    // ~0.02pt precision over 88pt range.
                    constexpr int kMaxIter = 12;
                    for (int step = 0; step < kMaxIter; ++step) {
                        const float mid = (low + high) * 0.5f;
                        TextLayoutResult temp = measure_with_size(mid);
                        if (fits_inside(input.box.size, temp.size)) {
                            best_size = mid;   // fits — try larger
                            low       = mid;
                        } else {
                            high      = mid;   // overflows — try smaller
                        }
                    }
                    current_input.style.size = best_size;
                }
            }
        }

        return current_input.runs.empty()
            ? detail::text_layout::layout_single_run(current_input)
            : detail::text_layout::layout_inline_runs(current_input);
    }
};

} // namespace chronon3d
