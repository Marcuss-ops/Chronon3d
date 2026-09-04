#include "command_registry.hpp"
#include "commands.hpp"
#include "dev/doctor_report.hpp"
#include "../utils/common/cli_mappers.hpp"
#include "../utils/common/cli_utils.hpp"

#include <memory>
#include <string_view>
#include <CLI/CLI.hpp>

namespace chronon3d::cli::group_core {

namespace {

struct InfoState {
    std::shared_ptr<std::string> id{std::make_shared<std::string>()};
};

struct VerifyState {
    std::shared_ptr<std::string> output_dir{
        std::make_shared<std::string>(
            chronon_artifact_path("verify", "").string())
    };
};

void register_list(CLI::App& app, CliContext& ctx) {
    auto* command = app.add_subcommand(
        "list", "List registered compositions and metadata");
    command->callback([&ctx]() {
        ctx.exit_code = command_list(ctx.registry);
    });
}

void register_benchmark_machine(CLI::App& app, CliContext& ctx) {
    auto* command = app.add_subcommand(
        "benchmark-machine",
        "Certify the host machine: CPU, logical cores, SIMD, NUMA, TBB budget");
    command->callback([&ctx]() {
        ctx.exit_code = command_benchmark_machine(ctx);
    });
}

void register_info(CLI::App& app, CliContext& ctx) {
    auto state = std::make_shared<InfoState>();
    auto* command = app.add_subcommand(
        "info", "Inspect a composition descriptor");
    command->add_option("id", *state->id, "Composition name")->required();
    command->callback([state, &ctx]() {
        ctx.exit_code = command_info(ctx.registry, *state->id);
    });
}

void register_benchmark(CLI::App& app, CliContext& ctx) {
    struct BenchmarkSatState {
        std::shared_ptr<std::string> scene{std::make_shared<std::string>()};
        std::shared_ptr<int> duration{std::make_shared<int>(300)};
        std::shared_ptr<bool> saturation{std::make_shared<bool>(false)};
        std::shared_ptr<std::string> report_json{std::make_shared<std::string>()};
        std::shared_ptr<int> motion_blur_mode{std::make_shared<int>(0)};
        std::shared_ptr<int> motion_blur_samples{std::make_shared<int>(8)};
        std::shared_ptr<std::string> backend{std::make_shared<std::string>("auto")};
    };
    auto state = std::make_shared<BenchmarkSatState>();
    auto* command = app.add_subcommand(
        "benchmark",
        "Benchmark a composition and produce the Saturation Report");
    command->add_option("--scene", *state->scene, "Composition name")->required();
    command->add_option("--duration", *state->duration, "Benchmark duration in seconds")
        ->default_val(300);
    command->add_flag("--saturation", *state->saturation,
                      "Print the full CHRONON3D SATURATION REPORT");
    command->add_option("--report-json", *state->report_json,
                        "Write the machine-readable benchmark report to this JSON path");
    command->add_option("--motion-blur-mode", *state->motion_blur_mode,
                        "Motion blur mode: 0=off, 1=temporal, 2=velocity")
        ->check(CLI::Range(0, 2));
    command->add_option("--motion-blur-samples", *state->motion_blur_samples,
                        "Motion blur subframe samples")
        ->check(CLI::Range(1, 64));
    command->add_option("--backend", *state->backend,
                        "Render backend: auto, software, or vulkan (strict)")
        ->check(CLI::IsMember({"auto", "software", "vulkan"}));
    command->callback([state, &ctx]() {
        ctx.exit_code = command_benchmark_saturation(
            ctx.registry, ctx, *state->scene, *state->duration, *state->report_json,
            parse_motion_blur_mode(*state->motion_blur_mode),
            *state->motion_blur_samples,
            parse_backend_preference(*state->backend));
    });
}

void register_doctor(CLI::App& app, CliContext& ctx) {
    auto state = std::make_shared<DoctorOptions>();
    auto* command = app.add_subcommand(
        "doctor", "Check whether the local Chronon3d environment is ready");
    command->add_flag("--json", state->json,
                      "Emit a machine-readable JSON report");
    command->add_option("--assets-root", state->assets_root,
                        "Asset root used for font/asset resolution checks");
    command->add_flag("--deep", state->deep,
                      "Run the deep render smoke test (compile + render 1 frame)");
    command->callback([state, &ctx]() {
        ctx.exit_code = command_doctor(ctx.registry, *state);
    });
}

void register_verify(CLI::App& app, CliContext& ctx) {
    auto state = std::make_shared<VerifyState>();
    auto* command = app.add_subcommand(
        "verify", "Run a quick render and video smoke test");
    command->add_option("-o,--output-dir", *state->output_dir,
                        "Output directory")
        ->default_val(chronon_artifact_path("verify", "").string());
    command->callback([state, &ctx]() {
        ctx.exit_code = command_verify(ctx.registry, *state->output_dir);
    });
}

void register_daemon(CLI::App& app, CliContext& ctx) {
    auto* command = app.add_subcommand(
        "daemon", "Start a warm render shell with persistent caches");
    auto assets_root = std::make_shared<std::string>();
    auto build_command = std::make_shared<std::string>("bash build-fast.sh cli");
    auto socket_path = std::make_shared<std::string>();
    auto backend = std::make_shared<std::string>("auto");
    auto gpu_device_id = std::make_shared<std::uint32_t>(chronon3d::Config::kAutoGpuDevice);
    command->add_option("-a,--assets-root", *assets_root,
                        "Asset root directory (fonts, images)");
    command->add_option("-b,--build-cmd", *build_command,
                        "Build command used by the manual reload action");
    command->add_option("-s,--socket", *socket_path,
                        "Serve the RenderingGen→Chronon IPC protocol on this "
                        "UNIX-domain socket path instead of stdin");
    command->add_option("--backend", *backend,
                        "Persistent render backend: auto, software, or vulkan")
        ->check(CLI::IsMember({"auto", "software", "vulkan"}));
    command->add_option("--gpu-device", *gpu_device_id,
                        "Vulkan graphics-device index (default: automatic)");
    command->callback([assets_root, build_command, socket_path, backend,
                       gpu_device_id, &ctx]() {
        ctx.exit_code = command_daemon(
            ctx.registry, *assets_root, *build_command, *socket_path,
            parse_backend_preference(*backend), *gpu_device_id);
    });
}

} // namespace

void register_commands(CLI::App& app, CliContext& ctx) {
    register_list(app, ctx);
    register_benchmark_machine(app, ctx);
    register_benchmark(app, ctx);
    register_info(app, ctx);
    register_doctor(app, ctx);
    register_verify(app, ctx);
    register_daemon(app, ctx);
#ifdef CHRONON3D_HAS_CLI_RENDER
    register_watch_commands(app, ctx);
    register_preview_commands(app, ctx);
#endif
}

} // namespace chronon3d::cli::group_core
