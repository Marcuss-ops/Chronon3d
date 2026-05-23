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
#include <chronon3d/core/render_telemetry.hpp>
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
    bg.bg_color = Color{0.0f, 0.0f, 0.0f, 1.0f}; // Pure black background
    bg.grid_color = Color{1.0f, 1.0f, 1.0f, 0.35f}; // Prominent pure white grid lines

    const float W = 1920.0f;
    const float H = 1080.0f;
    const float margin = 120.0f;
    const auto grid_path = scene::utils::detail::ensure_dark_grid_background_image(
        static_cast<int>(W + margin), static_cast<int>(H + margin), bg
    );

    s.layer("nbg_bg", [grid_path, W, H, margin](LayerBuilder& l) {
        l.cache_static();
        l.image("grid_bg", {
            .path = grid_path.string(),
            .size = {W + margin, H + margin},
            .pos = {-margin * 0.5f, -margin * 0.5f, 0.0f},
            .opacity = 1.0f
        });
    });

    s.layer("title", [](LayerBuilder& l) {
        l.enable_3d();
        l.cache_static();
        l.text("title", TextParams{
            .text = "LIL DIRK CLEAN",
            .size = {1280.0f, 300.0f}, // Expanded bounding box to accommodate glow radius completely
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

void record_telemetry_helper(const SoftwareRenderer& renderer, telemetry::RenderTelemetryRecord& run, const std::vector<telemetry::FrameTelemetryRecord>& frame_records, double total_render_ms) {
    const auto* counters = renderer.counters();
    
    run.pixels_touched = counters->pixels_touched.load(std::memory_order_relaxed);
    run.cache_hits = counters->cache_hits.load(std::memory_order_relaxed);
    run.cache_misses = counters->cache_misses.load(std::memory_order_relaxed);
    run.nodes_executed = counters->nodes_executed.load(std::memory_order_relaxed);
    run.layers_rendered = counters->layers_rendered.load(std::memory_order_relaxed);
    run.text_glyphs_rasterized = counters->text_glyphs_rasterized.load(std::memory_order_relaxed);
    run.images_sampled = counters->images_sampled.load(std::memory_order_relaxed);
    run.blur_pixels = counters->blur_pixels.load(std::memory_order_relaxed);
    run.simd_lerp_calls = counters->simd_lerp_calls.load(std::memory_order_relaxed);
    run.tiles_total = counters->tiles_total.load(std::memory_order_relaxed);
    run.tiles_hit = counters->tiles_hit.load(std::memory_order_relaxed);
    run.tiles_miss = counters->tiles_miss.load(std::memory_order_relaxed);
    run.tiles_partial = counters->tiles_partial.load(std::memory_order_relaxed);
    run.bytes_allocated_peak = telemetry::TelemetryManager::get_peak_memory_usage();
    run.node_cache_hash_collisions = counters->node_cache_hash_collisions.load(std::memory_order_relaxed);
    run.clear_calls = counters->clear_calls.load(std::memory_order_relaxed);
    run.clear_pixels = counters->clear_pixels.load(std::memory_order_relaxed);
    run.composite_calls = counters->composite_calls.load(std::memory_order_relaxed);
    run.composite_pixels = counters->composite_pixels.load(std::memory_order_relaxed);
    run.transform_calls = counters->transform_calls.load(std::memory_order_relaxed);
    run.transform_pixels = counters->transform_pixels.load(std::memory_order_relaxed);
    run.effect_stack_calls = counters->effect_stack_calls.load(std::memory_order_relaxed);
    run.effect_pixels = counters->effect_pixels.load(std::memory_order_relaxed);
    run.layer_culling_tests = counters->layer_culling_tests.load(std::memory_order_relaxed);
    run.layers_culled = counters->layers_culled.load(std::memory_order_relaxed);
    run.layers_visible = counters->layers_visible.load(std::memory_order_relaxed);
    run.framebuffer_allocations = counters->framebuffer_allocations.load(std::memory_order_relaxed);
    run.framebuffer_reuses = counters->framebuffer_reuses.load(std::memory_order_relaxed);
    run.framebuffer_bytes_allocated = counters->framebuffer_bytes_allocated.load(std::memory_order_relaxed);
    run.framebuffer_bytes_peak = counters->framebuffer_bytes_peak.load(std::memory_order_relaxed);
    run.dirty_rect_count = counters->dirty_rect_count.load(std::memory_order_relaxed);
    run.dirty_pixels = counters->dirty_pixels.load(std::memory_order_relaxed);
    run.dirty_union_area_pixels = counters->dirty_union_area_pixels.load(std::memory_order_relaxed);
    run.dirty_full_fallbacks = counters->dirty_full_fallbacks.load(std::memory_order_relaxed);
    run.bypass_not_cacheable_count = counters->bypass_not_cacheable_count.load(std::memory_order_relaxed);

    std::vector<telemetry::CounterTelemetryRecord> counters_list = {
        {"pixels_touched", run.pixels_touched},
        {"cache_hits", run.cache_hits},
        {"cache_misses", run.cache_misses},
        {"nodes_executed", run.nodes_executed},
        {"layers_rendered", run.layers_rendered},
        {"text_glyphs_rasterized", run.text_glyphs_rasterized},
        {"images_sampled", run.images_sampled},
        {"blur_pixels", run.blur_pixels},
        {"simd_lerp_calls", run.simd_lerp_calls},
        {"tiles_total", run.tiles_total},
        {"tiles_hit", run.tiles_hit},
        {"tiles_miss", run.tiles_miss},
        {"tiles_partial", run.tiles_partial},
        {"bytes_allocated_peak", run.bytes_allocated_peak},
        {"node_cache_hash_collisions", run.node_cache_hash_collisions},
        {"clear_calls", run.clear_calls},
        {"clear_pixels", run.clear_pixels},
        {"composite_calls", run.composite_calls},
        {"composite_pixels", run.composite_pixels},
        {"transform_calls", run.transform_calls},
        {"transform_pixels", run.transform_pixels},
        {"effect_stack_calls", run.effect_stack_calls},
        {"effect_pixels", run.effect_pixels},
        {"layer_culling_tests", run.layer_culling_tests},
        {"layers_culled", run.layers_culled},
        {"layers_visible", run.layers_visible},
        {"framebuffer_allocations", run.framebuffer_allocations},
        {"framebuffer_reuses", run.framebuffer_reuses},
        {"framebuffer_bytes_allocated", run.framebuffer_bytes_allocated},
        {"framebuffer_bytes_peak", run.framebuffer_bytes_peak},
        {"dirty_rect_count", run.dirty_rect_count},
        {"dirty_pixels", run.dirty_pixels},
        {"dirty_union_area_pixels", run.dirty_union_area_pixels},
        {"dirty_full_fallbacks", run.dirty_full_fallbacks},
        {"bypass_not_cacheable_count", run.bypass_not_cacheable_count}
    };

    std::vector<telemetry::PhaseTelemetryRecord> phases = {
        {"rendering_loop", total_render_ms}
    };

    telemetry::TelemetryManager::instance().record_run(run, frame_records, phases, counters_list);
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
    settings.tile_size = 256;
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
    CHECK(sample_pixel.r < 0.15f);
    CHECK(sample_pixel.g < 0.15f);
    CHECK(sample_pixel.b < 0.2f);
}

TEST_CASE("Render all frames of LilDirkClean and save as PNGs") {
    SoftwareRenderer renderer;
    RenderSettings settings;
    settings.use_modular_graph = true;
    settings.tile_size = 256;
    renderer.set_settings(settings);
    renderer.set_image_backend(std::make_shared<image::StbImageBackend>());

    Composition comp = LilDirkClean();
    
    // Warmup render of a frame to load fonts, textures, allocate pools, etc.
    {
        auto warmup_fb = renderer.render_frame(comp, 0);
        // Reset thread-local trace & counters after warmup
        renderer.trace()->clear();
        renderer.counters()->reset();
    }

    auto start_wall = std::chrono::high_resolution_clock::now();
    std::string started_at = telemetry::TelemetryManager::get_current_iso_time();
    
    std::filesystem::create_directories("output/seq");
    
    std::vector<telemetry::FrameTelemetryRecord> frame_records;
    frame_records.reserve(90);

    double total_render_ms = 0.0;

    for (int f = 0; f < 90; ++f) {
        auto start_frame = std::chrono::high_resolution_clock::now();
        spdlog::info("About to render frame {}...", f);
        auto fb = renderer.render_frame(comp, f);
        REQUIRE(fb != nullptr);
        
        std::string filename = fmt::format("output/seq/lil_dirk.{:04d}.png", f);
        spdlog::info("About to save frame {} to {}...", f, filename);
        save_png(*fb, filename);
        spdlog::info("Finished frame {} successfully.", f);

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

    // Prepare run record using actual hardware performance counters
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
    record_telemetry_helper(renderer, run, frame_records, total_render_ms);
}
