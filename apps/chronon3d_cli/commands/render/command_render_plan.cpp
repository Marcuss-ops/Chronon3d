#include "../../command_registry.hpp"

#include <chronon3d/c_api/chronon3d.h>

#include <CLI/CLI.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <cstdint>
#include <cerrno>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace chronon3d::cli {
namespace {

struct RenderPlanState {
    std::string input;
    std::string output;
    std::string assets_root;
    std::uint64_t start_frame{0};
    std::uint64_t end_frame{0};
    std::uint32_t fps_num{30};
    std::uint32_t fps_den{1};
};

std::string read_file(const std::string& path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("cannot open render plan: " + path);
    std::ostringstream contents;
    contents << input.rdbuf();
    return contents.str();
}

int run_process(const std::vector<std::string>& arguments) {
    if (arguments.empty()) return -1;
    std::vector<char*> argv;
    argv.reserve(arguments.size() + 1);
    for (const auto& argument : arguments) argv.push_back(const_cast<char*>(argument.c_str()));
    argv.push_back(nullptr);
    const pid_t child = ::fork();
    if (child < 0) return -1;
    if (child == 0) {
        ::execvp(argv[0], argv.data());
        _exit(127);
    }
    int status = 0;
    while (::waitpid(child, &status, 0) < 0 && errno == EINTR) {}
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128;
}

std::string resolve_audio_path(const std::string& source, const std::string& assets_root) {
    const std::filesystem::path path(source);
    if (path.is_absolute() || assets_root.empty()) return path.string();
    return (std::filesystem::path(assets_root) / path).string();
}

bool mux_audio(const nlohmann::json& root, const std::string& output, const std::string& assets_root) {
    const auto tracks = root.value("audio_tracks", nlohmann::json::array());
    if (!tracks.is_array() || tracks.empty()) return true;

    std::vector<std::string> audio_paths;
    std::vector<double> volumes;
    std::vector<double> offsets;
    std::vector<double> durations;
    for (const auto& track : tracks) {
        const auto source = track.value("source", track.value("source_url", std::string{}));
        if (source.empty()) {
            spdlog::error("Audio track is missing source");
            return false;
        }
        audio_paths.push_back(resolve_audio_path(source, assets_root));
        volumes.push_back(track.value("volume", 1.0));
        offsets.push_back(track.value("start_time_offset", 0.0));
        durations.push_back(track.value("duration_seconds", 0.0));
    }

    const std::string temp = output + ".audio.tmp.mp4";
    std::vector<std::string> command{"ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-i", output};
    for (const auto& path : audio_paths) command.insert(command.end(), {"-i", path});
    command.insert(command.end(), {"-map", "0:v:0"});
    std::string filter;
    for (std::size_t i = 0; i < audio_paths.size(); ++i) {
        filter += "[" + std::to_string(i + 1) + ":a]volume=" + std::to_string(volumes[i]);
        if (offsets[i] > 0) {
            const auto delay_ms = static_cast<long long>(offsets[i] * 1000.0);
            filter += ",adelay=" + std::to_string(delay_ms) + ":all=1";
        }
        if (durations[i] > 0) filter += ",atrim=duration=" + std::to_string(durations[i]);
        filter += "[a" + std::to_string(i) + "];";
    }
    if (audio_paths.size() == 1) {
        filter += "[a0]anull[aout]";
    } else {
        for (std::size_t i = 0; i < audio_paths.size(); ++i) filter += "[a" + std::to_string(i) + "]";
        filter += "amix=inputs=" + std::to_string(audio_paths.size()) + ":duration=longest:dropout_transition=0[aout]";
    }
    command.insert(command.end(), {"-filter_complex", filter, "-map", "[aout]", "-c:v", "copy", "-c:a", "aac", "-shortest"});
    command.insert(command.end(), {"-movflags", "+faststart", temp});
    if (run_process(command) != 0) {
        std::error_code ignored;
        std::filesystem::remove(temp, ignored);
        spdlog::error("FFmpeg audio mux failed");
        return false;
    }
    std::error_code error;
    std::filesystem::rename(temp, output, error);
    if (error) {
        std::filesystem::remove(temp, error);
        spdlog::error("Cannot replace rendered output with audio-muxed file: {}", error.message());
        return false;
    }
    return true;
}

int run_render_plan(const RenderPlanState& args) {
    try {
        const auto source = read_file(args.input);
        const auto root = nlohmann::json::parse(source);
        const auto canvas = root.at("canvas");
        const auto duration = canvas.at("duration_frames").get<std::uint64_t>();
        const auto output = args.output.empty()
            ? root.at("output").at("path").get<std::string>()
            : args.output;
        const auto fps = canvas.value("fps", static_cast<std::uint32_t>(30));

        chronon_engine_config config{};
        config.struct_size = sizeof(config);
        config.abi_version = chronon_abi_version();
        config.assets_root = args.assets_root.empty() ? nullptr : args.assets_root.c_str();
        chronon_engine* engine = chronon_engine_create(&config);
        if (!engine) {
            spdlog::error("Chronon engine creation failed");
            return 1;
        }

        chronon_plan* plan = nullptr;
        auto status = chronon_plan_compile_json(engine, source.c_str(), &plan);
        if (status != CHRONON_OK) {
            spdlog::error("Render plan compilation failed: {}", chronon_engine_last_error(engine));
            chronon_engine_destroy(engine);
            return 1;
        }

        const auto end = args.end_frame == 0 ? duration - 1 : args.end_frame;
        chronon_render_callbacks callbacks{};
        callbacks.progress = [](void*, std::uint64_t current, std::uint64_t total) {
            spdlog::info("render progress: {}/{}", current, total);
        };
        status = chronon_render_file(engine, plan, output.c_str(), args.start_frame, end,
                                     args.fps_num == 30 && args.fps_den == 1 ? fps : args.fps_num,
                                     args.fps_num == 30 && args.fps_den == 1 ? 1 : args.fps_den,
                                     &callbacks);
        if (status != CHRONON_OK) {
            spdlog::error("Render failed: {}", chronon_engine_last_error(engine));
        } else if (!mux_audio(root, output, args.assets_root)) {
            status = CHRONON_ERROR_RENDER_FAILED;
        }
        chronon_plan_destroy(plan);
        chronon_engine_destroy(engine);
        return status == CHRONON_OK ? 0 : 1;
    } catch (const std::exception& error) {
        spdlog::error("Render plan error: {}", error.what());
        return 1;
    }
}

} // namespace

void register_render_plan_command(CLI::App& app, CliContext&) {
    auto* command = app.add_subcommand("render-plan", "Render a chronon.render-plan.v1 JSON file");
    auto state = std::make_shared<RenderPlanState>();
    command->add_option("--input", state->input, "Render plan JSON")->required();
    command->add_option("--output", state->output, "Output file (overrides plan output.path)");
    command->add_option("--assets-root", state->assets_root, "Instance asset root");
    command->add_option("--start-frame", state->start_frame, "First frame");
    command->add_option("--end-frame", state->end_frame, "Last frame, inclusive");
    command->add_option("--fps-num", state->fps_num, "Frame-rate numerator");
    command->add_option("--fps-den", state->fps_den, "Frame-rate denominator");
    command->callback([state] { return run_render_plan(*state); });
}

} // namespace chronon3d::cli
