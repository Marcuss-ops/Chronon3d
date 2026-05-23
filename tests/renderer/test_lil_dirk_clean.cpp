#include <doctest/doctest.h>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/camera/camera_motion_presets.hpp>
#include <chronon3d/api/composition.hpp>
#include <chronon3d/api/scene.hpp>
#include <chronon3d/api/renderer.hpp>
#include <chronon3d/core/frame_context.hpp>
#include <chronon3d/scene/utils/dark_grid_background.hpp>
#include <chronon3d/runtime/telemetry/telemetry_manager.hpp>
#include <cmath>
#include <algorithm>
#include <chrono>

using namespace chronon3d;

namespace {

void build_lil_dirk_clean_background(SceneBuilder& s, const FrameContext& ctx) {
    const float t = (ctx.duration > 1)
        ? static_cast<float>(ctx.frame) / static_cast<float>(ctx.duration - 1)
        : 0.0f;

    s.camera().set(camera_motion::parallax_sweep(t, 18.0f, -1000.0f, 1000.0f));

    DarkGridBgParams bg;
    bg.centered = true;
    bg.spacing = 120.0f;
    bg.bg_color = Color{0.02f, 0.02f, 0.025f, 1.0f};
    bg.grid_color = Color{0.34f, 0.34f, 0.36f, 0.18f};
    scene::utils::dark_grid_background(s, ctx, bg);

    s.layer("title", [](LayerBuilder& l) {
        l.enable_3d();
        l.text("title", TextParams{
            .text = "LIL DIRK CLEAN",
            .size = {820.0f, 160.0f},
            .pos = {0.0f, 0.0f, 0.0f},
            .font_path = "assets/fonts/Inter-Bold.ttf",
            .font_family = "Inter",
            .font_weight = 800,
            .font_style = "normal",
            .font_size = 96.0f,
            .color = Color{0.98f, 0.98f, 0.99f, 1.0f},
            .align = TextAlign::Center,
            .line_height = 1.0f,
            .tracking = 1.0f
        });
        l.glow(42.0f, 0.34f, Color{0.96f, 0.96f, 1.0f, 0.62f});
    });
}

Scene build_lil_dirk_clean_scene(const FrameContext& ctx) {
    SceneBuilder s(ctx);
    build_lil_dirk_clean_background(s, ctx);
    return s.build();
}

Composition LilDirkClean() {
    return composition({
        .name     = "LilDirkClean",
        .width    = 1920,
        .height   = 1080,
        .duration = 90
    }, [](const FrameContext& ctx) {
        return build_lil_dirk_clean_scene(ctx);
    });
}

} // namespace

#include <chronon3d/backends/image/image_writer.hpp>
#include <chronon3d/backends/image/stb_image_backend.hpp>
#include <filesystem>
#include <fmt/format.h>

TEST_CASE("Test Lil Dirk Clean Scene Rendering") {
    SoftwareRenderer renderer;
    RenderSettings settings;
    settings.use_modular_graph = true;
    renderer.set_settings(settings);
    renderer.set_image_backend(std::make_shared<image::StbImageBackend>());

    Composition comp = LilDirkClean();
    REQUIRE(comp.name() == "LilDirkClean");
    REQUIRE(comp.duration() == 90);
    REQUIRE(comp.width() == 1920);
    REQUIRE(comp.height() == 1080);

    auto fb = renderer.render_frame(comp, 45);
    REQUIRE(fb != nullptr);
    CHECK(fb->width() == 1920);
    CHECK(fb->height() == 1080);

    // Verify background is filled with a dark color
    auto sample_pixel = fb->get_pixel(10, 10);
    CHECK(sample_pixel.r >= 0.0f);
    CHECK(sample_pixel.r < 0.1f);
    CHECK(sample_pixel.g < 0.1f);
    CHECK(sample_pixel.b < 0.1f);
}

TEST_CASE("Render all frames of LilDirkClean and save as PNGs") {
    auto start_wall = std::chrono::high_resolution_clock::now();
    std::string started_at = telemetry::TelemetryManager::get_current_iso_time();

    SoftwareRenderer renderer;
    RenderSettings settings;
    settings.use_modular_graph = true;
    renderer.set_settings(settings);
    renderer.set_image_backend(std::make_shared<image::StbImageBackend>());

    Composition comp = LilDirkClean();
    
    std::filesystem::create_directories("output/seq");
    
    std::vector<telemetry::FrameTelemetryRecord> frame_records;
    frame_records.reserve(90);

    double total_render_ms = 0.0;

    for (int f = 0; f < 90; ++f) {
        auto start_frame = std::chrono::high_resolution_clock::now();
        auto fb = renderer.render_frame(comp, f);
        REQUIRE(fb != nullptr);
        
        std::string filename = fmt::format("output/seq/lil_dirk.{:04d}.png", f);
        save_png(*fb, filename);

        auto end_frame = std::chrono::high_resolution_clock::now();
        double frame_duration = std::chrono::duration<double, std::milli>(end_frame - start_frame).count();
        total_render_ms += frame_duration;

        telemetry::FrameTelemetryRecord fr;
        fr.frame_number = f;
        fr.duration_ms = frame_duration;
        fr.cache_hit = false;
        fr.dirty_area_ratio = 1.0;
        frame_records.push_back(fr);
    }

    auto end_wall = std::chrono::high_resolution_clock::now();
    double wall_duration = std::chrono::duration<double, std::milli>(end_wall - start_wall).count();

    // Initialize telemetry stores
    telemetry::TelemetryManager::instance().initialize_default_stores();

    // Prepare run record
    telemetry::RenderTelemetryRecord run;
    run.run_id = telemetry::TelemetryManager::generate_uuid();
    run.composition_id = "LilDirkClean";
    run.output_path = "output/lil_dirk_telemetry.mp4"; // Set to final video so web dashboard links it correctly!
    run.success = true;
    run.frames_total = 90;
    run.frames_written = 90;
    run.wall_time_ms = wall_duration;
    run.render_ms = total_render_ms;
    run.encode_ms = 0.0;
    run.effective_fps = wall_duration > 0.0 ? (90.0 * 1000.0 / wall_duration) : 0.0;
    run.started_at_iso = started_at;
    run.finished_at_iso = telemetry::TelemetryManager::get_current_iso_time();

    // Record the run!
    telemetry::TelemetryManager::instance().record_run(run, frame_records);
}
