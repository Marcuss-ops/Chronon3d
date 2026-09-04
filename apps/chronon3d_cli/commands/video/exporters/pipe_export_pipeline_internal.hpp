#pragma once

#include "../common/pipe_export_pipeline.hpp"
#include "../common/pipe_export_helpers.hpp"

#include <memory>
#include <vector>

namespace chronon3d::cli::detail {

[[nodiscard]] media::VideoExecutionPlan render_graph_fallback_plan(
    const FfmpegExportOptions& opts);

class VariantFanoutEncoder final : public IVideoEncoder {
public:
    struct Child {
        std::unique_ptr<IVideoEncoder> encoder;
        FfmpegPipeOptions options;
    };

    explicit VariantFanoutEncoder(std::vector<Child> children);

    bool open(const FfmpegPipeOptions&) override;
    void set_counters(RenderCounters* counters) override;
    bool write_frame(const Framebuffer& framebuffer) override;
    bool write_frame_async(const Framebuffer& framebuffer,
                           std::shared_ptr<Framebuffer> owner) override;
    bool close() override;
    [[nodiscard]] std::uint64_t frames_written() const override;
    [[nodiscard]] EncoderFrameTelemetry last_frame_telemetry() const override;

private:
    std::vector<Child> children_;
    RenderCounters* counters_{nullptr};
};

} // namespace chronon3d::cli::detail
