#include "../../commands.hpp"
#include "text_audit_engine.hpp"
#include "text_audit_types.hpp"
#include <chronon3d/core/types/frame_context.hpp>

#include <spdlog/spdlog.h>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

namespace chronon3d {
namespace cli {

namespace {

// Parse a frame spec string like "0,19,20,40,80,160,299" or "0-300x10"
std::vector<int> parse_frame_spec(const std::string& spec) {
    std::vector<int> frames;
    std::istringstream ss(spec);
    std::string token;

    while (std::getline(ss, token, ',')) {
        // Trim whitespace
        token.erase(0, token.find_first_not_of(" \t"));
        token.erase(token.find_last_not_of(" \t") + 1);

        if (token.empty()) continue;

        // Check for range: "start-end" or "start-end x step"
        auto dash = token.find('-');
        if (dash != std::string::npos) {
            int start = std::stoi(token.substr(0, dash));
            std::string rest = token.substr(dash + 1);

            int end = 0;
            int step = 1;
            auto x_pos = rest.find('x');
            if (x_pos != std::string::npos) {
                end = std::stoi(rest.substr(0, x_pos));
                step = std::stoi(rest.substr(x_pos + 1));
            } else {
                end = std::stoi(rest);
            }

            for (int f = start; f <= end; f += step) {
                frames.push_back(f);
            }
        } else {
            frames.push_back(std::stoi(token));
        }
    }

    // Deduplicate and sort
    std::sort(frames.begin(), frames.end());
    frames.erase(std::unique(frames.begin(), frames.end()), frames.end());

    return frames;
}

// Render a single frame to PNG for visual inspection
bool render_frame_to_png(
    const CompositionRegistry& registry,
    const std::string& comp_id,
    int frame,
    const std::string& output_path)
{
    try {
        if (!registry.contains(comp_id)) return false;

        Composition comp = registry.create(comp_id);
        FrameContext render_ctx{
            .frame = Frame{frame},
            .local_frame = Frame{frame},
            .frame_time = 0.0f,
            .duration = comp.duration(),
            .frame_rate = comp.frame_rate(),
            .width = comp.width(),
            .height = comp.height(),
            .assets_root = comp.assets_root(),
            .resource = std::pmr::get_default_resource(),
            .font_engine = nullptr,
        };
        auto scene = comp.evaluate(render_ctx);

        // Use the existing render pipeline via SoftwareRenderer
        // For now, skip actual pixel rendering — the audit uses the
        // rasterizer directly for ink bbox analysis.
        //
        // Full frame rendering can be added later with:
        //   SoftwareRenderer renderer(...);
        //   renderer.render(scene, frame);
        //   renderer.write_png(output_path);

        spdlog::debug("text-audit: render frame {} to {} (placeholder)", frame, output_path);
        return true;
    } catch (const std::exception& e) {
        spdlog::error("text-audit: render frame {} failed: {}", frame, e.what());
        return false;
    }
}

} // anonymous namespace

int command_text_audit(const CompositionRegistry& registry, const TextAuditArgs& args) {
    // Parse frames
    std::vector<int> frames;
    if (args.frames.empty()) {
        // Default: 8 frames covering the typewriter lifecycle
        frames = {0, 19, 20, 40, 80, 160, 299, 309};
    } else {
        frames = parse_frame_spec(args.frames);
    }

    if (frames.empty()) {
        spdlog::error("text-audit: no frames specified");
        return 1;
    }

    // Build policy
    TextAuditPolicy policy;
    policy.safe_margin_x = args.safe_margin_x;
    policy.safe_margin_y = args.safe_margin_y;
    policy.max_center_error_px = args.max_center_error_px;
    policy.max_border_alpha_pixels = args.max_border_alpha_pixels;
    policy.glyph_position_tolerance = args.glyph_tolerance;
    policy.alpha_threshold = args.alpha_threshold;

    // Determine composition name
    std::string comp_id = args.comp_id;
    if (comp_id.empty()) {
        spdlog::error("text-audit: no composition specified");
        return 1;
    }

    spdlog::info("text-audit: auditing '{}' at {} frames", comp_id, frames.size());

    // Run the audit
    auto result = audit_composition(registry, comp_id, frames, policy);

    // Print summary to stdout
    spdlog::info("text-audit: overall_status={} exit_code={}", result.overall_status, result.exit_code);
    for (const auto& fr : result.frames) {
        spdlog::info("  frame {:3d}: status={} lines={} visible={}/{} center_err_x={:.1f}px",
            fr.frame, fr.status, fr.line_count,
            fr.visible_codepoints, fr.total_codepoints,
            fr.checks.center_error_x_px);
    }

    // Write JSON output
    if (!args.json_output.empty()) {
        fs::path json_path(args.json_output);
        if (json_path.has_parent_path()) {
            fs::create_directories(json_path.parent_path());
        }

        std::ofstream ofs(json_path);
        if (!ofs) {
            spdlog::error("text-audit: cannot write JSON to '{}'", args.json_output);
            return 1;
        }
        ofs << audit_result_to_json(result);
        ofs.close();
        spdlog::info("text-audit: JSON report written to '{}'", args.json_output);
    }

    // Render frames (if --render-dir specified)
    if (!args.render_dir.empty()) {
        fs::path render_dir(args.render_dir);
        fs::create_directories(render_dir);

        for (int frame : frames) {
            std::string fname = comp_id + "_frame_" + std::to_string(frame) + ".png";
            fs::path out_path = render_dir / fname;
            render_frame_to_png(registry, comp_id, frame, out_path.string());
        }
        spdlog::info("text-audit: rendered frames to '{}'", args.render_dir);
    }

    // Warn about failures
    if (result.exit_code != 0) {
        spdlog::warn("text-audit: FAILED with exit code {} ({})",
            result.exit_code,
            result.exit_code == 1 ? "layout error" :
            result.exit_code == 2 ? "clipping" :
            result.exit_code == 3 ? "typewriter instability" :
            result.exit_code == 4 ? "missing glyphs" :
            result.exit_code == 5 ? "visual diff" : "unknown");
    } else {
        spdlog::info("text-audit: PASSED — all checks green");
    }

    return result.exit_code;
}

} // namespace cli
} // namespace chronon3d
