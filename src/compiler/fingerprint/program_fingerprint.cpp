#include "../compiler_stages.hpp"

namespace chronon3d::compiler::stages {
std::uint64_t fingerprint(const render_plan::RenderPlan& plan, const CompiledProgram& program) {
    std::uint64_t value = plan.content_fingerprint;
    value ^= static_cast<std::uint64_t>(program.nodes.size()) * 0x9e3779b97f4a7c15ULL;
    value ^= static_cast<std::uint64_t>(program.animations.size()) +
             (static_cast<std::uint64_t>(program.passes.size()) << 32U);
    return value;
}
}
