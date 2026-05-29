// ==============================================================================
// CLI Group: Bench
// Contains: bench
// Depends: core
// ==============================================================================
#include "../command_registry.hpp"
#include "../commands.hpp"

namespace chronon3d::cli::group_bench {

#ifdef CHRONON3D_BUILD_BENCHMARKS
void register_commands(CLI::App& app, CliContext& ctx) {
    register_bench_commands(app, ctx);
}
#endif

} // namespace chronon3d::cli::group_bench