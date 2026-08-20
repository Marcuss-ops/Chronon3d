// ResourcePreparation media probes.
// Kept separate from the phase aggregator so FFmpeg lifecycle and metadata
// extraction remain one focused implementation unit.

#include <chronon3d/runtime/resource_preparation.hpp>

#include <utility>

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/error.h>
}
#endif

namespace chronon3d::runtime {

namespace {

PreparationError media_make_error(PreparationError::Code code,
                             std::string message,
                             const assets::InternalAssetRef* ref,
                             const char* phase) {
    PreparationError error;
    error.code = code;
    error.message = std::move(message);
    error.phase = phase ? phase : "";
    if (ref) {
        error.path = ref->path;
        error.owner = ref->owner;
    }
    return error;
}

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
std::string ffmpeg_error(int error_code) {
    char buffer[AV_ERROR_MAX_STRING_SIZE]{};
    av_strerror(error_code, buffer, sizeof(buffer));
    return buffer;
}

Result<AVFormatContext*, PreparationError> open_media(
    const assets::InternalAssetRef& ref,
    const assets::AssetResolver& resolver,
    const char* phase) {
    if (ref.path.empty()) {
        return media_make_error(PreparationError::Code::UnresolvableAssetPath,
                          "empty asset path", &ref, phase);
    }
    const auto resolved = resolver.resolve(ref.path);
    if (!resolved.has_value()) {
        return media_make_error(PreparationError::Code::MissingAsset,
                          "asset not found: " + ref.path, &ref, phase);
    }

    AVFormatContext* context = nullptr;
    const std::string path = resolved->string();
    const int open_result = avformat_open_input(&context, path.c_str(), nullptr, nullptr);
    if (open_result < 0) {
        return media_make_error(PreparationError::Code::CorruptedAsset,
                          "media container could not be opened: " +
                              ffmpeg_error(open_result), &ref, phase);
    }
    const int stream_result = avformat_find_stream_info(context, nullptr);
    if (stream_result < 0) {
        avformat_close_input(&context);
        return media_make_error(PreparationError::Code::CorruptedAsset,
                          "media stream metadata could not be read: " +
                              ffmpeg_error(stream_result), &ref, phase);
    }
    return context;
}
#endif

} // namespace

Result<PreparedVideoMetadata, PreparationError>
ResourcePreparation::probe_video_metadata(
    const assets::InternalAssetRef& ref,
    const assets::AssetResolver& resolver) {
#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
    auto opened = open_media(ref, resolver, "video");
    if (!opened.has_value()) return opened.error();

    AVFormatContext* context = opened.value();
    const int stream_index = av_find_best_stream(
        context, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (stream_index < 0) {
        avformat_close_input(&context);
        return media_make_error(PreparationError::Code::CorruptedAsset,
                          "media contains no video stream", &ref, "video");
    }

    AVStream* stream = context->streams[stream_index];
    const AVCodecParameters* codec = stream->codecpar;
    const AVRational rate = av_guess_frame_rate(context, stream, nullptr);
    const double fps = rate.den != 0 ? av_q2d(rate) : 0.0;
    const std::int64_t frame_count = stream->nb_frames > 0 ? stream->nb_frames : 0;
    PreparedVideoMetadata metadata{
        .path = ref.path, .owner = ref.owner,
        .width = codec->width, .height = codec->height,
        .fps = static_cast<float>(fps), .frame_count = frame_count};
    avformat_close_input(&context);
    if (metadata.width <= 0 || metadata.height <= 0 || metadata.fps <= 0.0f) {
        return media_make_error(PreparationError::Code::CorruptedAsset,
                          "video stream has invalid metadata", &ref, "video");
    }
    return metadata;
#else
    if (ref.path.empty()) {
        return media_make_error(PreparationError::Code::UnresolvableAssetPath,
                          "empty asset path", &ref, "video");
    }
    if (!resolver.resolve(ref.path).has_value()) {
        return media_make_error(PreparationError::Code::MissingAsset,
                          "asset not found: " + ref.path, &ref, "video");
    }
    return media_make_error(PreparationError::Code::InternalError,
                      "video preparation requires CHRONON3D_ENABLE_NATIVE_FFMPEG",
                      &ref, "video");
#endif
}

Result<PreparedAudioIndex, PreparationError>
ResourcePreparation::build_audio_index(
    const assets::InternalAssetRef& ref,
    const assets::AssetResolver& resolver) {
#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
    auto opened = open_media(ref, resolver, "audio");
    if (!opened.has_value()) return opened.error();

    AVFormatContext* context = opened.value();
    const int stream_index = av_find_best_stream(
        context, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (stream_index < 0) {
        avformat_close_input(&context);
        return media_make_error(PreparationError::Code::CorruptedAsset,
                          "media contains no audio stream", &ref, "audio");
    }

    const AVStream* stream = context->streams[stream_index];
    const AVCodecParameters* codec = stream->codecpar;
    double duration = 0.0;
    if (stream->duration != AV_NOPTS_VALUE) {
        duration = static_cast<double>(stream->duration) * av_q2d(stream->time_base);
    } else if (context->duration != AV_NOPTS_VALUE) {
        duration = static_cast<double>(context->duration) / AV_TIME_BASE;
    }
    PreparedAudioIndex metadata{
        .path = ref.path, .owner = ref.owner,
        .duration_seconds = static_cast<float>(duration),
        .sample_rate = codec->sample_rate};
    avformat_close_input(&context);
    if (metadata.duration_seconds <= 0.0f || metadata.sample_rate <= 0) {
        return media_make_error(PreparationError::Code::CorruptedAsset,
                          "audio stream has invalid metadata", &ref, "audio");
    }
    return metadata;
#else
    if (ref.path.empty()) {
        return media_make_error(PreparationError::Code::UnresolvableAssetPath,
                          "empty asset path", &ref, "audio");
    }
    if (!resolver.resolve(ref.path).has_value()) {
        return media_make_error(PreparationError::Code::MissingAsset,
                          "asset not found: " + ref.path, &ref, "audio");
    }
    return media_make_error(PreparationError::Code::InternalError,
                      "audio preparation requires CHRONON3D_ENABLE_NATIVE_FFMPEG",
                      &ref, "audio");
#endif
}

} // namespace chronon3d::runtime
