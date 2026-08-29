#pragma once

#include "../../../utils/video/direct_yuv_frame.hpp"

#include <chronon3d/timeline/compiled_composition.hpp>

#include <memory>
#include <string>

namespace chronon3d {
class ImageCache;
namespace media { class NativeVideoFrameDecoder; }
}

namespace chronon3d::cli {

/// Resolver-owned, precompiled program for the deliberately small direct-YUV
/// subset.  It is not a second scene renderer: eligibility is derived from
/// the canonical compiled composition, assets are loaded through the runtime
/// ImageCache, and execution only supplies decoded NV12 frames to the shared
/// encoder contract.
class DirectYuvProgram final {
public:
    static std::shared_ptr<DirectYuvProgram> prepare(
        const CompiledComposition& compiled,
        ImageCache& image_cache,
        void* cuda_context,
        std::string& reason);

    [[nodiscard]] std::shared_ptr<DirectYuvFrame> execute(
        media::NativeVideoFrameDecoder& decoder,
        Frame frame) const;

    [[nodiscard]] const std::string& video_path() const noexcept { return video_path_; }
    [[nodiscard]] double scene_eval_ms() const noexcept { return scene_eval_ms_; }
    [[nodiscard]] double watermark_load_ms() const noexcept { return watermark_load_ms_; }
    [[nodiscard]] double watermark_upload_ms() const noexcept { return watermark_upload_ms_; }

private:
    DirectYuvProgram() = default;
    std::string video_path_;
    int width_{0};
    int height_{0};
    std::shared_ptr<DirectYuvFrame> template_frame_;
    double scene_eval_ms_{0.0};
    double watermark_load_ms_{0.0};
    double watermark_upload_ms_{0.0};
};

} // namespace chronon3d::cli
