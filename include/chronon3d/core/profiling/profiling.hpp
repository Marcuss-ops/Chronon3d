#pragma once

// ============================================================================
// profiling.hpp — Backward-compatible umbrella
//
// The monolithic profiling header was split into single-responsibility
// headers.  This umbrella preserves the full original surface for every
// existing consumer; new code should include the narrower headers.
//
//   - timing.hpp                 — profiling::Clock + now()/elapsed_*/duration_*
//   - profiling_context.hpp      — counter/pool thread-locals + enums +
//                                  GpuUploadProducerScope/FramebufferAllocationScope/
//                                  ProfilingGuard
//   - render_counter_types.hpp   — RenderCounters full definition
//   - tracing/tracing.hpp        — CHRONON_ZONE / CHRONON_ZONE_C timeline macros
//   - tracing/tracing_categories.hpp — chronon3d::trace_category namespace
// ============================================================================

#include <chronon3d/core/profiling/timing.hpp>
#include <chronon3d/core/profiling/profiling_context.hpp>
#include <chronon3d/core/profiling/render_counter_types.hpp>  // RenderCounters full definition
#include <chronon3d/core/tracing/tracing.hpp>
#include <chronon3d/core/tracing/tracing_categories.hpp>
