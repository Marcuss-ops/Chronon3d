#pragma once

// ============================================================================
// trace_options.hpp — Job-scoped trace session options
//
// TraceOptions is passed to TraceSession::start() and controls what gets
// captured for the duration of one render job.  The session itself is
// job-scoped (created/destroyed per RenderJob), while the Perfetto SDK is
// initialized once per process (see src/core/tracing/perfetto_backend.cpp).
// ============================================================================

#include <cstdint>
#include <filesystem>

namespace chronon3d::trace {

/// Trace capture level — maps to the Perfetto category sets in
/// tracing_categories.hpp.
///
///   kPipeline (default) — chronon.frame / pipeline / graph / media / gpu /
///                         encode / io — minimal overhead.
///   kNodes              — adds chronon.node / cache / surface — node-level
///                         execution detail (debug/slow-tagged categories).
///   kFull               — adds chronon.memory / text / image / effect —
///                         diagnostics only; higher overhead.
enum class TraceLevel : uint8_t {
    kPipeline = 0,
    kNodes = 1,
    kFull = 2,
};

struct TraceOptions {
    /// Master switch — when false, TraceSession::start() is a no-op and no
    /// trace file is produced.  Kept so the session API is safe to invoke
    /// unconditionally (mirrors the no-op contract of the CHRONON_TRACE_*
    /// macros when tracing is compiled out).
    bool enabled{false};

    /// Output path for the finished trace (render.pftrace).  Written only at
    /// TraceSession::finish(), never during the render hot path.
    std::filesystem::path output;

    /// Capture level (see TraceLevel above).
    TraceLevel level{TraceLevel::kPipeline};

    /// In-process ring-buffer size in MiB.  32 MiB covers a short golden
    /// clip at pipeline level; 64 MiB is the recommended headroom when node
    /// tracing is enabled.
    uint32_t buffer_mb{32};

    /// Include the warmup phase in the captured trace.  Off by default so
    /// steady-state frame timings are not polluted by first-frame caches.
    bool include_warmup{false};
};

} // namespace chronon3d::trace
