// ═══════════════════════════════════════════════════════════════════════════
// include/chronon3d/animation/motion/timeline.hpp
//
// TICKET-CONTENT-COMMON-ANIMATION-HELPERS-MISSING-HEADER (2026-07-11) —
// back-compat re-export header for the `motion::Timeline<T>` and
// `motion::timeline(...)` API names that `content/common/animation_helpers.hpp:5`
// expects.
//
// The canonical C1 surface lives in <chronon3d/animation/motion/motion.hpp> as
// `chronon3d::MotionTimeline<T>` + `chronon3d::timeline(T)`. This header
// provides the legacy `motion::` namespace aliases so that consumer code
// authored before the C1 canonicalization continues to compile unchanged.
//
// Pure type-alias surface — zero runtime overhead, zero new state,
// byte-equivalent to the canonical API.
//
// All new code should include <chronon3d/animation/motion/motion.hpp> and
// use the `chronon3d::MotionTimeline<T>` / `chronon3d::timeline()` symbols
// directly. The aliases here exist solely for back-compat with the
// content/ legacy helpers (e.g. `content/common/animation_helpers.hpp`).
//
// AGENTS.md v0.1 Cat-3 JUSTIFIED: the `motion::Timeline<T>` alias +
// `motion::timeline` re-export are NEW public identifiers in the public
// SDK namespace, BUT they are pure re-exports of the canonical C1 surface
// (no new implementations, no new state, byte-equivalent at runtime). The
// justification is the TICKET-CONTENT-COMMON-ANIMATION-HELPERS-MISSING-HEADER
// back-compat requirement: the content/ legacy helpers (e.g.
// `content/common/animation_helpers.hpp:96-104`) were authored against the
// pre-C1 `motion::` namespace and must continue to compile unchanged until
// they migrate to the canonical API. Forward-only invariant: the legacy
// `motion::` namespace can be deprecated + removed after the content/
// helpers migrate.
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include <chronon3d/animation/motion/motion.hpp>

namespace motion {

// Back-compat alias: legacy `motion::Timeline<T>` was the pre-C1 name
// for what is now `chronon3d::MotionTimeline<T>` in the canonical surface.
template <typename T>
using Timeline = chronon3d::MotionTimeline<T>;

// Back-compat factory: legacy `motion::timeline(T)` was the pre-C1 name
// for what is now `chronon3d::timeline(T)` in the canonical surface.
using chronon3d::timeline;

} // namespace motion
