#pragma once

// ============================================================================
// tracing.hpp — Timeline tracing macro surface
//
// This is the ONLY timeline-tracing entry point.  The CHRONON_TRACE_* macro
// family maps 1:1 onto the Perfetto TrackEvent API and carries the
// "chronon.*" category registry declared in tracing_categories.hpp.
//
// All macros compile to no-ops when CHRONON3D_ENABLE_TRACING is off, so
// call sites are safe in every build configuration (trace-OFF gate: zero
// overhead beyond the probe in TracingActive()).
//
// Responsibility split (AGENTS.md / profiling.hpp migration):
//   - profiling/timing.hpp          — now()/duration_* numeric timing
//   - profiling/profiling_context.hpp — counter + pool thread-locals
//   - tracing/tracing.hpp           — CHRONON_TRACE_* timeline macros (here)
// ============================================================================

#include "chronon3d/core/tracing/tracing_categories.hpp"
#include "chronon3d/core/tracing/trace_ids.hpp"

#include <cstdint>

namespace chronon3d::tracing {

/// Current Perfetto trace-clock time in nanoseconds (the same clock used by
/// every CHRONON_TRACE_* event timestamp).  Returns 0 when tracing is
/// compiled out.  Used to anchor GPU timestamp calibration to the CPU
/// timeline (VK_EXT_calibrated_timestamps).
inline std::int64_t TraceTimeNs() {
#ifdef CHRONON3D_ENABLE_TRACING
    return static_cast<std::int64_t>(::perfetto::TrackEvent::GetTraceTimeNs());
#else
    return 0;
#endif
}

/// Cheap runtime probe: true only when a Perfetto session is actually
/// capturing.  Callers use it to skip non-free sampling (e.g. mutex-guarded
/// pool/queue stats) when tracing is compiled in but no trace is active —
/// the trace-OFF gate wants zero overhead, and "compiled in but idle" should
/// cost nothing beyond the probe itself.  Always false when tracing is
/// compiled out (no-op build stays at zero cost).
inline bool TracingActive() {
#ifdef CHRONON3D_ENABLE_TRACING
    return ::perfetto::TrackEvent::IsEnabled();
#else
    return false;
#endif
}

/// Global track id for the dedicated GPU timeline track "Chronon Vulkan
/// Queue".  GPU work is NOT recorded on any CPU thread track: it gets its
/// own track, with slices positioned using real calibrated GPU timestamps
/// converted into the Perfetto trace-clock domain.
inline constexpr std::uint64_t kVulkanQueueTrackId =
    static_cast<std::uint64_t>(0x4348524F4E4F4E56ULL);  // "CHRONONV"

/// Name the dedicated GPU track.  Safe to call before a trace session
/// starts; the descriptor persists in the Perfetto track registry and is
/// emitted when the track is first used.  No-op when tracing is compiled
/// out.
inline void RegisterVulkanQueueTrack() {
#ifdef CHRONON3D_ENABLE_TRACING
    const auto track = ::perfetto::Track(kVulkanQueueTrackId);
    auto desc = track.Serialize();
    desc.set_name("Chronon Vulkan Queue");
    ::perfetto::TrackEvent::SetTrackDescriptor(track, desc);
#else
    (void)0;
#endif
}

} // namespace chronon3d::tracing

#ifdef CHRONON3D_ENABLE_TRACING
#include <perfetto.h>

// ── Perfetto surface (canonical timeline tracing) ────────────────────────
// `cat` MUST be a static string literal from tracing_categories.hpp
// (e.g. "chronon.frame") so TRACE_EVENT resolves it at compile time.

/// Scoped slice: emitted on the calling thread's track, closed at scope end.
#define CHRONON_TRACE_SCOPE(cat, name) \
    TRACE_EVENT(cat, name)

/// Scoped slice with one uint64 debug annotation (`ann_name` -> `ann_value`),
/// e.g. stable_node_id on node_execute (plan §7/§8).  The annotation lambda
/// only runs when the category is active, so building the value costs nothing
/// when tracing is off.
#define CHRONON_TRACE_SCOPE_ANNOTATED(cat, name, ann_name, ann_value) \
    TRACE_EVENT(cat, name, [&](::perfetto::EventContext ctx) { \
        auto* da = ctx.event()->add_debug_annotations(); \
        da->set_name(ann_name); \
        da->set_uint_value(static_cast<uint64_t>(ann_value)); \
    })

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

/// Scoped slice with job_id + frame_id debug annotations (no flow).
#define CHRONON_TRACE_SCOPE_IDS(cat, name, job_id, frame_id) \
    TRACE_EVENT(cat, name, [&](::perfetto::EventContext ctx) { \
        auto* da = ctx.event()->add_debug_annotations(); \
        da->set_name("job"); \
        da->set_uint_value(static_cast<uint64_t>(job_id)); \
        da = ctx.event()->add_debug_annotations(); \
        da->set_name("frame"); \
        da->set_uint_value(static_cast<uint64_t>(frame_id)); \
    })

/// Non-terminating Flow: work continues on another thread carrying the same
/// flow id (e.g. decode worker → render thread).
#define CHRONON_TRACE_FLOW(cat, name, flow_id) \
    TRACE_EVENT(cat, name, ::perfetto::Flow::ProcessScoped(flow_id))

/// Non-terminating Flow + job_id/frame_id debug annotations.
#define CHRONON_TRACE_FLOW_IDS(cat, name, flow_id, job_id, frame_id) \
    TRACE_EVENT(cat, name, ::perfetto::Flow::ProcessScoped(flow_id), \
        [&](::perfetto::EventContext ctx) { \
            auto* da = ctx.event()->add_debug_annotations(); \
            da->set_name("job"); \
            da->set_uint_value(static_cast<uint64_t>(job_id)); \
            da = ctx.event()->add_debug_annotations(); \
            da->set_name("frame"); \
            da->set_uint_value(static_cast<uint64_t>(frame_id)); \
        })

/// TerminatingFlow: work completes on this thread (e.g. writer/NVENC sink).
#define CHRONON_TRACE_FLOW_END(cat, name, flow_id) \
    TRACE_EVENT(cat, name, ::perfetto::TerminatingFlow::ProcessScoped(flow_id))

/// TerminatingFlow + job_id/frame_id debug annotations.
#define CHRONON_TRACE_FLOW_END_IDS(cat, name, flow_id, job_id, frame_id) \
    TRACE_EVENT(cat, name, ::perfetto::TerminatingFlow::ProcessScoped(flow_id), \
        [&](::perfetto::EventContext ctx) { \
            auto* da = ctx.event()->add_debug_annotations(); \
            da->set_name("job"); \
            da->set_uint_value(static_cast<uint64_t>(job_id)); \
            da = ctx.event()->add_debug_annotations(); \
            da->set_name("frame"); \
            da->set_uint_value(static_cast<uint64_t>(frame_id)); \
        })

// ── GPU slices on the dedicated queue track (calibrated timestamps) ────────
// `start_ns`/`end_ns` MUST be in the Perfetto trace-clock domain (see
// chronon3d::tracing::TraceTimeNs()).  Emitted ONLY with real calibrated GPU
// timestamps — never positioned arbitrarily on a CPU track.

/// Begin a GPU slice on the "Chronon Vulkan Queue" track at `start_ns`.
#define CHRONON_TRACE_GPU_BEGIN(name, start_ns) \
    TRACE_EVENT_BEGIN("chronon.gpu", name, \
        ::perfetto::Track(chronon3d::tracing::kVulkanQueueTrackId), \
        static_cast<uint64_t>(start_ns))

/// End the GPU slice opened on the queue track at `end_ns`.
#define CHRONON_TRACE_GPU_END(end_ns) \
    TRACE_EVENT_END("chronon.gpu", \
        ::perfetto::Track(chronon3d::tracing::kVulkanQueueTrackId), \
        static_cast<uint64_t>(end_ns))

#else // !CHRONON3D_ENABLE_TRACING

#define CHRONON_TRACE_SCOPE(cat, name) \
    do { (void)sizeof(cat); (void)sizeof(name); } while (false)
#define CHRONON_TRACE_SCOPE_ANNOTATED(cat, name, ann_name, ann_value) \
    do { (void)sizeof(cat); (void)sizeof(name); (void)sizeof(ann_name); (void)sizeof(ann_value); } while (false)
#define CHRONON_TRACE_COUNTER(cat, name, value) \
    do { (void)sizeof(cat); (void)sizeof(name); (void)sizeof(value); } while (false)
#define CHRONON_TRACE_BEGIN(cat, name) \
    do { (void)sizeof(cat); (void)sizeof(name); } while (false)
#define CHRONON_TRACE_END(cat) \
    do { (void)sizeof(cat); } while (false)
#define CHRONON_TRACE_FRAME(cat, name, frame_id) \
    do { (void)sizeof(cat); (void)sizeof(name); (void)sizeof(frame_id); } while (false)
#define CHRONON_TRACE_SCOPE_IDS(cat, name, job_id, frame_id) \
    do { (void)sizeof(cat); (void)sizeof(name); (void)sizeof(job_id); (void)sizeof(frame_id); } while (false)
#define CHRONON_TRACE_FLOW(cat, name, flow_id) \
    do { (void)sizeof(cat); (void)sizeof(name); (void)sizeof(flow_id); } while (false)
#define CHRONON_TRACE_FLOW_IDS(cat, name, flow_id, job_id, frame_id) \
    do { (void)sizeof(cat); (void)sizeof(name); (void)sizeof(flow_id); \
         (void)sizeof(job_id); (void)sizeof(frame_id); } while (false)
#define CHRONON_TRACE_FLOW_END(cat, name, flow_id) \
    do { (void)sizeof(cat); (void)sizeof(name); (void)sizeof(flow_id); } while (false)
#define CHRONON_TRACE_FLOW_END_IDS(cat, name, flow_id, job_id, frame_id) \
    do { (void)sizeof(cat); (void)sizeof(name); (void)sizeof(flow_id); \
         (void)sizeof(job_id); (void)sizeof(frame_id); } while (false)
#define CHRONON_TRACE_GPU_BEGIN(name, start_ns) \
    do { (void)sizeof(name); (void)sizeof(start_ns); } while (false)
#define CHRONON_TRACE_GPU_END(end_ns) \
    do { (void)sizeof(end_ns); } while (false)

#endif // CHRONON3D_ENABLE_TRACING
