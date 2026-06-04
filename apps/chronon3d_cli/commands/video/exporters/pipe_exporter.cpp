#include "../exporter_registry.hpp"

namespace chronon3d::cli {
namespace {

class PipeExporter final : public VideoExporter {
public:
    std::string_view id() const override { return "pipe"; }
    std::string_view description() const override {
        return "Render frames and stream to ffmpeg subprocess via pipe";
    }

    int export_video(const VideoExportJob& job) override {
        return render_and_encode_ffmpeg_pipe(
            job.registry, job.comp, job.composition_id,
            job.settings, job.start, job.end, job.opts);
    }
};

} // namespace

void register_pipe_exporter(ExporterRegistry& registry) {
    registry.register_exporter(std::make_unique<PipeExporter>());
}

} // namespace chronon3d::cli
