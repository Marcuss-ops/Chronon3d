// ============================================================================
// glyph_selector_math.cpp — Pure-math selector primitives (shape + order)
// ============================================================================
//
// FASE 23 split: glyph_selector.cpp was monolithic (~514L) and held map
// building, math primitives, compile + evaluate paths, and the legacy
// "evaluate without compile" public API all in one TU.  This file owns
// ONLY the pure-math selection primitives — no allocation, no input
// mutation, no TextUnitMap dependency:
//
//   detail::evaluate_selector_shape(shape, unit_position, start, end)
//       Compute the per-unit weight given the shape envelope
//       (Square / RampUp / RampDown / Triangle / Round / Smooth).
//       Handles the wrap-around fall-through via manual start ≤ end
//       swap (when start > end, weight is the reversed ramp).
//
//   detail::apply_selector_order(order, original_index, total, random_seed)
//       Permute the original index into the unit's display order
//       (Forward / Reverse / FromCenter / ToCenter / Random).  Random
//       delegates to `get_or_build_permutation` from
//       `glyph_selector_random.hpp` (FASE 8 permutation cache).
//       ToCenter recursively invokes FromCenter via the inverse map.
//
// Dependency: glyph_selector.hpp (type declarations), glyph_selector_random.hpp
// (Random-order permutation cache), standard <algorithm> + <cmath>.
// ============================================================================

#include <chronon3d/text/glyph_selector.hpp>
#include "glyph_selector_random.hpp"  // FASE 8 — Random permutation cache

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace chronon3d {
namespace detail {

f32 evaluate_selector_shape(
    TextSelectorShape shape,
    f32 unit_position,
    f32 start,
    f32 end
) {
    unit_position = std::clamp(unit_position, 0.0f, 1.0f);
    start = std::clamp(start, 0.0f, 1.0f);
    end   = std::clamp(end, 0.0f, 1.0f);

    if (start > end) {
        std::swap(start, end);
        unit_position = 1.0f - unit_position;
    }

    const f32 range = end - start;

    if (unit_position <= start) {
        if (shape == TextSelectorShape::RampDown) return 1.0f;
        return 0.0f;
    }
    if (unit_position >= end) {
        if (shape == TextSelectorShape::RampUp) return 1.0f;
        return 0.0f;
    }

    const f32 t = (range > 1e-7f) ? ((unit_position - start) / range) : 0.5f;

    switch (shape) {
        case TextSelectorShape::Square:
            return 1.0f;

        case TextSelectorShape::RampUp:
            return t;

        case TextSelectorShape::RampDown:
            return 1.0f - t;

        case TextSelectorShape::Triangle: {
            if (t <= 0.5f) return 2.0f * t;
            return 2.0f * (1.0f - t);
        }

        case TextSelectorShape::Round: {
            if (t <= 0.5f) {
                const f32 u = 2.0f * t;
                return 3.0f * u * u - 2.0f * u * u * u;
            }
            const f32 u = 2.0f * (1.0f - t);
            return 3.0f * u * u - 2.0f * u * u * u;
        }

        case TextSelectorShape::Smooth: {
            if (t <= 0.5f) {
                const f32 u = 2.0f * t;
                const f32 one_minus_u = 1.0f - u;
                return 1.0f - one_minus_u * one_minus_u * one_minus_u;
            }
            const f32 u = 2.0f * (1.0f - t);
            const f32 one_minus_u = 1.0f - u;
            return 1.0f - one_minus_u * one_minus_u * one_minus_u;
        }
    }

    return 0.0f;
}

u32 apply_selector_order(
    TextSelectorOrder order,
    u32 original_index,
    u32 total_units,
    u64 random_seed
) {
    if (total_units <= 1) return 0;

    switch (order) {
        case TextSelectorOrder::Forward:
            return original_index;

        case TextSelectorOrder::Reverse:
            return total_units - 1 - original_index;

        case TextSelectorOrder::FromCenter: {
            const u32 mid = total_units / 2;

            if (total_units % 2 == 1) {
                if (original_index == mid) return 0;
                if (original_index < mid) {
                    const u32 dist = mid - original_index;
                    return 2u * dist - 1u;
                }
                const u32 dist = original_index - mid;
                return 2u * dist;
            }

            if (original_index == mid - 1) return 0;
            if (original_index == mid)      return 1;

            if (original_index < mid - 1) {
                const u32 pair_dist = (mid - 1) - original_index;
                return 2u + 2u * (pair_dist - 1u);
            }

            const u32 pair_dist = original_index - mid;
            return 3u + 2u * (pair_dist - 1u);
        }

        case TextSelectorOrder::ToCenter: {
            const u32 from_centre = apply_selector_order(
                TextSelectorOrder::FromCenter, original_index, total_units, random_seed
            );
            return total_units - 1 - from_centre;
        }

        case TextSelectorOrder::Random: {
            const auto& perm = get_or_build_permutation(random_seed, total_units);
            return perm[original_index];
        }
    }

    return original_index;
}

} // namespace detail
} // namespace chronon3d
