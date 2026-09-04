int command_benchmark_saturation(const CompositionRegistry& registry, const CliContext& ctx,
                                  const std::string& scene, int duration_sec,
                                  const std::string& report_json,
                                  MotionBlurMode motion_blur_mode,
                                  int motion_blur_samples,
                                  graph::BackendPreference backend) {
#include "command_benchmark_saturation_measure.inc"
#include "command_benchmark_saturation_report.inc"
#include "command_benchmark_saturation_json.inc"
#include "command_benchmark_saturation_finish.inc"
}
