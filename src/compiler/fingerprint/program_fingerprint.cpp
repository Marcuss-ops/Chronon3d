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
    std::uint64_t value = plan.content_fingerprint ^ 0x9e3779b97f4a7c15ULL;
    const auto mix = [](std::uint64_t x) {
        x += 0x9e3779b97f4a7c15ULL;
        x = (x ^ (x >> 30U)) * 0xbf58476d1ce4e5b9ULL;
        x = (x ^ (x >> 27U)) * 0x94d049bb133111ebULL;
        return x ^ (x >> 31U);
    };
    const auto combine = [&value, &mix](std::size_t count) {
        value = mix(value ^ mix(static_cast<std::uint64_t>(count)));
    };
    combine(program.nodes.size());
    combine(program.animations.size());
    combine(program.passes.size());
    combine(program.timelines.size());
    combine(program.animation_tracks.size());
    combine(program.curve_keyframes.size());
    combine(program.texts.size());
    combine(program.text_values.size());
    return value;
}

}  // namespace chronon3d::compiler::stages
