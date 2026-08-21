#pragma once

// ============================================================================
// tracing.hpp — Timeline tracing macro surface
//
// This is the ONLY timeline-tracing entry point.  The current backend is
// Tracy (gated by CHRONON_PROFILING); the macros are intentionally kept
// backend-agnostic so a future Perfetto backend can swap in underneath
// without touching call sites.
//
// Responsibility split (AGENTS.md / profiling.hpp migration):
//   - profiling/timing.hpp          — now()/duration_* numeric timing
//   - profiling/profiling_context.hpp — counter + pool thread-locals
//   - tracing/tracing.hpp           — CHRONON_ZONE* timeline macros (here)
// ============================================================================

#ifdef CHRONON_PROFILING
#include <tracy/Tracy.hpp>
#define CHRONON_ZONE(name) \
    ZoneScopedN(name)
#define CHRONON_ZONE_C(name, cat) \
    ZoneScopedN(name)
#else
#ifndef ZoneScoped
#define ZoneScoped
#endif
#ifndef ZoneScopedN
#define ZoneScopedN(name)
#endif
#define CHRONON_ZONE(name) \
    do { (void)sizeof(name); } while (false)
#define CHRONON_ZONE_C(name, cat) \
    do { (void)sizeof(name); (void)sizeof(cat); } while (false)
#endif
