#include <doctest/doctest.h>
#include <apps/chronon3d_cli/utils/render_job_plan.hpp>

using namespace chronon3d::cli;

TEST_CASE("render job rejects empty output") {
    RenderArgs args;
    args.comp_id = "Demo";
    args.frames = "0";
    args.output = "";

    auto result = plan_render_job(args, false);
    CHECK_FALSE(result.ok);
    CHECK(result.error.find("Output path") != std::string::npos);
}

TEST_CASE("render job rejects invalid ssaa") {
    RenderArgs args;
    args.comp_id = "Demo";
    args.frames = "0";
    args.output = "out.png";
    args.ssaa = 0.0f;

    auto result = plan_render_job(args, false);
    CHECK_FALSE(result.ok);
}

TEST_CASE("render job accepts valid single frame") {
    RenderArgs args;
    args.comp_id = "Demo";
    args.frames = "0";
    args.output = "out.png";

    auto result = plan_render_job(args, false);
    CHECK(result.ok);
    CHECK(result.value.range.start == 0);
    CHECK(result.value.range.end == 0);
}

TEST_CASE("render job handles modern frames args") {
    RenderArgs args;
    args.comp_id = "Demo";
    args.output = "out.png";
    args.frames = "42";

    auto result = plan_render_job(args, false);
    CHECK(result.ok);
    CHECK(result.value.range.start == 42);
    CHECK(result.value.range.end == 42);
}
