#pragma once

#include <cstddef>

// Forward declaration — the canonical full definition lives at
// include/chronon3d/core/profiling/render_counter_types.hpp (existing
// public SDK surface).  Forward-declared here at the ROOT `chronon3d`
// namespace (NOT nested inside `chronon3d::graph`) to avoid NAME
// SHADOWING: a forward-decl inside `chronon3d::graph` would create a
// distinct, incomplete `chronon3d::graph::RenderCounters` type, which
// is NOT type-compatible with the canonical `chronon3d::RenderCounters`
// passed at the function-signature boundary.
namespace chronon3d {
struct RenderCounters;
}

namespace chronon3d::graph {

// ── roll_up_level_durations — whole-level phase fold ──────────────────────
//
// OPT-DEAD-TIMINGS — replaces LevelTimings (per-node pmr::vector
// accumulators that Stage 3 orphaned: constructed per level per frame,
// never written, still heap-allocated). The executor now folds only the
// four measured whole-level phase durations into RenderCounters using the
// identical relaxed fetch_add pattern the struct's roll_up() used.
void roll_up_level_durations(::chronon3d::RenderCounters& counters,
                             double dispatch_ms,
                             double input_ms,
                             double schedule_ms,
                             double framebuffer_ms);

} // namespace chronon3d::graph
