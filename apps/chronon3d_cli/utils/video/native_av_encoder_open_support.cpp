#include "native_av_encoder.hpp"
#include "native_av_encoder_internal.hpp"
#include "ffmpeg_profile_constants.hpp"

#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/media/video/keyframe_cadence.hpp>
#include <fmt/format.h>
#include <libavutil/mastering_display_metadata.h>
#include <libavutil/mathematics.h>
#include <libavcodec/version.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <optional>

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
#include <cuda.h>
#include <libavutil/hwcontext_cuda.h>
#ifdef CHRONON3D_ENABLE_VULKAN
#include <chronon3d/media/video/compositor/cuda/cuda_nv12_compositor.hpp>
#endif
#endif

namespace chronon3d::cli::detail {

runtime::TransferFunction transfer_from_output_space(
    color::ColorSpace output) noexcept {
    switch (output) {
        case color::ColorSpace::LinearSRGB:
        case color::ColorSpace::ACEScg:
            return runtime::TransferFunction::Linear;
        case color::ColorSpace::SRGB:
            return runtime::TransferFunction::Srgb;
        case color::ColorSpace::PQ:
            return runtime::TransferFunction::Pq;
        case color::ColorSpace::HLG:
            return runtime::TransferFunction::Hlg;
        case color::ColorSpace::LinearBT2020:
            return runtime::TransferFunction::Linear;
        case color::ColorSpace::Rec709:
        default:
            return runtime::TransferFunction::Srgb;
    }
}

bool set_codec_option_checked(AVCodecContext* codec, const char* key,
                              const std::string& value) {
    const int rc = av_opt_set(codec, key, value.c_str(), AV_OPT_SEARCH_CHILDREN);
    if (rc < 0) {
        char error[AV_ERROR_MAX_STRING_SIZE]{};
        av_strerror(rc, error, sizeof(error));
        spdlog::error("[native_av] unsupported encoder option {}='{}': {}",
                      key, value, error);
        return false;
    }
    return true;
}

bool configure_hdr_static_metadata(
    AVCodecContext* codec, const color::HdrMasteringMetadata& metadata) {
#if LIBAVCODEC_VERSION_MAJOR < 61
    // Older FFmpeg releases do not expose encoder context side data APIs.
    // Keep SDR/software-pipe builds usable and reject explicit HDR metadata.
    (void)codec;
    (void)metadata;
    return false;
#else
    auto* mastering_side_data = av_frame_side_data_new(
        &codec->decoded_side_data, &codec->nb_decoded_side_data,
        AV_FRAME_DATA_MASTERING_DISPLAY_METADATA,
        sizeof(AVMasteringDisplayMetadata), AV_FRAME_SIDE_DATA_FLAG_UNIQUE);
    if (!mastering_side_data) return false;
    auto* mastering = reinterpret_cast<AVMasteringDisplayMetadata*>(
        mastering_side_data->data);
    std::memset(mastering, 0, sizeof(*mastering));
    for (int primary = 0; primary < 3; ++primary) {
        mastering->display_primaries[primary][0] =
            av_d2q(metadata.display_primaries[primary][0], 1'000'000);
        mastering->display_primaries[primary][1] =
            av_d2q(metadata.display_primaries[primary][1], 1'000'000);
    }
    mastering->white_point[0] = av_d2q(metadata.white_point[0], 1'000'000);
    mastering->white_point[1] = av_d2q(metadata.white_point[1], 1'000'000);
    mastering->min_luminance = av_d2q(metadata.min_luminance_nits, 1'000'000);
    mastering->max_luminance = av_d2q(metadata.max_luminance_nits, 1'000'000);
    mastering->has_primaries = 1;
    mastering->has_luminance = 1;
    if (metadata.content_light) {
        auto* light_side_data = av_frame_side_data_new(
            &codec->decoded_side_data, &codec->nb_decoded_side_data,
            AV_FRAME_DATA_CONTENT_LIGHT_LEVEL,
            sizeof(AVContentLightMetadata), AV_FRAME_SIDE_DATA_FLAG_UNIQUE);
        if (!light_side_data) return false;
        auto* light = reinterpret_cast<AVContentLightMetadata*>(
            light_side_data->data);
        std::memset(light, 0, sizeof(*light));
        light->MaxCLL = metadata.content_light->max_cll_nits;
        light->MaxFALL = metadata.content_light->max_fall_nits;
    }
    return true;
#endif
}

bool validate_encoder_options(const FfmpegPipeOptions& options) {
    if (options.width <= 0 || options.height <= 0 ||
        options.canonical_fps_num() <= 0 || options.canonical_fps_den() <= 0 ||
        (options.output_path.empty() && options.output_segments.empty())) {
        return false;
    }
    if (options.hdr_mastering_metadata &&
        ((options.color_transform.output != color::ColorSpace::PQ &&
          options.color_transform.output != color::ColorSpace::HLG) ||
         options.hardware_encoder != "none")) {
        spdlog::error(
            "[native_av] explicit HDR mastering metadata requires CPU native PQ/HLG encoding");
        return false;
    }
    // LinearBT2020 needs a float media stream that this encoder does not
    // provide. PQ and HLG are emitted as 10-bit YUV420 on the CPU lane.
    switch (options.color_transform.output) {
        case color::ColorSpace::ACEScg:
        case color::ColorSpace::LinearBT2020:
            return false;
        default:
            return true;
    }
}

} // namespace chronon3d::cli::detail
