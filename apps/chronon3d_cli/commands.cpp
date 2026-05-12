#include "commands.hpp"
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

} // namespace cli
} // namespace chronon3d
