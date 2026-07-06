#pragma once

// ============================================================================
// counters.hpp — Umbrella entry point for the profiling counter surface
// ============================================================================
//
// FASE 21 split: counters.hpp was a 494L monolithic header coupling the
// X-macro vocabulary with the concrete struct/accessor definitions.  Both
// responsibilities now live in dedicated headers:
//
//   - render_counter_macros.hpp   — all CHRONON_COUNTERS_<DOMAIN>(X) macros,
//                                   plus the umbrella CHRONON_RENDER_COUNTERS
//                                   and the SYSTEM / SETUP variants.
//   - render_counter_types.hpp    — DirtyFallbackSlot, RenderCountersRaw,
//                                   RenderCounters, thread_local_counters(),
//                                   render_counters_field_count(), kCounterNames.
//
// This umbrella preserves backward compatibility: every existing include of
// <chronon3d/core/profiling/counters.hpp> still pulls in the full surface.
//
// Consumers needing only the introspection helpers (field count, name
// array) can include render_counter_types.hpp directly without paying
// for the macro vocabulary.  Consumers defining X-emitted structs not
// in this file can include render_counter_macros.hpp directly.
// ============================================================================

#include <chronon3d/core/profiling/render_counter_macros.hpp>
#include <chronon3d/core/profiling/render_counter_types.hpp>
