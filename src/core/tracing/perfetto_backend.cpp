// ============================================================================
// perfetto_backend.cpp — One-time Perfetto SDK bootstrap
//
// Perfetto requires a single process-wide SDK initialization before any
// tracing session can be created:
//
//   perfetto::TracingInitArgs args;
//   args.backends = perfetto::kInProcessBackend;   // headless, no daemon
//   perfetto::Tracing::Initialize(args);
//   perfetto::TrackEvent::Register();
//
// This file owns that bootstrap, guarded by std::call_once so it happens
// exactly once per process no matter how many TraceSession instances (or
// render jobs) are created.  Perfetto is fully encapsulated here — no
// Perfetto type appears in the public tracing headers.
//
// The in-process backend writes traces to a ring buffer in memory and only
// drains them at TraceSession::finish(), so the render hot path never
// performs trace I/O.
// ============================================================================

#include "chronon3d/core/tracing/trace_session.hpp"
#include "chronon3d/core/tracing/tracing_categories.hpp"

#include <mutex>

#ifdef CHRONON3D_ENABLE_TRACING
#include <perfetto.h>
#endif

namespace chronon3d::trace {
namespace {

#ifdef CHRONON3D_ENABLE_TRACING
std::once_flag g_perfetto_init_flag;

void InitPerfettoOnce() {
    perfetto::TracingInitArgs args;
    args.backends = perfetto::kInProcessBackend;
    perfetto::Tracing::Initialize(args);
    perfetto::TrackEvent::Register();
}
#endif

} // namespace

Result<bool, TraceError> TraceSession::EnsureInitialized() {
#ifdef CHRONON3D_ENABLE_TRACING
    std::call_once(g_perfetto_init_flag, InitPerfettoOnce);
    return Result<bool, TraceError>(true);
#else
    return Result<bool, TraceError>(TraceError::kDisabled);
#endif
}

} // namespace chronon3d::trace
