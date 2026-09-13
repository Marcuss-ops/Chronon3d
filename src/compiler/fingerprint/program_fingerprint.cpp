// program_fingerprint.cpp — structural fingerprint of a compiled program.
//
// Audit FP-1: this file used to also carry a second, verbatim copy of the job
// identity (`compute_digests` + its private `render_settings_fingerprint`),
// duplicating src/render_plan/render_plan_compiler_fingerprint.cpp. Two
// independent producers of the same digest pair are a wrong-cache-hit hazard:
// a program/binary cache keyed on them could see different values for the same
// job. The job identity now lives only in
// render_plan::compute_render_job_fingerprint; what remains here is the
// structural `fingerprint()` of the CompiledProgramData itself, which is a
// different fact (topology/IR shape, not content identity).

#include "../compiler_stages.hpp"

#include <cstdint>

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

}  // namespace chronon3d::compiler::stages
