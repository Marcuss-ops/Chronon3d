#pragma once

#include <chronon3d/core/cancellation_token.hpp>
#include <chronon3d/core/types/result.hpp>

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

namespace chronon3d::media::video {

/// One audio input to be mixed by the external FFmpeg mux boundary.
/// The source path must already be resolved by the caller; this module never
/// consults process-wide asset state.
struct MuxAudioTrack {
    std::filesystem::path source;
    double volume{1.0};
    double start_time_offset{0.0};
    double duration_seconds{0.0};
    std::string role;
    bool loop{false};
    double fade_in_seconds{0.0};
    double fade_out_seconds{0.0};
    bool ducking_enabled{false};
};

/// Immutable request for an external audio mux operation.
/// Chronon renders the video; FFmpeg is an explicit post-process boundary.
enum class MuxContainer : unsigned char {
    Mp4,
    Mkv,
    WebM,
};

struct MuxPlan {
    std::filesystem::path video_input;
    std::filesystem::path output;
    std::vector<MuxAudioTrack> tracks;
    MuxContainer container{MuxContainer::Mp4};
    bool overwrite{true};
    std::chrono::milliseconds process_timeout{std::chrono::minutes(5)};
    std::chrono::milliseconds graceful_cancel_timeout{std::chrono::seconds(5)};
};

enum class MuxErrorCode : unsigned char {
    InvalidPlan,
    InputMissing,
    OutputExists,
    FfmpegNotFound,
    FfprobeNotFound,
    Cancelled,
    ProcessFailed,
    Timeout,
    VerificationFailed,
    PublishFailed,
};

struct MuxError {
    MuxErrorCode code{MuxErrorCode::InvalidPlan};
    std::string message;
    int process_exit_code{-1};
};

struct MuxReport {
    std::filesystem::path output;
    std::size_t audio_track_count{0};
    double duration_seconds{0.0};
};

/// Runs the official external audio boundary. No libav audio encoder is used
/// by this class; FFmpeg and ffprobe are separate child processes.
class ExternalAudioMuxer final {
public:
    [[nodiscard]] Result<MuxReport, MuxError> run(
        const MuxPlan& plan,
        CancellationToken* cancellation = nullptr) const;
};

}  // namespace chronon3d::media::video
