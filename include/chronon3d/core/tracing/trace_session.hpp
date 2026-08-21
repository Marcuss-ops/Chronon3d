#pragma once

// ============================================================================
// trace_session.hpp — Job-scoped Perfetto trace session
//
// TraceSession owns the capture of one render job's timeline.  It is
// job-scoped (one instance per RenderJob), NOT process-global: only the
// Perfetto SDK bootstrap is once-per-process, and that lives in
// src/core/tracing/perfetto_backend.cpp (hidden behind a pimpl so no
// Perfetto type leaks into the public header).
//
// Usage:
//   chronon3d::trace::TraceSession session;
//   auto ok = session.start(options);          // options.enabled == false → no-op
//   ... render job runs, CHRONON_TRACE_* emit events ...
//   auto ok = session.finish();               // writes options.output (.pftrace)
//
// When CHRONON3D_ENABLE_TRACING is compiled out, start()/finish() are
// no-ops returning success — matching the no-op contract of the
// CHRONON_TRACE_* macros.
// ============================================================================

#include <memory>

#include "chronon3d/core/tracing/trace_options.hpp"
#include "chronon3d/core/types/result.hpp"

namespace chronon3d::trace {

/// Error codes surfaced by TraceSession.
enum class TraceError {
    kDisabled,       // tracing compiled out (no CHRONON3D_ENABLE_TRACING)
    kNotInitialized, // Perfetto SDK init failed
    kStartFailed,    // session could not start
    kStopFailed,     // session could not stop/flush
    kWriteFailed,    // trace output file could not be written
};

class TraceSession {
public:
    TraceSession();
    ~TraceSession();
    TraceSession(const TraceSession&) = delete;
    TraceSession& operator=(const TraceSession&) = delete;

    /// One-time process-wide Perfetto SDK init (kInProcessBackend +
    /// TrackEvent::Register).  Called lazily by start(); exposed so CLI
    /// entry points can fail fast before running a render.
    static Result<bool, TraceError> EnsureInitialized();

    /// Start a job-scoped capture with the given options.  When
    /// `options.enabled` is false, this is a no-op returning success.
    Result<bool, TraceError> start(const TraceOptions& options);

    /// Stop the capture, drain the in-memory buffer and write the trace to
    /// `options.output`.  Safe to call when no session is active (no-op).
    Result<bool, TraceError> finish();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace chronon3d::trace
