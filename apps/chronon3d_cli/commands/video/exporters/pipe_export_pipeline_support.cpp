#include "pipe_export_pipeline_internal.hpp"

#include <algorithm>
#include <atomic>
#include <utility>

namespace chronon3d::cli::detail {

media::VideoExecutionPlan render_graph_fallback_plan(const FfmpegExportOptions& opts) {
    const bool native_gpu_graph =
        opts.backend_preference == graph::BackendPreference::GPU &&
        opts.encoder.encoder_backend == "native" &&
        opts.encoder.hardware_encoder == "nvenc";
    if (native_gpu_graph) {
        return {
            media::DecodePath::Nvdec,
            media::CompositePath::VulkanGraph,
            media::EncodePath::Nvenc,
            media::InteropPath::VulkanCuda,
            media::SurfaceHandoffPath::VulkanCopy};
    }
    return {
        media::DecodePath::Software,
        media::CompositePath::SoftwareGraph,
        media::EncodePath::Pipe,
        media::InteropPath::None,
        media::SurfaceHandoffPath::HostUpload};
}

VariantFanoutEncoder::VariantFanoutEncoder(std::vector<Child> children)
    : children_(std::move(children)) {}

bool VariantFanoutEncoder::open(const FfmpegPipeOptions&) {
    for (auto& child : children_) {
        if (!child.encoder->open(child.options)) return false;
    }
    return !children_.empty();
}

void VariantFanoutEncoder::set_counters(RenderCounters* counters) {
    counters_ = counters;
    for (auto& child : children_) child.encoder->set_counters(counters);
}

bool VariantFanoutEncoder::write_frame(const Framebuffer& framebuffer) {
    for (auto& child : children_) {
        if (!child.encoder->write_frame(framebuffer)) return false;
    }
    if (counters_) {
        counters_->simo_variant_submits.fetch_add(children_.size(), std::memory_order_relaxed);
    }
    return true;
}

bool VariantFanoutEncoder::write_frame_async(
    const Framebuffer& framebuffer,
    std::shared_ptr<Framebuffer> owner) {
    for (auto& child : children_) {
        if (!child.encoder->write_frame_async(framebuffer, owner)) return false;
    }
    if (counters_) {
        counters_->simo_variant_submits.fetch_add(children_.size(), std::memory_order_relaxed);
    }
    return true;
}

bool VariantFanoutEncoder::close() {
    bool ok = true;
    for (auto& child : children_) ok = child.encoder->close() && ok;
    return ok;
}

std::uint64_t VariantFanoutEncoder::frames_written() const {
    if (children_.empty()) return 0;
    auto frames = children_.front().encoder->frames_written();
    for (const auto& child : children_) {
        frames = std::min(frames, child.encoder->frames_written());
    }
    return frames;
}

EncoderFrameTelemetry VariantFanoutEncoder::last_frame_telemetry() const {
    return children_.empty() ? EncoderFrameTelemetry{}
                             : children_.front().encoder->last_frame_telemetry();
}

} // namespace chronon3d::cli::detail
