// ============================================================================
// trace_session.cpp — Job-scoped Perfetto trace session
//
// Implements the TraceSession pimpl declared in the public header.  All
// Perfetto usage is confined to this TU (plus perfetto_backend.cpp for the
// one-time SDK init and tracing_categories.cpp for the category storage).
//
// Lifecycle:
//   start(options)
//     - no-op success when options.enabled == false
//     - builds a TraceConfig: one ring buffer of options.buffer_mb MiB,
//       "track_event" data source, disabled_categories "*" plus the
//       enabled category set for the requested TraceLevel
//     - perfetto::Tracing::NewTrace(kInProcessBackend) + Setup + Start
//   ... render job runs; CHRONON_TRACE_* events are recorded in memory ...
//   finish()
//     - StopBlocking + ReadTraceBlocking
//     - writes the drained bytes to options.output (render.pftrace)
//
// No trace I/O happens on the render hot path: the buffer is drained only
// here, after the job completes.
// ============================================================================

#include "chronon3d/core/tracing/trace_session.hpp"

#include <cstdio>
#include <fstream>
#include <vector>

#ifdef CHRONON3D_ENABLE_TRACING
#include <perfetto.h>
#endif

namespace chronon3d::trace {

#ifdef CHRONON3D_ENABLE_TRACING

namespace {

/// Category sets per TraceLevel.  Normal categories first; debug/slow-tagged
/// categories are appended at kNodes / kFull and are off by default because
/// of their tags (they are explicitly re-enabled here via enabled_categories).
const char* kPipelineCategories[] = {
    "chronon.frame",  "chronon.pipeline", "chronon.graph", "chronon.media",
    "chronon.gpu",    "chronon.encode",   "chronon.io",
};
const char* kNodesExtraCategories[] = {
    "chronon.node", "chronon.cache", "chronon.surface",
};
const char* kFullExtraCategories[] = {
    "chronon.memory", "chronon.text", "chronon.image", "chronon.effect",
};

void AddEnabledCategories(perfetto::protos::gen::TrackEventConfig& tec,
                          TraceLevel level) {
    for (const char* cat : kPipelineCategories) {
        tec.add_enabled_categories(cat);
    }
    if (level >= TraceLevel::kNodes) {
        for (const char* cat : kNodesExtraCategories) {
            tec.add_enabled_categories(cat);
        }
    }
    if (level >= TraceLevel::kFull) {
        for (const char* cat : kFullExtraCategories) {
            tec.add_enabled_categories(cat);
        }
    }
}

} // namespace

struct TraceSession::Impl {
    std::unique_ptr<perfetto::TracingSession> session;
    TraceOptions options;
};

TraceSession::TraceSession() : impl_(std::make_unique<Impl>()) {}
TraceSession::~TraceSession() = default;

Result<bool, TraceError> TraceSession::start(const TraceOptions& options) {
    if (!options.enabled) {
        impl_->options = options;
        return Result<bool, TraceError>(true); // no-op session
    }

    auto init = EnsureInitialized();
    if (!init) {
        return Result<bool, TraceError>(init.error());
    }

    perfetto::TraceConfig cfg;
    cfg.add_buffers()->set_size_kb(static_cast<uint32_t>(options.buffer_mb) * 1024);

    auto* ds_cfg = cfg.add_data_sources()->mutable_config();
    ds_cfg->set_name("track_event");

    // The amalgamated SDK exposes the track-event config as a gen message
    // that is serialized into the data-source config's raw field.
    perfetto::protos::gen::TrackEventConfig tec;
    tec.add_disabled_categories("*");
    AddEnabledCategories(tec, options.level);
    ds_cfg->set_track_event_config_raw(tec.SerializeAsString());

    impl_->session = perfetto::Tracing::NewTrace(perfetto::kInProcessBackend);
    impl_->session->Setup(cfg);
    impl_->session->StartBlocking();
    impl_->options = options;
    return Result<bool, TraceError>(true);
}

Result<bool, TraceError> TraceSession::finish() {
    if (!impl_->session) {
        return Result<bool, TraceError>(true); // nothing to drain
    }

    impl_->session->StopBlocking();
    std::vector<char> trace = impl_->session->ReadTraceBlocking();
    impl_->session.reset();

    const std::filesystem::path output = impl_->options.output;
    if (output.empty()) {
        return Result<bool, TraceError>(TraceError::kWriteFailed);
    }

    std::ofstream out(output, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        return Result<bool, TraceError>(TraceError::kWriteFailed);
    }
    out.write(trace.data(), static_cast<std::streamsize>(trace.size()));
    if (!out) {
        return Result<bool, TraceError>(TraceError::kWriteFailed);
    }
    return Result<bool, TraceError>(true);
}

#else // !CHRONON3D_ENABLE_TRACING

struct TraceSession::Impl {};

TraceSession::TraceSession() = default;
TraceSession::~TraceSession() = default;

Result<bool, TraceError> TraceSession::start(const TraceOptions&) {
    // Compiled out: no-op success (macro surface is no-op as well).
    return Result<bool, TraceError>(true);
}

Result<bool, TraceError> TraceSession::finish() {
    return Result<bool, TraceError>(true);
}

#endif // CHRONON3D_ENABLE_TRACING

} // namespace chronon3d::trace
