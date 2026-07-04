// SPDX-License-Identifier: MIT
#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// M1.5#3 — text_run_hash.hpp
//
// Single-responsibility sub-header for the hash free-function slot.
//
// Public surface (verbatim moved from the canonical text_run.hpp):
//   - `[[nodiscard]] u64 hash_text_run_shape(const TextRunShape& s);`
//   - `[[nodiscard]] u64 hash_text_run_shape(const TextRunShape& s, Frame frame);`
//
// Bodies remain in `src/text/text_run.cpp` (the canonical impl file).
// The free function family covers static text fingerprint (no frame
// fold) and the PR 10 AnimatedTextDocument state fold (frame overload).
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/text/text_run_shape.hpp>      // TextRunShape
#include <chronon3d/core/types/frame.hpp>         // Frame — for the cache-key overload

namespace chronon3d {

/// Free function to hash a TextRunShape for content fingerprinting.
[[nodiscard]] u64 hash_text_run_shape(const TextRunShape& s);

/// Optional overload that folds the AnimatedTextDocument state at the
/// requested integral frame.  When `s.animated_doc` is bound, this overload
/// samples the doc at `frame` and folds the per-frame transition_type +
/// active->utf8 + active->defaults.font + transition_text bytes +
/// morph_map bytes + mix value into the cache key, so Scramble / Morph /
/// CrossfadeLayouts / font-swap Cut renders correctly invalidate the
/// executor's per-frame node cache without false hits and without leaking
/// stale layout bytes.
///
/// The single-argument variant remains valid for shapes whose layout
/// never changes between frames (e.g. plain static text); its doc-fold
/// branch is a no-op.
///
/// Takes a `Frame` (not `SampleTime`) so this header does not need to
/// pull `sample_time.hpp` (FrameRate + TemporalSampleKey + math
/// headers) into every TU that uses TextRunShape.  Sub-frame precision
/// was not used by the implementation anyway.
[[nodiscard]] u64 hash_text_run_shape(const TextRunShape& s, Frame frame);

} // namespace chronon3d
