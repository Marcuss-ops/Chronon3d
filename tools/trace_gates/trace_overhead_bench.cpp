// ============================================================================
// tools/trace_gates/trace_overhead_bench.cpp
//
// Plan §23 overhead gates.  ONE source, compiled twice:
//
//   - WITHOUT CHRONON3D_ENABLE_TRACING  -> baseline binary (macros no-op)
//   - WITH    CHRONON3D_ENABLE_TRACING  -> tracing binary; runtime mode via argv:
//         off      no session started            (gate: <0.5% vs baseline)
//         pipeline TraceSession level kPipeline  (gate: <2%   vs baseline)
//         nodes    TraceSession level kNodes     (gate: <5%   vs baseline)
//
// The per-frame work is a fixed CPU-bound busy loop wrapped in the same
// macro structure the real pipeline uses (frame slice + ids + counter +
// node slice), so the measured delta is exactly the tracing cost.  Prints
// `ms=<total>` on stdout; the driver script takes best-of-N and computes
// the percentages.
// ============================================================================

#include "chronon3d/core/tracing/tracing.hpp"
#include "chronon3d/core/tracing/tracing_categories.hpp"
#include "chronon3d/core/tracing/trace_session.hpp"
#include "chronon3d/core/tracing/trace_ids.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

// CPU-bound busy work (~1 ms on typical desktop hardware).  The sink is
// volatile so the loop can never be elided; identical in every config.
volatile std::uint64_t g_sink = 0;
void busy_work(int iterations) {
    std::uint64_t acc = 0;
    for (int i = 0; i < iterations; ++i) {
        acc += static_cast<std::uint64_t>(i) * 2654435761ULL;
        acc ^= acc >> 13;
        acc *= 0x9E3779B97F4A7C15ULL;
    }
    g_sink += acc;
}

constexpr int kFrames = 200;
constexpr int kBusyIterations = 220000;

} // namespace

int main(int argc, char** argv) {
#ifdef CHRONON3D_ENABLE_TRACING
    const std::string mode = argc > 1 ? argv[1] : "off";
    using namespace chronon3d::trace;
    TraceOptions o;
    o.enabled = (mode != "off");
    o.output = "/tmp/chronon3d_trace_overhead.pftrace";
    o.buffer_mb = 32;
    if (mode == "nodes") o.level = TraceLevel::kNodes;
    else if (mode == "full") o.level = TraceLevel::kFull;
    else o.level = TraceLevel::kPipeline;
    TraceSession session;
    auto r = session.start(o);
    if (!r) {
        std::printf("bench-start-failed\n");
        return 2;
    }
#endif

    const std::uint64_t job = 0xABCD'EF01ULL;
    const auto t0 = std::chrono::steady_clock::now();
    for (int f = 0; f < kFrames; ++f) {
        CHRONON_TRACE_SCOPE("chronon.frame", "Frame");
        CHRONON_TRACE_SCOPE_IDS("chronon.pipeline", "GraphExecute",
                                job, static_cast<std::uint64_t>(f));
        CHRONON_TRACE_SCOPE("chronon.node", "NodeExecute");  // debug/slow: only at nodes+
        CHRONON_TRACE_COUNTER("chronon.pipeline", "frames_in_flight", f + 1);
        busy_work(kBusyIterations);
    }
    const auto t1 = std::chrono::steady_clock::now();

#ifdef CHRONON3D_ENABLE_TRACING
    (void)session.finish();
#endif

    const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    std::printf("ms=%.3f\n", ms);
    return 0;
}
