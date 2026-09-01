namespace {

[[nodiscard]] media::VideoExecutionPlan render_graph_fallback_plan(
    const FfmpegExportOptions& opts) {
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

class VariantFanoutEncoder final : public IVideoEncoder {
public:
    struct Child {
        std::unique_ptr<IVideoEncoder> encoder;
        FfmpegPipeOptions options;
    };

    explicit VariantFanoutEncoder(std::vector<Child> children)
        : children_(std::move(children)) {}

    bool open(const FfmpegPipeOptions&) override {
        for (auto& child : children_) {
            if (!child.encoder->open(child.options)) return false;
        }
        return !children_.empty();
    }

    void set_counters(RenderCounters* counters) override {
        counters_ = counters;
        for (auto& child : children_) child.encoder->set_counters(counters);
    }

    bool write_frame(const Framebuffer& framebuffer) override {
        for (auto& child : children_) {
            if (!child.encoder->write_frame(framebuffer)) return false;
        }
        if (counters_) {
            counters_->simo_variant_submits.fetch_add(
                children_.size(), std::memory_order_relaxed);
        }
        return true;
    }

    bool write_frame_async(const Framebuffer& framebuffer,
                           std::shared_ptr<Framebuffer> owner) override {
        for (auto& child : children_) {
            if (!child.encoder->write_frame_async(framebuffer, owner)) return false;
        }
        if (counters_) {
            counters_->simo_variant_submits.fetch_add(
                children_.size(), std::memory_order_relaxed);
        }
        return true;
    }

    bool close() override {
        bool ok = true;
        for (auto& child : children_) ok = child.encoder->close() && ok;
        return ok;
    }

    [[nodiscard]] std::uint64_t frames_written() const override {
        if (children_.empty()) return 0;
        auto frames = children_.front().encoder->frames_written();
        for (const auto& child : children_) {
            frames = std::min(frames, child.encoder->frames_written());
        }
        return frames;
    }

    [[nodiscard]] EncoderFrameTelemetry last_frame_telemetry() const override {
        return children_.empty() ? EncoderFrameTelemetry{}
                                 : children_.front().encoder->last_frame_telemetry();
    }

private:
    std::vector<Child> children_;
    RenderCounters* counters_{nullptr};
};

} // namespace
