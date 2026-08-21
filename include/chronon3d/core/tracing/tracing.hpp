#pragma once

// ============================================================================
// tracing.hpp — Timeline tracing macro surface
//
// This is the ONLY timeline-tracing entry point.  Two macro families:
//
//   CHRONON_ZONE / CHRONON_ZONE_C   — legacy Tracy surface (transitional,
//                                     removed when the Perfetto migration
//                                     completes; see Fase 5 of the plan).
//   CHRONON_TRACE_*                 — Perfetto track-event surface (new).
//
// Both families compile to no-ops when CHRONON3D_ENABLE_TRACING is off, so
// call sites are safe in every build configuration.  With tracing enabled,
// CHRONON_TRACE_* map 1:1 onto the Perfetto TrackEvent API and carry the
// "chronon.*" category registry declared in tracing_categories.hpp.
//
// Responsibility split (AGENTS.md / profiling.hpp migration):
//   - profiling/timing.hpp          — now()/duration_* numeric timing
//   - profiling/profiling_context.hpp — counter + pool thread-locals
//   - tracing/tracing.hpp           — CHRONON_TRACE_* timeline macros (here)
// ============================================================================

#include "chronon3d/core/tracing/tracing_categories.hpp"

#ifdef CHRONON3D_ENABLE_TRACING
#include <perfetto.h>

// ── Legacy Tracy surface (transitional — Fase 5 removes it) ──────────────
#include <tracy/Tracy.hpp>
#define CHRONON_ZONE(name) \
    ZoneScopedN(name)
#define CHRONON_ZONE_C(name, cat) \
    ZoneScopedN(name)

// ── Perfetto surface (new canonical timeline tracing) ────────────────────
// `cat` MUST be a static string literal from tracing_categories.hpp
// (e.g. "chronon.frame") so TRACE_EVENT resolves it at compile time.

/// Scoped slice: emitted on the calling thread's track, closed at scope end.
#define CHRONON_TRACE_SCOPE(cat, name) \
    TRACE_EVENT(cat, name)

/// Numeric counter sample on a counter track (e.g. queue depths, VRAM).
#define CHRONON_TRACE_COUNTER(cat, name, value) \
    TRACE_COUNTER(cat, name, value)

/// Manual slice begin (paired with CHRONON_TRACE_END).
#define CHRONON_TRACE_BEGIN(cat, name) \
    TRACE_EVENT_BEGIN(cat, name)

/// Manual slice end (pairs with CHRONON_TRACE_BEGIN).
#define CHRONON_TRACE_END(cat) \
    TRACE_EVENT_END(cat)

/// Frame slice with a `frame` debug annotation for cross-thread correlation.
#define CHRONON_TRACE_FRAME(cat, name, frame_id) \
    TRACE_EVENT(cat, name, [&](::perfetto::EventContext ctx) { \
        auto* da = ctx.event()->add_debug_annotations(); \
        da->set_name("frame"); \
        da->set_uint_value(static_cast<uint64_t>(frame_id)); \
    })

#else // !CHRONON3D_ENABLE_TRACING

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

#define CHRONON_TRACE_SCOPE(cat, name) \
    do { (void)sizeof(cat); (void)sizeof(name); } while (false)
#define CHRONON_TRACE_COUNTER(cat, name, value) \
    do { (void)sizeof(cat); (void)sizeof(name); (void)sizeof(value); } while (false)
#define CHRONON_TRACE_BEGIN(cat, name) \
    do { (void)sizeof(cat); (void)sizeof(name); } while (false)
#define CHRONON_TRACE_END(cat) \
    do { (void)sizeof(cat); } while (false)
#define CHRONON_TRACE_FRAME(cat, name, frame_id) \
    do { (void)sizeof(cat); (void)sizeof(name); (void)sizeof(frame_id); } while (false)

#endif // CHRONON3D_ENABLE_TRACING
