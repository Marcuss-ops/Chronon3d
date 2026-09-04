#pragma once

#include <chronon3d/core/cancellation_token.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/media/video/direct_yuv_frame.hpp>
#include <chronon3d/timeline/compiled_composition.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace chronon3d {
class ImageCache;
}

namespace chronon3d::media {
class NativeVideoFrameDecoder;
class VideoDeviceRuntime;
}

namespace chronon3d::media::video {

class DirectYuvProgram;

enum class DirectYuvExecutionStatus : std::uint8_t {
    Success,
    Cancelled,
    ConsumerFailed,
    ExecutionFailed,
    SubmitFailed,
    Exception,
};

struct DirectYuvFrameTiming {
    Frame frame{0};
    double wall_start_ms{0.0};
    double execute_ms{0.0};
    double submit_wait_ms{0.0};
};

struct DirectYuvExecutionRequest {
    Frame start{0};
    Frame end{0};
    const CancellationToken* cancellation_token{nullptr};
    std::function<bool()> consumer_failed;
    std::function<bool(Frame, std::shared_ptr<DirectYuvFrame>)> submit;
    std::function<void(int, int)> progress;
};

struct DirectYuvExecutionResult {
    DirectYuvExecutionStatus status{DirectYuvExecutionStatus::ExecutionFailed};
    Frame terminal_frame{0};
    int frames_rendered{0};
    int frames_submitted{0};
    double execute_ms{0.0};
    double submit_wait_ms{0.0};
    double wall_ms{0.0};
    std::string error;
    std::vector<DirectYuvFrameTiming> timings;
};

/// Media-owned DirectYUV execution facade. The private DirectYuvProgram is
/// compiled once in prepare(); CLI/export types never cross this boundary.
class DirectYuvExecutor final {
public:
    static std::shared_ptr<DirectYuvExecutor> prepare(
        const CompiledComposition& compiled,
        ImageCache& image_cache,
        std::shared_ptr<::chronon3d::media::VideoDeviceRuntime> video_runtime,
        std::string& reason);

    [[nodiscard]] DirectYuvExecutionResult run(
        ::chronon3d::media::NativeVideoFrameDecoder& decoder,
        const DirectYuvExecutionRequest& request) const;

    [[nodiscard]] const std::string& video_path() const noexcept;
    [[nodiscard]] double scene_eval_ms() const noexcept;
    [[nodiscard]] double watermark_load_ms() const noexcept;
    [[nodiscard]] double watermark_upload_ms() const noexcept;

private:
    explicit DirectYuvExecutor(std::shared_ptr<DirectYuvProgram> program)
        : program_(std::move(program)) {}

    std::shared_ptr<DirectYuvProgram> program_;
};

} // namespace chronon3d::media::video
