#pragma once
#include <doctest/doctest.h>
#include <chronon3d/core/gpu_hot_path_mode.hpp>
#include <chronon3d/media/video/video_execution_resolver.hpp>
#include <chronon3d/media/video/output_contract.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/timeline/render_job.hpp>
#include <chronon3d/verification/render_receipt.hpp>
#include <array>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

#include <nlohmann/json.hpp>

#include "commands/video/exporters/pipe_timing_sidecar.hpp"
#include "commands/video/common/pipe_export_pipeline.hpp"
#include "utils/job/render_job.hpp"
#include "utils/video/media_contract_from_probe.hpp"
#include "utils/video/video_export_support.hpp"

#include <chronon3d/media/video/packet_assembler.hpp>
#include <chronon3d/media/video/gop_execution_executor.hpp>
#include "utils/video/gop_smart_copy.hpp"
#include "utils/video/encoder_bitstream_contract.hpp"
#include "utils/video/variant_batch.hpp"

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
#include "utils/video/native_av_encoder.hpp"
#include "utils/video/ffmpeg_profile_constants.hpp"
#endif

using chronon3d::media::AudioExecutionPath;
using chronon3d::media::resolve_audio_execution;
using chronon3d::media::resolve_video_execution;
using chronon3d::media::resolve_video_execution_path;
using chronon3d::media::resolve_video_execution_path_from_gop_analysis;
using chronon3d::media::VideoExecutionPath;

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
namespace {
chronon3d::cli::FfmpegPipeOptions p0_5a_pipe_options(const std::string& output) {
    chronon3d::cli::FfmpegPipeOptions options;
    options.width = 64;
    options.height = 64;
    options.fps_num = 10;
    options.fps_den = 1;
    options.codec = "libx264";
    options.hardware_encoder = "none";
    options.preset = "ultrafast";
    options.rate_control_mode = "crf";
    options.rate_control_mode_explicit = true;
    options.crf = 23;
    options.crf_explicit = true;
    options.output_path = output;
    return options;
}

} // namespace
#endif // CHRONON3D_ENABLE_NATIVE_FFMPEG
