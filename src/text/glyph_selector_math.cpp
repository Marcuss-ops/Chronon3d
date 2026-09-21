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
#include <chronon3d/core/random/deterministic_random.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace chronon3d {
namespace detail {

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
            const auto perm = random::permutation(
                random::Seed{random_seed}, total_units);
            return perm[original_index];
        }
    }

    return original_index;
}

} // namespace detail
} // namespace chronon3d
