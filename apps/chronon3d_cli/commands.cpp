#include "commands.hpp"
#include "proof_suites.hpp"
#include <chronon3d/core/pipeline.hpp>
#include <chronon3d/renderer/software_renderer.hpp>
#include <chronon3d/io/image_writer.hpp>
#include <spdlog/spdlog.h>
#include <toml++/toml.h>
#include <iostream>
#include <filesystem>
#include <fmt/format.h>
#include <chrono>
#include <thread>
#include <cstdlib>

namespace chronon3d {
namespace cli {

FrameRange parse_frames(const std::string& s) {
    FrameRange r;
    if (auto pos = s.find('-'); pos == std::string::npos) {
        r.start = r.end = std::stoll(s);
    } else {
        r.start = std::stoll(s.substr(0, pos));
        auto rest = s.substr(pos + 1);
        if (auto x = rest.find('x'); x != std::string::npos) {
            r.end = std::stoll(rest.substr(0, x));
            r.step = std::stoll(rest.substr(x + 1));
        } else {
            r.end = std::stoll(rest);
        }
    }
    return r;
}

std::string format_path(const std::string& pattern, int64_t frame, bool is_range) {
    std::string s = pattern;
    std::string replacement = fmt::format("{:04d}", frame);
    size_t pos = s.find("####");
    if (pos != std::string::npos) {
        s.replace(pos, 4, replacement);
    } else if (is_range) {
        // Automatically append frame number before extension if #### is missing in range mode
        size_t dot_pos = s.find_last_of('.');
        if (dot_pos != std::string::npos) {
            s.insert(dot_pos, "_" + replacement);
        } else {
            s += "_" + replacement;
        }
    }
    return s;
}

int command_list(const CompositionRegistry& registry) {
    auto ids = registry.available();
    if (ids.empty()) {
        std::cout << "No compositions registered." << std::endl;
        return 0;
    }

    std::cout << "Available compositions:" << std::endl;
    for (const auto& id : ids) {
        std::cout << "  - " << id << std::endl;
    }
    return 0;
}

int command_info(const CompositionRegistry& registry, const std::string& id) {
    if (!registry.contains(id)) {
        spdlog::error("Unknown composition: {}", id);
        return 1;
    }

    auto comp = registry.create(id);
    std::cout << "Composition: " << id << std::endl;
    std::cout << "  Dimensions: " << comp.width() << "x" << comp.height() << std::endl;
    std::cout << "  Duration:   " << comp.duration() << " frames" << std::endl;
    std::cout << "  Frame Rate: " << comp.frame_rate().numerator << "/" << comp.frame_rate().denominator << " fps" << std::endl;
    return 0;
}

int command_render(const CompositionRegistry& registry, const RenderArgs& args) {
    if (!registry.contains(args.comp_id)) {
        spdlog::error("Unknown composition: {}", args.comp_id);
        return 1;
    }

    auto range = parse_frames(args.frames);
    
    // Handle legacy arguments if present
    if (args.frame_old != -1) {
        range.start = range.end = args.frame_old;
    } else if (args.start_old != -1 || args.end_old != -1) {
        if (args.start_old != -1) range.start = args.start_old;
        if (args.end_old != -1) range.end = args.end_old;
    }

    auto comp_instance = registry.create(args.comp_id);
    auto comp_ptr = std::make_shared<Composition>(std::move(comp_instance));
    auto renderer = std::make_shared<SoftwareRenderer>();
    renderer->set_diagnostic_mode(args.diagnostic);
    
    RenderPipeline pipeline(comp_ptr, renderer);

    spdlog::info("Rendering {} [{} -> {} step {}]...", args.comp_id, range.start, range.end, range.step);

    // We use a simplified loop for range rendering to support steps
    int64_t effective_end = (range.start == range.end) ? range.start + 1 : range.end;
    for (int64_t f = range.start; f < effective_end; f += range.step) {
        auto scene = comp_ptr->evaluate(f);
        auto fb = renderer->render_scene(scene, comp_ptr->camera, comp_ptr->width(), comp_ptr->height());

        if (fb) {
            bool is_range = (range.start != range.end);
            std::string path = format_path(args.output, f, is_range);
            std::filesystem::path p(path);
            if (p.has_parent_path()) {
                std::filesystem::create_directories(p.parent_path());
            }

            if (save_png(*fb, path)) {
                spdlog::info("Frame {} saved to {}", f, path);
            } else {
                spdlog::error("Failed to save frame {} to {}", f, path);
            }
        }
    }

    spdlog::info("Render complete.");
    return 0;
}

int command_batch(const CompositionRegistry& registry, const std::string& config_path) {
    if (!std::filesystem::exists(config_path)) {
        spdlog::error("Batch config not found: {}", config_path);
        return 1;
    }

    try {
        auto config = toml::parse_file(config_path);
        auto jobs = config["job"].as_array();
        if (!jobs) {
            spdlog::error("No 'job' array found in {}", config_path);
            return 1;
        }

        int total_frames = 0;
        int failed_frames = 0;
        auto t0 = std::chrono::steady_clock::now();

        for (auto& node : *jobs) {
            auto tbl = node.as_table();
            if (!tbl) continue;

            RenderArgs args;
            args.comp_id = (*tbl)["composition"].value_or<std::string>("");
            args.frames = (*tbl)["frames"].value_or<std::string>("0");
            args.output = (*tbl)["output"].value_or<std::string>("render_####.png");
            args.diagnostic = (*tbl)["diagnostic"].value_or<bool>(false);

            if (args.comp_id.empty()) {
                spdlog::warn("Skipping job with missing composition ID");
                continue;
            }

            if (command_render(registry, args) != 0) {
                spdlog::error("Batch job failed for {}", args.comp_id);
                // We continue with other jobs
            }
        }

        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0).count();

        spdlog::info("Batch complete in {}ms", ms);
        return 0;
    } catch (const std::exception& e) {
        spdlog::error("TOML parse error: {}", e.what());
        return 1;
    }
}

int command_watch(const CompositionRegistry& registry, const std::string& comp_id) {
    if (!registry.contains(comp_id)) {
        spdlog::error("Unknown composition: {}", comp_id);
        return 1;
    }

    spdlog::info("Watching for changes... (Polling src/ and examples/)");
    
    auto get_latest_mtime = []() {
        std::filesystem::file_time_type latest = std::filesystem::file_time_type::min();
        auto check_dir = [&](const std::string& dir) {
            if (std::filesystem::exists(dir)) {
                for (const auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
                    if (entry.is_regular_file()) {
                        auto mtime = std::filesystem::last_write_time(entry);
                        if (mtime > latest) latest = mtime;
                    }
                }
            }
        };
        check_dir("src");
        check_dir("include");
        check_dir("examples");
        check_dir("apps");
        return latest;
    };

    auto last_write = get_latest_mtime();
    
    // Initial render
    RenderArgs args;
    args.comp_id = comp_id;
    args.frames = "0";
    args.output = "output/watch_####.png";
    args.diagnostic = true;
    command_render(registry, args);

    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        auto current = get_latest_mtime();
        if (current > last_write) {
            spdlog::info("Change detected! Rebuilding and rendering...");
            
            // Note: This is a bit simplistic as it assumes xmake is in path
            // and we are in the project root.
            int ret = std::system("xmake -y");
            if (ret == 0) {
                command_render(registry, args);
                last_write = current;
            } else {
                spdlog::error("Build failed. Fix errors to continue.");
                // We update last_write anyway to avoid infinite loop on failed build
                // OR we can wait for another change. 
                // Let's wait for another change.
                last_write = current;
            }
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// video command
// ---------------------------------------------------------------------------

namespace {

bool ffmpeg_available() {
#ifdef _WIN32
    return std::system("ffmpeg -version >nul 2>nul") == 0;
#else
    return std::system("ffmpeg -version >/dev/null 2>/dev/null") == 0;
#endif
}

int run_ffmpeg_encode(const std::string& frame_pattern, const std::string& output,
                      int fps, int crf, const std::string& preset) {
    const std::string cmd = fmt::format(
        "ffmpeg -y -framerate {} -i \"{}\" -c:v libx264 -pix_fmt yuv420p "
        "-crf {} -preset {} -movflags +faststart \"{}\"",
        fps, frame_pattern, crf, preset, output);
    return std::system(cmd.c_str());
}

} // anonymous namespace

int command_video(const CompositionRegistry& registry, const VideoArgs& args) {
    if (!registry.contains(args.comp_id)) {
        spdlog::error("Unknown composition: {}", args.comp_id);
        return 1;
    }
    if (args.end <= args.start) {
        spdlog::error("--end ({}) must be greater than --start ({})", args.end, args.start);
        return 1;
    }

    namespace fs = std::filesystem;

    // Determine frames directory
    const fs::path frames_dir = args.frames_dir.empty()
        ? fs::path("output") / "frames" / args.comp_id
        : fs::path(args.frames_dir);

    std::error_code ec;
    fs::create_directories(frames_dir, ec);
    if (ec) {
        spdlog::error("Cannot create frames directory {}: {}", frames_dir.string(), ec.message());
        return 1;
    }

    // Render PNG sequence
    const std::string frame_pattern = (frames_dir / "frame_%04d.png").string();

    RenderArgs render_args;
    render_args.comp_id   = args.comp_id;
    render_args.start_old = args.start;
    render_args.end_old   = args.end;
    render_args.output    = (frames_dir / "frame_####.png").string();

    spdlog::info("Rendering {} frames {} → {} ...", args.comp_id, args.start, args.end);
    const int render_result = command_render(registry, render_args);
    if (render_result != 0) {
        spdlog::error("Frame rendering failed");
        return render_result;
    }

    // Encode with ffmpeg
    if (!ffmpeg_available()) {
        spdlog::error("ffmpeg not found in PATH — install ffmpeg or add it to PATH");
        return 1;
    }

    const fs::path out_path(args.output);
    fs::create_directories(out_path.parent_path(), ec);

    spdlog::info("Encoding {} → {}", frame_pattern, args.output);
    const int ff_result = run_ffmpeg_encode(frame_pattern, args.output, args.fps, args.crf, args.preset);
    if (ff_result != 0) {
        spdlog::error("ffmpeg exited with code {}", ff_result);
        return ff_result;
    }

    if (!fs::exists(out_path) || fs::file_size(out_path) == 0) {
        spdlog::error("Output video missing or empty: {}", args.output);
        return 1;
    }

    if (!args.keep_frames) {
        fs::remove_all(frames_dir, ec);
        if (ec) spdlog::warn("Could not remove frames dir {}: {}", frames_dir.string(), ec.message());
    }

    spdlog::info("Video saved to {}", args.output);
    return 0;
}

// ---------------------------------------------------------------------------
// proofs command
// ---------------------------------------------------------------------------

static int render_proof_suite(const CompositionRegistry& registry,
                               const ProofSuite& suite,
                               const std::filesystem::path& out_dir) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(out_dir, ec);

    int failed = 0;
    for (const auto& pf : suite.frames) {
        if (!registry.contains(pf.composition)) {
            spdlog::warn("Composition not registered (skipped): {}", pf.composition);
            continue;
        }
        const auto out = out_dir / pf.filename;
        spdlog::info("  {} frame {} → {}", pf.composition, pf.frame, out.string());

        RenderArgs args;
        args.comp_id   = pf.composition;
        args.frame_old = pf.frame;
        args.output    = out.string();

        const int r = command_render(registry, args);
        if (r != 0 || !fs::exists(out) || fs::file_size(out) == 0) {
            spdlog::error("  FAILED: {} frame {}", pf.composition, pf.frame);
            ++failed;
        }
    }
    return failed == 0 ? 0 : 1;
}

int command_proofs(const CompositionRegistry& registry, const ProofsArgs& args) {
    if (args.suite == "list") {
        fmt::print("Available proof suites:\n");
        for (const auto& s : proof_suites())
            fmt::print("  {}\n", s.name);
        return 0;
    }

    if (args.suite == "all") {
        int result = 0;
        for (const auto& suite : proof_suites()) {
            spdlog::info("[proofs] Suite: {}", suite.name);
            const auto suite_dir = std::filesystem::path(args.output_dir) / suite.name;
            result |= render_proof_suite(registry, suite, suite_dir);
        }
        return result;
    }

    const auto suite = find_proof_suite(args.suite);
    if (!suite) {
        spdlog::error("Unknown proof suite '{}'. Use 'proofs list' to see available suites.", args.suite);
        return 1;
    }

    spdlog::info("[proofs] Suite: {}", suite->name);
    return render_proof_suite(registry, *suite, args.output_dir);
}

} // namespace cli
} // namespace chronon3d
