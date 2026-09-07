#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/compiler/compiler.hpp>
#include <chronon3d/render_plan/render_plan.hpp>
#include <chronon3d/timeline/compiled_composition.hpp>

#include <doctest/doctest.h>

#include <type_traits>
#include <utility>

TEST_CASE("compiler boundary returns an immutable CompiledProgram") {
    static_assert(!std::is_copy_constructible_v<chronon3d::compiler::CompiledProgram>);
    static_assert(!std::is_copy_assignable_v<chronon3d::compiler::CompiledProgram>);

    chronon3d::render_plan::RenderPlan plan;
    plan.job_id = "compiler-boundary";
    plan.canvas.width = 1920;
    plan.canvas.height = 1080;
    plan.canvas.duration = chronon3d::Frame{1};

    chronon3d::assets::AssetResolver resolver;
    auto result = chronon3d::compiler::compile(plan, resolver);

    REQUIRE(result);
    auto program = std::move(result).value();
    CHECK(program.job_id() == "compiler-boundary");
    CHECK(program.canvas().width == 1920);
    CHECK(program.canvas().height == 1080);
    CHECK(program.compiled_composition().composition != nullptr);
    CHECK(program.metadata.job_id == "compiler-boundary");
    CHECK(program.nodes.empty());
    CHECK(program.passes.size() == 1);
    CHECK(program.fingerprint_data.algorithm_version == 1);
}
