#pragma once

#include "video_export_common.hpp"
#include <string>
#include <memory>
#include <string_view>

namespace chronon3d::cli {

/// Aggregated parameters for a single video export run.
/// Lives on the stack — references are valid for the duration of the export.
struct VideoExportJob {
    const CompositionRegistry& registry;
    const Composition& comp;
    std::string composition_id;
    RenderSettings settings;
    Frame start;
    Frame end;
    FfmpegExportOptions opts;
};

/// Abstract interface for a video export strategy.
///
/// Each exporter implements a specific flavour of the frame render → encode
/// pipeline (e.g. pipe subprocess, chunked PNG → ffmpeg, in-process native,
/// EXR sequence …).
class VideoExporter {
public:
    virtual ~VideoExporter() = default;

    /// Machine-readable identifier (e.g. "pipe", "chunked").
    virtual std::string_view id() const = 0;

    /// Human-readable description shown in help / dry-run output.
    virtual std::string_view description() const = 0;

    /// Run the full render + encode pipeline.
    /// Returns 0 on success, non-zero on failure.
    virtual int export_video(const VideoExportJob& job) = 0;
};

// ── Built-in exporter registration helpers ───────────────────────────────────

class ExporterRegistry;

void register_pipe_exporter(ExporterRegistry& registry);
void register_chunked_exporter(ExporterRegistry& registry);

/// Convenience: register all built-in exporters at once.
void register_builtin_exporters(ExporterRegistry& registry);

} // namespace chronon3d::cli
