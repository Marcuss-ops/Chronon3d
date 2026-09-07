#pragma once

#include <chronon3d/compiler/compiled_program.hpp>
#include <chronon3d/render_plan/render_plan.hpp>

namespace chronon3d::compiler::stages {
void lower_scene(const render_plan::RenderPlan&, CompiledProgram&);
void lower_animation(const render_plan::RenderPlan&, CompiledProgram&);
void lower_timeline(const render_plan::RenderPlan&, CompiledProgram&);
void lower_text(const render_plan::RenderPlan&, CompiledProgram&);
void build_dependencies(CompiledProgram&);
void build_passes(CompiledProgram&);
void optimize(CompiledProgram&);
std::uint64_t fingerprint(const render_plan::RenderPlan&, const CompiledProgram&);
}
