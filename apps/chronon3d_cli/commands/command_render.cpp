#include "../commands.hpp"
#include "../utils/render_job.hpp"
#include <spdlog/spdlog.h>

namespace chronon3d {
namespace cli {

int command_render(const CompositionRegistry& registry, const RenderArgs& args) {
    auto plan = plan_render_job(registry, args);
    if (!plan) {
        return 1;
    }
    return execute_render_job(registry, *plan) ? 0 : 1;
}

} // namespace cli
} // namespace chronon3d
