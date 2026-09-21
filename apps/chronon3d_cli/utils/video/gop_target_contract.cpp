#include "gop_target_contract.hpp"

#include "../../commands/video/common/pipe_export_helpers.hpp"
#include "../../commands/video/common/video_export_common.hpp"
#include "encoder_config_resolution.hpp"
#include "encoder_bitstream_contract.hpp"

namespace chronon3d::cli {

std::optional<BitstreamTargetContract> resolve_gop_target_contract(
    const FfmpegExportOptions& opts,
    const CompiledComposition& compiled,
    const chronon3d::CpuBudget& cpu_budget) {
    if (opts.gop_copy_only) {
        return make_remux_target_contract(opts.gop_source);
    }

    const auto codec = resolve_cli_ffmpeg_codec(
        opts.encoder.codec, opts.encoder.hardware_encoder);
    const auto pipe_options =
        make_pipe_options(compiled, opts, codec, cpu_budget);
    if (!pipe_options) return std::nullopt;
    return resolve_encoder_bitstream_contract(*pipe_options);
}

} // namespace chronon3d::cli
