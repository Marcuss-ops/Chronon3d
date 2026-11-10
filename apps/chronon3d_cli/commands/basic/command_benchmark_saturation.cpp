// command_benchmark_saturation — `chronon benchmark --saturation`
//
// This benchmark rendered an arbitrary registry Composition through the legacy
// CompiledComposition executor, which has been removed. It must be rebuilt on
// top of a chronon.render-plan.v2 input before it can measure the typed path.

#include "../../commands.hpp"

#include <spdlog/spdlog.h>

namespace chronon3d::cli {

int command_benchmark_saturation(const CompositionRegistry& registry,
                                 const CliContext& ctx,
                                 const std::string& scene, int duration_sec,
                                 const std::string& report_json,
                                 MotionBlurMode motion_blur_mode,
                                 int motion_blur_samples,
                                 graph::BackendPreference backend) {
    (void)registry;
    (void)ctx;
    (void)scene;
    (void)duration_sec;
    (void)report_json;
    (void)motion_blur_mode;
    (void)motion_blur_samples;
    (void)backend;
    spdlog::error(
        "The saturation benchmark rendered arbitrary Compositions through the "
        "legacy executor; that path was removed. Re-run the benchmark from a "
        "chronon.render-plan.v2 job.");
    return 1;
}

} // namespace chronon3d::cli
