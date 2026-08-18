#pragma once

#include <chronon3d/runtime/telemetry/render_telemetry_record.hpp>

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

namespace chronon3d::telemetry {

/// One ranked cost on the measured render critical path.
struct BottleneckFinding {
    std::string name;
    double value_ms{0.0};
    double critical_path_share{0.0};
    std::string recommendation;
};

/// Read-only analysis of the canonical job report. It intentionally collects
/// no telemetry and never invents zero-valued measurements: a fallback is
/// used only when the more specific field is unavailable.
inline std::vector<BottleneckFinding> analyze_bottlenecks(
    const RenderTelemetryRecord& run,
    std::size_t limit = 5) {
    const auto preferred = [](double specific, double fallback) {
        return specific > 0.0 ? specific : fallback;
    };

    const double readback = preferred(
        run.gpu_readback_ms,
        preferred(run.phase_gpu_readback_ms,
                  static_cast<double>(run.frame_conversion_copy_wall_ms)));
    const double conversion = preferred(run.video_conversion_wall_ms,
                                        run.chronon_conversion_copy_ms);
    const double gpu = preferred(
        run.gpu_execute_ms,
        preferred(run.phase_gpu_render_ms,
                  static_cast<double>(run.node_execute_actual_wall_ms)));
    const double encoder = preferred(run.phase_encode_ms,
                                     run.ffmpeg_encode_total_ms);
    const double pipe = preferred(run.ffmpeg_pipe_write_wall_ms,
                                  static_cast<double>(run.video_pipe_write_wall_ms));
    const double scene = preferred(run.phase_scene_eval_ms,
                                   static_cast<double>(run.video_graph_eval_wall_ms));

    std::vector<BottleneckFinding> findings{
        {"GPU readback", readback, 0.0,
         "Investigate zero-copy GPU encoder or reduce GPU→CPU transfers."},
        {"Pixel conversion", conversion, 0.0,
         "Benchmark packed vs swscale conversion and remove redundant copies."},
        {"GPU execution", gpu, 0.0,
         "Inspect passes, barriers, pipeline binds and shader workload."},
        {"FFmpeg pipe backpressure", pipe, 0.0,
         "Compare renderer production with encoder consumption and enlarge the queue."},
        {"Encoder", encoder, 0.0,
         "Measure codec settings and hardware/native encoder availability."},
        {"Scene/graph evaluation", scene, 0.0,
         "Cache layout/graph work and inspect node scheduling and dirty evaluation."},
    };

    findings.erase(std::remove_if(findings.begin(), findings.end(),
                                  [](const BottleneckFinding& f) { return f.value_ms <= 0.0; }),
                   findings.end());
    std::sort(findings.begin(), findings.end(),
              [](const BottleneckFinding& a, const BottleneckFinding& b) {
                  return a.value_ms > b.value_ms;
              });

    const double wall = run.wall_time_ms > 0.0 ? run.wall_time_ms : run.e2e_wall_ms;
    if (wall > 0.0) {
        for (auto& finding : findings) {
            finding.critical_path_share = finding.value_ms / wall;
        }
    }
    if (findings.size() > limit) {
        findings.resize(limit);
    }
    return findings;
}

} // namespace chronon3d::telemetry
