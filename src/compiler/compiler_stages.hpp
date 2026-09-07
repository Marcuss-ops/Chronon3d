#pragma once

#include <chronon3d/compiler/compiled_program.hpp>
#include <chronon3d/render_plan/render_plan.hpp>

namespace chronon3d::compiler::stages {
void lower_scene(const render_plan::RenderPlan&, CompiledProgramData&);
void lower_animation(const render_plan::RenderPlan&, CompiledProgramData&);
void lower_timeline(const render_plan::RenderPlan&, CompiledProgramData&);
void lower_text(const render_plan::RenderPlan&, CompiledProgramData&);
void build_dependencies(CompiledProgramData&);
void build_passes(CompiledProgramData&);
void optimize(CompiledProgramData&);
std::uint64_t fingerprint(const render_plan::RenderPlan&, const CompiledProgramData&);
}
