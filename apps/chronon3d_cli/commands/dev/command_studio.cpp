#include "../../commands.hpp"
#include "../../utils/job/render_job.hpp"
#include "../../utils/job/cli_render_utils.hpp"
#include "../../utils/telemetry/telemetry_run.hpp"
#include "../../utils/common/cli_utils.hpp"
#include <chronon3d/backends/image/image_writer.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <fmt/format.h>
#include <spdlog/spdlog.h>
#include <chrono>
#include <filesystem>

namespace chronon3d::cli {

namespace {


bool save_studio_output(const Framebuffer& fb, const std::string& out_path) {
    auto parent = std::filesystem::path(out_path).parent_path();
    if (!parent.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            spdlog::error("Failed to create directory {}: {}", parent.string(), ec.message());
            return false;
        }
    }

    if (!save_png(fb, out_path)) {
        spdlog::error("Failed to save image to {}", out_path);
        return false;
    }

    spdlog::info("Studio output saved successfully to: {}", out_path);
    return true;
}

std::string resolve_output_path(const std::string& requested, const std::string& fallback) {
    if (!requested.empty()) {
        return requested;
    }
    return chronon3d::cli::chronon_artifact_path("previews", fallback).string();
}

} // namespace

int command_preview(const CompositionRegistry& registry, const RenderArgs& args) {
    auto plan = plan_render_job(registry, args);
    if (!plan) {
        return 1;
    }

    // Default to middle frame if default value "0" or empty
    Frame frame{0};
    if (args.frames == "0" || args.frames.empty()) {
        frame = static_cast<Frame>(plan->comp->duration() / 2);
    } else {
        try {
            frame = static_cast<Frame>(std::stoll(args.frames));
        } catch (...) {
            frame = Frame{0};
        }
    }

    const auto started_at_iso = chronon3d::telemetry::TelemetryManager::get_current_iso_time();
    const auto wall_t0 = std::chrono::steady_clock::now();

    auto renderer = create_renderer(registry, plan->settings);
    const auto setup_t1 = std::chrono::steady_clock::now();
    spdlog::info("Studio Preview: Rendering frame {} of composition '{}'...", frame, plan->comp_id);

    const auto render_t0 = std::chrono::steady_clock::now();
    auto fb = renderer->render_frame(*plan->comp, frame);
    const auto render_t1 = std::chrono::steady_clock::now();
    if (!fb) {
        spdlog::error("Failed to render preview frame {}", frame);
        return 1;
    }

    std::string out_path = resolve_output_path(args.output, "preview.png");
    const auto encode_t0 = std::chrono::steady_clock::now();
    const bool saved = save_studio_output(*fb, out_path);
    const auto encode_t1 = std::chrono::steady_clock::now();
    const auto wall_t1 = std::chrono::steady_clock::now();

    if (saved) {
        const auto phases = std::vector<chronon3d::telemetry::PhaseTelemetryRecord>{
            {"setup_renderer", std::chrono::duration<double, std::milli>(setup_t1 - wall_t0).count()},
            {"rendering_loop", std::chrono::duration<double, std::milli>(render_t1 - render_t0).count()},
            {"encoder_close_and_flush", std::chrono::duration<double, std::milli>(encode_t1 - encode_t0).count()},
        };
        cli::telemetry::record_output_run(
            plan->comp_id,
            out_path,
            true,
            1,
            1,
            std::chrono::duration<double, std::milli>(wall_t1 - wall_t0).count(),
            std::chrono::duration<double, std::milli>(render_t1 - render_t0).count(),
            std::chrono::duration<double, std::milli>(encode_t1 - encode_t0).count(),
            started_at_iso,
            phases,
            cli::telemetry::capture_counters(*renderer->counters()),
            {},
            renderer->counters());
    }

    return saved ? 0 : 1;
}

int command_contact_sheet(const CompositionRegistry& registry, const RenderArgs& args) {
    auto plan = plan_render_job(registry, args);
    if (!plan) {
        return 1;
    }

    const auto started_at_iso = chronon3d::telemetry::TelemetryManager::get_current_iso_time();
    const auto wall_t0 = std::chrono::steady_clock::now();

    auto renderer = create_renderer(registry, plan->settings);
    const auto setup_t1 = std::chrono::steady_clock::now();
    i32 w = plan->comp->width();
    i32 h = plan->comp->height();
    Frame duration = plan->comp->duration();

    spdlog::info("Studio Contact Sheet: Rendering 4 frames spaced evenly across {} frames...", duration);

    // Calculate 4 even frames
    Frame frames[4];
    frames[0] = Frame{0};
    frames[1] = Frame{duration / 3};
    frames[2] = Frame{(duration * 2) / 3};
    frames[3] = Frame{duration > 0 ? duration - 1 : 0};

    Framebuffer sheet(w * 4, h);
    sheet.clear(Color::black());

    const auto render_t0 = std::chrono::steady_clock::now();
    for (i32 i = 0; i < 4; ++i) {
        spdlog::info("Rendering sheet frame {} / 4 (Frame: {})...", i + 1, frames[i]);
        auto fb = renderer->render_frame(*plan->comp, frames[i]);
        if (!fb) {
            spdlog::error("Failed to render sheet frame at {}", frames[i]);
            return 1;
        }
        sheet.blit(*fb, i * w, 0);
    }
    const auto render_t1 = std::chrono::steady_clock::now();

    std::string out_path = resolve_output_path(args.output, "contact_sheet.png");
    const auto encode_t0 = std::chrono::steady_clock::now();
    const bool saved = save_studio_output(sheet, out_path);
    const auto encode_t1 = std::chrono::steady_clock::now();
    const auto wall_t1 = std::chrono::steady_clock::now();

    if (saved) {
        const auto phases = std::vector<chronon3d::telemetry::PhaseTelemetryRecord>{
            {"setup_renderer", std::chrono::duration<double, std::milli>(setup_t1 - wall_t0).count()},
            {"rendering_loop", std::chrono::duration<double, std::milli>(render_t1 - render_t0).count()},
            {"encoder_close_and_flush", std::chrono::duration<double, std::milli>(encode_t1 - encode_t0).count()},
        };
        cli::telemetry::record_output_run(
            plan->comp_id,
            out_path,
            true,
            4,
            4,
            std::chrono::duration<double, std::milli>(wall_t1 - wall_t0).count(),
            std::chrono::duration<double, std::milli>(render_t1 - render_t0).count(),
            std::chrono::duration<double, std::milli>(encode_t1 - encode_t0).count(),
            started_at_iso,
            phases,
            cli::telemetry::capture_counters(*renderer->counters()),
            {},
            renderer->counters());
    }

    return saved ? 0 : 1;
}

int command_storyboard(const CompositionRegistry& registry, const RenderArgs& args) {
    auto plan = plan_render_job(registry, args);
    if (!plan) {
        return 1;
    }

    const auto started_at_iso = chronon3d::telemetry::TelemetryManager::get_current_iso_time();
    const auto wall_t0 = std::chrono::steady_clock::now();

    auto renderer = create_renderer(registry, plan->settings);
    const auto setup_t1 = std::chrono::steady_clock::now();
    i32 w = plan->comp->width();
    i32 h = plan->comp->height();
    Frame duration = plan->comp->duration();

    spdlog::info("Studio Storyboard: Rendering 6 keyframes with beautiful HUD overlays...", duration);

    // Calculate 6 even frames: 2 rows of 3 columns
    Frame frames[6];
    for (i32 i = 0; i < 6; ++i) {
        frames[i] = Frame{duration > 1 ? (duration - 1) * i / 5 : 0};
    }

    Framebuffer storyboard(w * 3, h * 2);
    storyboard.clear(Color::black());

    const auto render_t0 = std::chrono::steady_clock::now();
    for (i32 i = 0; i < 6; ++i) {
        Frame f = frames[i];
        spdlog::info("Rendering storyboard panel {} / 6 (Frame: {})...", i + 1, f);

        // Evaluate the scene manually
        Scene scene = plan->comp->evaluate(f);

        // Append beautiful HUD overlay layer containing Frame & Time Z-aligned on top
        LayerBuilder builder("hud_overlay", f, scene.resource());
        builder.screen_dimensions(static_cast<f32>(w), static_cast<f32>(h))
               .position({ w * 0.5f, h * 0.5f, 0.0f });

        // HUD bottom status bar/pill
        builder.rounded_rect("hud_pill", {
            .size = { 420.0f, 64.0f },
            .radius = 32.0f,
            .color = Color{ 0.05f, 0.05f, 0.08f, 0.85f },
            .pos = { 0.0f, h * 0.5f - 80.0f, 0.0f }
        });

        // Pill border
        builder.rounded_rect("hud_pill_border", {
            .size = { 422.0f, 66.0f },
            .radius = 33.0f,
            .color = Color{ 0.3f, 0.6f, 1.0f, 0.4f },
            .pos = { 0.0f, h * 0.5f - 80.0f, 0.0f }
        });

        scene.add_layer(builder.build());

        // Render the scene directly with HUD overlay
        auto panel_fb = renderer->render_scene(scene, plan->comp->camera, w, h);
        if (!panel_fb) {
            spdlog::error("Failed to render storyboard panel at frame {}", f);
            return 1;
        }

        i32 col = i % 3;
        i32 row = i / 3;
        storyboard.blit(*panel_fb, col * w, row * h);
    }
    const auto render_t1 = std::chrono::steady_clock::now();

    std::string out_path = resolve_output_path(args.output, "storyboard.png");
    const auto encode_t0 = std::chrono::steady_clock::now();
    const bool saved = save_studio_output(storyboard, out_path);
    const auto encode_t1 = std::chrono::steady_clock::now();
    const auto wall_t1 = std::chrono::steady_clock::now();

    if (saved) {
        const auto phases = std::vector<chronon3d::telemetry::PhaseTelemetryRecord>{
            {"setup_renderer", std::chrono::duration<double, std::milli>(setup_t1 - wall_t0).count()},
            {"rendering_loop", std::chrono::duration<double, std::milli>(render_t1 - render_t0).count()},
            {"encoder_close_and_flush", std::chrono::duration<double, std::milli>(encode_t1 - encode_t0).count()},
        };
        cli::telemetry::record_output_run(
            plan->comp_id,
            out_path,
            true,
            6,
            6,
            std::chrono::duration<double, std::milli>(wall_t1 - wall_t0).count(),
            std::chrono::duration<double, std::milli>(render_t1 - render_t0).count(),
            std::chrono::duration<double, std::milli>(encode_t1 - encode_t0).count(),
            started_at_iso,
            phases,
            cli::telemetry::capture_counters(*renderer->counters()),
            {},
            renderer->counters());
    }

    return saved ? 0 : 1;
}

} // namespace chronon3d::cli
