#pragma once

#include <chronon3d/compiler/compiled_program.hpp>
#include <chronon3d/render_plan/render_plan.hpp>
#include <chronon3d/render_plan/render_plan_fingerprint.hpp>

namespace chronon3d::compiler::stages {

void lower_scene(const render_plan::RenderPlan&, CompiledProgramData&);
void lower_animation(const render_plan::RenderPlan&, CompiledProgramData&);
void lower_timeline(const render_plan::RenderPlan&, CompiledProgramData&);
void lower_text(const render_plan::RenderPlan&, CompiledProgramData&);
void build_dependencies(CompiledProgramData&);
void build_passes(CompiledProgramData&);
void optimize(CompiledProgramData&);
std::uint64_t fingerprint(const render_plan::RenderPlan&, const CompiledProgramData&);
// Job identity (content_digest / request_digest) is NOT a stage: it is
// render_plan::compute_render_job_fingerprint, the single authority shared
// with the render-plan path (audit FP-1). This namespace only owns the
// structural `fingerprint()` of the compiled program itself.

}  // namespace chronon3d::compiler::stages
