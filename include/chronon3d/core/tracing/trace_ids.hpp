#pragma once

// ============================================================================
// trace_ids.hpp — Universal trace correlation IDs
//
// The ID vocabulary used to correlate timeline events across threads and
// hardware engines (NVDEC → render graph → writer/NVENC):
//
//   TraceJobId    — stable per render job (one export/session).
//   TraceFrameId  — the frame number being processed by each stage.
//   MakeFlowId()  — deterministic (job, frame) → 64-bit Perfetto flow id.
//
// The same MakeFlowId(job, frame) is computed independently on every stage of
// the pipeline, so Perfetto links decode → render → encode into a single flow
// without any shared mutable tracing state between threads.
// ============================================================================

#include <cstdint>

namespace chronon3d::tracing {

/// Stable numeric id for one render job (export session). Synthesized by the
/// CLI at session setup; 0 means "no correlation context".
using TraceJobId = std::uint64_t;

/// The frame number a stage is processing (composition frame on the render
/// loop / writer, source frame on the decoder).
using TraceFrameId = std::uint64_t;

/// Sentinel: never a real flow (avoids accidental cross-job correlation).
inline constexpr TraceJobId kInvalidTraceJobId = 0;

/// Deterministic 64-bit flow id for (job, frame). Every pipeline stage
/// computes the same value from its own job id + frame number, so a Perfetto
/// flow can span the decode worker → render thread → writer thread chain
/// without shared state.
[[nodiscard]] inline constexpr std::uint64_t MakeFlowId(
    TraceJobId job_id, TraceFrameId frame_id) noexcept {
    // splitmix64 finalizer: cheap, avalanche-y, collision-resistant enough
    // for flow correlation across thousands of frames.
    std::uint64_t x = job_id * 0x9E3779B97F4A7C15ULL + frame_id;
    x ^= x >> 30;
    x *= 0xBF58476D1CE4E5B9ULL;
    x ^= x >> 27;
    x *= 0x94D049BB133111EBULL;
    x ^= x >> 31;
    return x;
}

} // namespace chronon3d::tracing
