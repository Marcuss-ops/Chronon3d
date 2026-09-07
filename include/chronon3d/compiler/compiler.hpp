#pragma once

#include <chronon3d/compiler/compiled_program.hpp>
#include <chronon3d/compiler/compile_error.hpp>
#include <chronon3d/core/types/result.hpp>
#include <chronon3d/render_plan/render_plan.hpp>
#include <chronon3d/render_plan/render_plan_fingerprint.hpp>

namespace chronon3d::assets {
class AssetResolver;
}

namespace chronon3d::compiler {

/// Source accepted by the single compiler root. Decoding JSON into this
/// authoring form is intentionally separate from lowering/execution.
struct RenderJobSource {
    render_plan::RenderPlan plan;
};

struct CompileOptions {
    render_plan::RenderPlanFingerprintOptions fingerprint{};
};

/// Compiler facade. It is deliberately independent from execution resources.
class Compiler final {
public:
    [[nodiscard]] Result<CompiledProgram, CompileError> compile(
        const RenderJobSource& source,
        assets::AssetResolver& resolver,
        const CompileOptions& options = {}) const;

    [[nodiscard]] Result<CompiledProgram, CompileError> compile(
        const render_plan::RenderPlan& plan,
        assets::AssetResolver& resolver,
        const CompileOptions& options = {}) const;
};

[[nodiscard]] inline Result<CompiledProgram, CompileError> compile(
    const RenderJobSource& source,
    assets::AssetResolver& resolver,
    const CompileOptions& options = {}) {
    return Compiler{}.compile(source, resolver, options);
}

[[nodiscard]] inline Result<CompiledProgram, CompileError> compile(
    const render_plan::RenderPlan& plan,
    assets::AssetResolver& resolver,
    const CompileOptions& options = {}) {
    return Compiler{}.compile(plan, resolver, options);
}

}  // namespace chronon3d::compiler
