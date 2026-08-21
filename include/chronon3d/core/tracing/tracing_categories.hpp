#pragma once

// ============================================================================
// tracing_categories.hpp — Timeline tracing categories
//
// The Perfetto category registry (CHRONON3D_ENABLE_TRACING only) — the
// formal TrackEvent category set.  Normal categories are captured in light
// traces; categories tagged "debug"/"slow" are off by default and require
// an explicit TraceOptions level that enables them (kNodes / kFull).
//
// The Perfetto registry follows the documented split:
//   - this header:  PERFETTO_DEFINE_CATEGORIES(...)   (declaration, inline)
//   - src/core/tracing/tracing_categories.cpp: PERFETTO_TRACK_EVENT_STATIC_STORAGE()
//     (exactly one TU in the program defines the runtime storage)
// ============================================================================

#ifdef CHRONON3D_ENABLE_TRACING
#include <perfetto.h>

PERFETTO_DEFINE_CATEGORIES(
    // ── Normal categories (captured by default in light traces) ──────────
    ::perfetto::Category("chronon.frame"),
    ::perfetto::Category("chronon.pipeline"),
    ::perfetto::Category("chronon.graph"),
    ::perfetto::Category("chronon.media"),
    ::perfetto::Category("chronon.gpu"),
    ::perfetto::Category("chronon.encode"),
    ::perfetto::Category("chronon.io"),
    // ── Detailed categories (debug/slow — off unless explicitly enabled) ──
    ::perfetto::Category("chronon.node").SetTags("debug", "slow"),
    ::perfetto::Category("chronon.cache").SetTags("debug", "slow"),
    ::perfetto::Category("chronon.memory").SetTags("debug", "slow"),
    ::perfetto::Category("chronon.text").SetTags("debug", "slow"),
    ::perfetto::Category("chronon.image").SetTags("debug", "slow"),
    ::perfetto::Category("chronon.effect").SetTags("debug", "slow"),
    ::perfetto::Category("chronon.surface").SetTags("debug", "slow"));
#endif // CHRONON3D_ENABLE_TRACING
