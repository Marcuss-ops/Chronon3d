// command_inspect_text.cpp — retired with the legacy Composition executor.
//
// The command needed a rendered frame from `render_compiled(Composition)` to
// populate the text audit snapshots. Production execution now accepts only a
// typed `CompiledProgram`; the command must be rebuilt on a render-plan input
// before it can audit again.

#include "command_inspect_text.hpp"

#include <fmt/format.h>
#include <nlohmann/json.hpp>

namespace chronon3d::cli {

int command_inspect_text(const CompositionRegistry& registry,
                         const InspectTextArgs& args) {
    (void)registry;
    nlohmann::json js{
        {"error", "composition_executor_removed"},
        {"composition", args.comp_id},
        {"frame", args.frame.integral()},
        {"status", "FAIL"},
        {"message",
         "The legacy Composition executor was removed; inspect text through a "
         "chronon.render-plan.v2 render job."},
    };
    fmt::print("{}\n", js.dump(2));
    return 1;
}

} // namespace chronon3d::cli
