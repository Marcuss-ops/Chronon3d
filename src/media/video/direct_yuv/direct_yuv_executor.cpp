#include <chronon3d/media/video/direct_yuv_executor.hpp>

#include "direct_yuv_program.hpp"

#include <chronon3d/media/video/native_video_frame_decoder.hpp>
#include <chronon3d/media/video/video_device_runtime.hpp>

#include <chrono>
#include <exception>
#include <utility>

namespace chronon3d::media::video {
namespace {

using Clock = std::chrono::steady_clock;

double elapsed_ms(const Clock::time_point& start, const Clock::time_point& end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
}

} // namespace

std::shared_ptr<DirectYuvExecutor> DirectYuvExecutor::prepare(
    const CompiledComposition& compiled,
    ImageCache& image_cache,
    std::shared_ptr<::chronon3d::media::VideoDeviceRuntime> video_runtime,
    std::string& reason) {
    auto program = DirectYuvProgram::prepare(
        compiled, image_cache, std::move(video_runtime), reason);
    if (!program) return nullptr;
    return std::shared_ptr<DirectYuvExecutor>(
        new DirectYuvExecutor(std::move(program)));
}

DirectYuvExecutionResult DirectYuvExecutor::run(
    ::chronon3d::media::NativeVideoFrameDecoder& decoder,
    const DirectYuvExecutionRequest& request) const {
    DirectYuvExecutionResult result;
    result.terminal_frame = request.start;
    const int total = static_cast<int>((request.end - request.start).integral());
    if (total > 0) result.timings.reserve(static_cast<std::size_t>(total));
    const auto loop_start = Clock::now();

    if (!program_) {
        result.error = "DirectYUV executor has no prepared program";
        result.status = DirectYuvExecutionStatus::ExecutionFailed;
        return result;
    }
    if (!request.submit) {
        result.error = "DirectYUV executor requires a submit callback";
        result.status = DirectYuvExecutionStatus::SubmitFailed;
        return result;
    }

    try {
        for (Frame frame = request.start; frame < request.end; ++frame) {
            result.terminal_frame = frame;
            if (request.cancellation_token && request.cancellation_token->is_cancelled()) {
                result.status = DirectYuvExecutionStatus::Cancelled;
                result.wall_ms = elapsed_ms(loop_start, Clock::now());
                return result;
            }
            if (request.consumer_failed && request.consumer_failed()) {
                result.status = DirectYuvExecutionStatus::ConsumerFailed;
                result.wall_ms = elapsed_ms(loop_start, Clock::now());
                return result;
            }

            DirectYuvFrameTiming timing;
            timing.frame = frame;
            const auto execute_start = Clock::now();
            timing.wall_start_ms = elapsed_ms(loop_start, execute_start);
            auto direct_frame = program_->execute(decoder, frame);
            const auto execute_end = Clock::now();
            timing.execute_ms = elapsed_ms(execute_start, execute_end);
            result.execute_ms += timing.execute_ms;
            ++result.frames_rendered;

            if (!direct_frame) {
                result.error = "DirectYUV frame execution failed";
                result.status = DirectYuvExecutionStatus::ExecutionFailed;
                result.wall_ms = elapsed_ms(loop_start, Clock::now());
                return result;
            }

            const auto submit_start = Clock::now();
            const bool accepted = request.submit(frame, std::move(direct_frame));
            const auto submit_end = Clock::now();
            timing.submit_wait_ms = elapsed_ms(submit_start, submit_end);
            result.submit_wait_ms += timing.submit_wait_ms;

            if (!accepted) {
                if (request.consumer_failed && request.consumer_failed()) {
                    result.status = DirectYuvExecutionStatus::ConsumerFailed;
                } else if (request.cancellation_token &&
                           request.cancellation_token->is_cancelled()) {
                    result.status = DirectYuvExecutionStatus::Cancelled;
                } else {
                    result.status = DirectYuvExecutionStatus::SubmitFailed;
                    result.error = "DirectYUV submit callback rejected frame";
                }
                result.wall_ms = elapsed_ms(loop_start, Clock::now());
                return result;
            }

            ++result.frames_submitted;
            result.timings.push_back(timing);
            const int done = static_cast<int>((frame - request.start).integral() + 1);
            if (request.progress) request.progress(done, total);
        }
    } catch (const std::exception& error) {
        result.status = DirectYuvExecutionStatus::Exception;
        result.error = error.what();
        result.wall_ms = elapsed_ms(loop_start, Clock::now());
        return result;
    } catch (...) {
        result.status = DirectYuvExecutionStatus::Exception;
        result.error = "unknown DirectYUV execution exception";
        result.wall_ms = elapsed_ms(loop_start, Clock::now());
        return result;
    }

    result.terminal_frame = request.end;
    result.status = DirectYuvExecutionStatus::Success;
    result.wall_ms = elapsed_ms(loop_start, Clock::now());
    return result;
}

const std::string& DirectYuvExecutor::video_path() const noexcept {
    return program_->video_path();
}

double DirectYuvExecutor::scene_eval_ms() const noexcept {
    return program_ ? program_->scene_eval_ms() : 0.0;
}

double DirectYuvExecutor::watermark_load_ms() const noexcept {
    return program_ ? program_->watermark_load_ms() : 0.0;
}

double DirectYuvExecutor::watermark_upload_ms() const noexcept {
    return program_ ? program_->watermark_upload_ms() : 0.0;
}

} // namespace chronon3d::media::video
