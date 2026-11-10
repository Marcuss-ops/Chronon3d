// command_bench_convert.cpp — retired with the legacy Composition executor.
//
// The benchmark rendered an arbitrary registry Composition through
// `SoftwareRenderer::render_compiled`. That path no longer exists: production
// execution accepts only a typed `CompiledProgram` from a RenderPlan.

#include "../../commands.hpp"

#include <spdlog/spdlog.h>

namespace chronon3d::cli {

int command_bench_convert(const CompositionRegistry& registry,
                          const BenchConvertArgs& args) {
    (void)registry;
    (void)args;
    spdlog::error(
        "bench-convert rendered an arbitrary Composition through the legacy "
        "executor, which was removed. Run the conversion benchmark from a "
        "chronon.render-plan.v2 job instead.");
    return 1;
}

} // namespace chronon3d::cli
