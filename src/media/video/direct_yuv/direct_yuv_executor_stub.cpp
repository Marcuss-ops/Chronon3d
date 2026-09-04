#include <chronon3d/media/video/direct_yuv_executor.hpp>

namespace chronon3d::media::video {

std::shared_ptr<DirectYuvExecutor> DirectYuvExecutor::prepare(
    const CompiledComposition&,
    ImageCache&,
    std::shared_ptr<::chronon3d::media::VideoDeviceRuntime>,
    std::string& reason) {
    reason = "DirectYUV requires native FFmpeg support";
    return nullptr;
}

DirectYuvExecutionResult DirectYuvExecutor::run(
    ::chronon3d::media::NativeVideoFrameDecoder&,
    const DirectYuvExecutionRequest& request) const {
    DirectYuvExecutionResult result;
    result.status = DirectYuvExecutionStatus::ExecutionFailed;
    result.terminal_frame = request.start;
    result.error = "DirectYUV requires native FFmpeg support";
    return result;
}

const std::string& DirectYuvExecutor::video_path() const noexcept {
    static const std::string empty;
    return empty;
}

double DirectYuvExecutor::scene_eval_ms() const noexcept { return 0.0; }
double DirectYuvExecutor::watermark_load_ms() const noexcept { return 0.0; }
double DirectYuvExecutor::watermark_upload_ms() const noexcept { return 0.0; }

} // namespace chronon3d::media::video
