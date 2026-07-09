#pragma once

// ---------------------------------------------------------------------------
// glyph_selector_random.hpp — Deterministic permutation/random helpers
// extracted from glyph_selector.cpp (FASE 8 Step 1)
// ---------------------------------------------------------------------------

#include <chronon3d/core/types/types.hpp>

#include <cstdint>
#include <vector>

namespace chronon3d::detail {

/// Deterministic pseudo-random float in [0, 1) from (seed, unit_index).
f32 hash_to_unit_float(u64 seed, u64 unit_index);

/// Return a Fisher-Yates permutation (bijection) for the given (seed, total_units).
/// Thread-safe via thread_local cache — each thread maintains its own.
const std::vector<u32>& get_or_build_permutation(u64 seed, u32 total_units);

} // namespace chronon3d::detail
