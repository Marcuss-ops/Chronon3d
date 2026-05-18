#include "../commands.hpp"
#include "../utils/render_job.hpp"
#include "../utils/cli_render_utils.hpp"
#include <chronon3d/backends/image/image_writer.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <fmt/format.h>
#include <spdlog/spdlog.h>
#include <chrono>
#include <filesystem>

namespace chronon3d::cli {

namespace {

void copy_framebuffer_pixels(Framebuffer& dst, const Framebuffer& src, i32 start_x, i32 start_y) {
    for (i32 y = 0; y < src.height(); ++y) {
        for (i32 x = 0; x < src.width(); ++x) {
            dst.set_pixel(start_x + x, start_y + y, src.get_pixel(x, y));
        }
    }
}

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

    auto renderer = create_renderer(registry, plan->settings);
    spdlog::info("Studio Preview: Rendering frame {} of composition '{}'...", frame, plan->comp_id);

    auto fb = renderer->render_frame(*plan->comp, frame);
    if (!fb) {
        spdlog::error("Failed to render preview frame {}", frame);
        return 1;
    }

    std::string out_path = args.output.empty() ? "preview.png" : args.output;
    return save_studio_output(*fb, out_path) ? 0 : 1;
}

int command_contact_sheet(const CompositionRegistry& registry, const RenderArgs& args) {
    auto plan = plan_render_job(registry, args);
    if (!plan) {
        return 1;
    }

    auto renderer = create_renderer(registry, plan->settings);
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

    for (i32 i = 0; i < 4; ++i) {
        spdlog::info("Rendering sheet frame {} / 4 (Frame: {})...", i + 1, frames[i]);
        auto fb = renderer->render_frame(*plan->comp, frames[i]);
        if (!fb) {
            spdlog::error("Failed to render sheet frame at {}", frames[i]);
            return 1;
        }
        copy_framebuffer_pixels(sheet, *fb, i * w, 0);
    }

    std::string out_path = args.output.empty() ? "contact_sheet.png" : args.output;
    return save_studio_output(sheet, out_path) ? 0 : 1;
}

int command_storyboard(const CompositionRegistry& registry, const RenderArgs& args) {
    auto plan = plan_render_job(registry, args);
    if (!plan) {
        return 1;
    }

    auto renderer = create_renderer(registry, plan->settings);
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

        // Overlay metadata text
        builder.text("hud_text", {
            .content = fmt::format("PANEL {} | F: {} | {:.2f}s", i + 1, f, static_cast<f32>(f) / plan->comp->frame_rate().fps()),
            .style = {
                .font_family = "Inter",
                .font_weight = 700,
                .size = 20.0f,
                .color = Color::white(),
                .align = TextAlign::Center
            },
            .box = { .size = { 400.0f, 40.0f }, .enabled = true }
        }).at({ 0.0f, h * 0.5f - 80.0f, 0.0f });

        scene.add_layer(builder.build());

        // Render the scene directly with HUD overlay
        auto panel_fb = renderer->render_scene(scene, plan->comp->camera, w, h);
        if (!panel_fb) {
            spdlog::error("Failed to render storyboard panel at frame {}", f);
            return 1;
        }

        i32 col = i % 3;
        i32 row = i / 3;
        copy_framebuffer_pixels(storyboard, *panel_fb, col * w, row * h);
    }

    std::string out_path = args.output.empty() ? "storyboard.png" : args.output;
    return save_studio_output(storyboard, out_path) ? 0 : 1;
}

} // namespace chronon3d::cli
