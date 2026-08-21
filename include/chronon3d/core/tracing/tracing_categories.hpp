#pragma once

// ============================================================================
// tracing_categories.hpp — Timeline tracing categories
//
// Two views of the same vocabulary:
//
//   1. Legacy constants (chronon3d::trace_category) — short Tracy-era
//      category names consumed by CHRONON_ZONE_C call sites.  Kept until
//      Fase 2 migrates every call site to the Perfetto category strings.
//
//   2. Perfetto category registry (CHRONON3D_ENABLE_TRACING only) — the
//      formal TrackEvent category set.  Normal categories are captured in
//      light traces; categories tagged "debug"/"slow" are off by default
//      and require an explicit TraceOptions level that enables them
//      (kNodes / kFull).
//
// The Perfetto registry follows the documented split:
//   - this header:  PERFETTO_DEFINE_CATEGORIES(...)   (declaration, inline)
//   - src/core/tracing/tracing_categories.cpp: PERFETTO_TRACK_EVENT_STATIC_STORAGE()
//     (exactly one TU in the program defines the runtime storage)
// ============================================================================

#include <string_view>

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

namespace chronon3d::trace_category {

// Legacy Tracy-era category constants.  Migrated to "chronon.*" Perfetto
// category strings in Fase 2; these stay for the CHRONON_ZONE_C call sites.
inline constexpr std::string_view kGraph{"graph"};
inline constexpr std::string_view kFrame{"frame"};
inline constexpr std::string_view kNode{"node"};
inline constexpr std::string_view kEffect{"effect"};
inline constexpr std::string_view kText{"text"};
inline constexpr std::string_view kImage{"image"};
inline constexpr std::string_view kRasterize{"rasterize"};
inline constexpr std::string_view kTimeline{"timeline"};
inline constexpr std::string_view kComposite{"composite"};
inline constexpr std::string_view kDownsample{"downsample"};
inline constexpr std::string_view kPipeline{"pipeline"};
inline constexpr std::string_view kOutput{"output"};

} // namespace chronon3d::trace_category
