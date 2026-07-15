#include "../../command_registry.hpp"
#include "../../commands.hpp"
#include "../../utils/common/cli_utils.hpp"
#include <chronon3d/cache/cache_diagnostics.hpp>
#include <fmt/format.h>
#include <memory>
#include <string>

namespace chronon3d::cli {

namespace {

struct BatchState {
    std::shared_ptr<std::vector<std::string>> jobs{std::make_shared<std::vector<std::string>>()};
};

struct RenderState { std::shared_ptr<RenderArgs> args{std::make_shared<RenderArgs>()}; };

// Phase 1d / Increment C — TICKET-PHASE1D-V2-REGISTRY-INTROSPECTION
struct SchemaState { std::shared_ptr<SchemaArgs> args{std::make_shared<SchemaArgs>()}; };

// Phase 1d / Increment D
struct ExamplePropsState { std::shared_ptr<ExamplePropsArgs> args{std::make_shared<ExamplePropsArgs>()}; };

// Phase 1d / Increment E
struct ValidateState { std::shared_ptr<ValidateArgs> args{std::make_shared<ValidateArgs>()}; };

// Phase 1d / Increment F
struct ResolveState { std::shared_ptr<ResolveArgs> args{std::make_shared<ResolveArgs>()}; };

void register_schema(CLI::App& app, CliContext& ctx) {
    auto state = std::make_shared<SchemaState>();
    auto* cmd = app.add_subcommand("schema",
        "Emit PropsSchema as machine-readable JSON (fields + types + defaults + enums)");
    cmd->add_option("id", state->args->comp_id, "Composition name")->required();
    cmd->add_flag("--json,!--no-json", state->args->json,
        "Emit JSON to stdout (default: on)");
    cmd->callback([state, &ctx]() {
        ctx.exit_code = command_schema(ctx.registry, *state->args);
    });
}

void register_example_props(CLI::App& app, CliContext& ctx) {
    auto state = std::make_shared<ExamplePropsState>();
    auto* cmd = app.add_subcommand("example-props",
        "Emit canonical example props (descriptor.schema field defaults) as JSON");
    cmd->add_option("id", state->args->comp_id, "Composition name")->required();
    cmd->add_flag("--json,!--no-json", state->args->json,
        "Emit JSON to stdout (default: on)");
    cmd->callback([state, &ctx]() {
        ctx.exit_code = command_example_props(ctx.registry, *state->args);
    });
}

void register_validate(CLI::App& app, CliContext& ctx) {
    auto state = std::make_shared<ValidateState>();
    auto* cmd = app.add_subcommand("validate",
        "Run canonical decode+validate pipeline; emits {valid: bool, ...} JSON");
    cmd->add_option("id", state->args->comp_id, "Composition name")->required();
    cmd->add_option("--props-file", state->args->props_file,
        "Path to JSON props file (canonical load_props_file() parse)");
    cmd->add_option("--props-json", state->args->props_json,
        "Inline JSON object string (e.g. '{\"title\":\"X\"}')");
    cmd->add_flag("--json,!--no-json", state->args->json,
        "Emit JSON to stdout (default: on)");
    cmd->callback([state, &ctx]() {
        ctx.exit_code = command_validate(ctx.registry, *state->args);
    });
}

void register_resolve(CLI::App& app, CliContext& ctx) {
    auto state = std::make_shared<ResolveState>();
    auto* cmd = app.add_subcommand("resolve",
        "Run canonical resolve() pipeline; emits {resolved: true, props, metadata} JSON");
    cmd->add_option("id", state->args->comp_id, "Composition name")->required();
    cmd->add_option("--props-file", state->args->props_file,
        "Path to JSON props file (canonical load_props_file() parse)");
    cmd->add_option("--props-json", state->args->props_json,
        "Inline JSON object string (e.g. '{\"title\":\"X\"}')");
    cmd->add_flag("--json,!--no-json", state->args->json,
        "Emit JSON to stdout (default: on)");
    cmd->callback([state, &ctx]() {
        ctx.exit_code = command_resolve(ctx.registry, *state->args);
    });
}

void register_render_all(CLI::App& app, CliContext& ctx) {
    auto output_dir = std::make_shared<std::string>(chronon_artifact_path("verify", "").string());
    auto* render_all = app.add_subcommand("render-all", "Render frame 0 of every registered composition");
    render_all->add_option("-o,--output-dir", *output_dir, "Output directory")->default_val(chronon_artifact_path("verify", "").string());
    render_all->callback([output_dir, &ctx]() {
        ctx.exit_code = command_verify(ctx.registry, *output_dir);
    });
}

void register_batch(CLI::App& app, CliContext& ctx) {
    auto jobs = std::make_shared<std::vector<std::string>>();
    auto* cmd = app.add_subcommand("batch", "Run multiple rendering jobs from explicit CLI job specs");
    cmd->add_option("--job", *jobs, "Job spec string: composition|frames|output|diagnostic|graph")->required();
    cmd->callback([jobs, &ctx]() {
        ctx.exit_code = command_batch(ctx.registry, *jobs);
    });
}

void register_bench_convert(CLI::App& app, CliContext& ctx) {
    auto args = std::make_shared<BenchConvertArgs>();
    auto* cmd = app.add_subcommand("benchconvert",
        "Benchmark YUV frame conversion backends (Packed, Swscale)");
    cmd->add_option("comp_id", args->comp_id, "Composition ID to render and convert")->required();
    cmd->add_option("-f,--frame",     args->frame,       "Frame index to render")->default_val(0);
    cmd->add_option("-i,--iterations",args->iterations,  "Timing iterations per backend")->default_val(10);
    cmd->add_option("--format",       args->format,
                    "Output YUV format: nv12 | yuv420p")->default_val("yuv420p");
    cmd->add_flag("--gamma,!--no-gamma", args->apply_gamma,
                  "Apply sRGB gamma when quantising linear RGB")->default_val(true);
    cmd->callback([args, &ctx]() {
        ctx.exit_code = command_bench_convert(ctx.registry, *args);
    });
}

}  // namespace (closes anonymous namespace)

// Top-level dispatcher — declared in command_registry.hpp:11 and called from
// command_registry.cpp's register_all_commands().  Mirrors the flat-mount
// convention used by register_render_commands / register_video_commands:
// each helper adds its subcommand directly to the provided `app`.  Must live
// OUTSIDE the anonymous namespace above so it has external linkage and the
// linker can resolve calls from group_dev.cpp / command_registry.cpp.
void register_cache_stats(CLI::App& app, CliContext& ctx) {
    auto* cmd = app.add_subcommand("cache-stats", "Print live cache diagnostics snapshot");
    cmd->callback([&ctx]() {
        (void)ctx;
        chronon3d::cache::CacheDiagnostics diag;
        fmt::print("{}", chronon3d::cache::format_cache_snapshot(diag));
        ctx.exit_code = 0;
    });
}

void register_dev_commands(CLI::App& app, CliContext& ctx) {
    register_render_all(app, ctx);
    register_batch(app, ctx);
#ifdef CHRONON3D_HAS_CLI_VIDEO_EXPORT
    register_bench_convert(app, ctx);
#endif
    register_cache_stats(app, ctx);
    // Phase 1d / Increment C — TICKET-PHASE1D-V2-REGISTRY-INTROSPECTION
    register_schema(app, ctx);
    // Phase 1d / Increment D
    register_example_props(app, ctx);
    // Phase 1d / Increment E
    register_validate(app, ctx);
    // Phase 1d / Increment F
    register_resolve(app, ctx);
}

}  // namespace chronon3d::cli

