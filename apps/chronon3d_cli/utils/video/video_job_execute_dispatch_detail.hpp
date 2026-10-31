#pragma once

#include "video_export_support.hpp"
#include "gop_smart_copy.hpp"

#include <optional>

namespace chronon3d::cli::detail {

media::GopAnalysisSummary inspect_source_once(
    const FfmpegExportOptions&, const compiler::CompiledProgram&,
    double start_sec, double end_sec, const chronon3d::CpuBudget&);

std::optional<int> execute_packet_copy_lane(
    const FfmpegExportOptions&, double start_sec, double end_sec);

bool execute_direct_mux_gop_plan(
    const CompositionRegistry&, const compiler::CompiledProgram&,
    const std::string&, const RenderSettings&, Frame, Frame,
    const FfmpegExportOptions&, const chronon3d::CpuBudget&,
    const std::shared_ptr<media::VideoJobExecutionContext>&,
    std::string& reason);

} // namespace chronon3d::cli::detail
