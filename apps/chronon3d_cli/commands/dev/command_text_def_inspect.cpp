// command_text_def_inspect.cpp — retired with the legacy Composition executor.
//
// The command needed a rendered frame from `render_compiled(Composition)` to
// populate the text audit snapshots. Production execution now accepts only a
// typed `CompiledProgram`; the command must be rebuilt on a render-plan input
// before it can audit again.

#include "../../commands.hpp"

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <fstream>

namespace chronon3d::cli {

int command_text_def_inspect(const CompositionRegistry& registry,
                             const TextDefInspectArgs& args) {
    (void)registry;
    nlohmann::json js{
        {"error", "composition_executor_removed"},
        {"composition", args.comp_id},
        {"frame", static_cast<int>(args.frame)},
        {"status", "FAIL"},
        {"message",
         "The legacy Composition executor was removed; inspect text through a "
         "chronon.render-plan.v2 render job."},
    };
    if (!args.json_output.empty()) {
        std::ofstream out(args.json_output);
        if (out) out << js.dump(2) << "\n";
    } else {
        fmt::print("{}\n", js.dump(2));
    }
    return 1;
}

} // namespace chronon3d::cli
