#include "../compiler_stages.hpp"

namespace chronon3d::compiler::stages {
std::uint64_t fingerprint(const render_plan::RenderPlan& plan, const CompiledProgramData& program) {
    std::uint64_t value = plan.content_fingerprint;
    value ^= static_cast<std::uint64_t>(program.nodes.size()) * 0x9e3779b97f4a7c15ULL;
    value ^= static_cast<std::uint64_t>(program.animations.size()) +
             (static_cast<std::uint64_t>(program.passes.size()) << 32U);
    value ^= static_cast<std::uint64_t>(program.timelines.size()) << 7U;
    value ^= static_cast<std::uint64_t>(program.animation_tracks.size()) << 13U;
    value ^= static_cast<std::uint64_t>(program.curve_keyframes.size()) << 19U;
    value ^= static_cast<std::uint64_t>(program.texts.size()) << 23U;
    value ^= static_cast<std::uint64_t>(program.text_values.size()) << 29U;
    return value;
}
}
