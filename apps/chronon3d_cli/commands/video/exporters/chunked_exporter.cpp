#include "../exporter_registry.hpp"

namespace chronon3d::cli {
namespace {

class ChunkedExporter final : public VideoExporter {
public:
    std::string_view id() const override { return "png"; }
    std::string_view description() const override {
        return "Render frames in parallel chunks to PNG, then encode with ffmpeg";
    }

    int export_video(const VideoExportJob& job) override {
        auto result = render_and_encode_ffmpeg_chunked(
            job.registry, job.comp, job.composition_id,
            job.settings, job.start, job.end, job.opts);
        return result.return_code;
    }
};

} // namespace

void register_chunked_exporter(ExporterRegistry& registry) {
    registry.register_exporter(std::make_unique<ChunkedExporter>());
}

} // namespace chronon3d::cli
