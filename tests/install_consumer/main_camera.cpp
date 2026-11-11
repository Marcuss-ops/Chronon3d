// =============================================================================
// tests/install_consumer/main_camera.cpp
//
// Installed-SDK consumer using a resolved camera in a RenderPlan.
//
// Contract (P3-H strict):
//   (a) Includes ONLY headers listed in cmake/Chronon3DPublicHeaders.cmake
//   (b) Calls ONLY `sdk::RenderEngine::compile_plan_json()` +
//       `sdk::RenderEngine::render_program()` + `chronon3d::save_png(...)`
//   (c) Uses the plan's resolved camera block as renderer input
//   (d) Output PNG via save_png
//   (e) Pixel-hash check: fails if every pixel is below 5/255
//   (f) MUST NOT see `CameraSession` or `RenderGraph` — no internal header
//
// Output marker: `[CAMERA-OK]` on success (consumed by
// tools/sdk/run_external_consumer.sh).
// =============================================================================

#include "consumer_plan.hpp"

#include <chronon3d/backends/image/image_writer.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/sdk/render_engine.hpp>
#include <chronon3d/sdk/render_output.hpp>
#include <chronon3d/sdk/render_settings.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>

namespace c3d = chronon3d;

namespace {

constexpr const char* kPlanJson = R"JSON({
  "schema": "chronon.render-plan.v3",
  "version": 3,
  "job_id": "p3h_camera_consumer",
  "camera": {
    "type": "perspective",
    "position": [0, 0, -1200],
    "rotation_deg": [0, 180, 0],
    "focal_length_mm": 50,
    "sensor_width_mm": 36,
    "sensor_height_mm": 24,
    "f_stop": 2.8,
    "near": 1,
    "far": 10000
  },
  "canvas": { "width": 1920, "height": 1080, "fps_num": 30, "fps_den": 1, "duration_frames": 60 },
  "output": { "path": "sdk_camera_consumer_output.png", "format": "png" },
  "layers": [
    {
      "id": "background",
      "type": "shape",
      "size": [1920, 1080],
      "position": [960, 540],
      "start_frame": 0,
      "duration_frames": 60,
      "shape": { "type": "rect", "fill": [0.05, 0.05, 0.07, 1.0] }
    },
    {
      "id": "title",
      "type": "text",
      "text": "PERSPECTIVE OK",
      "size": [1920, 1080],
      "position": [960, 540],
      "start_frame": 0,
      "duration_frames": 60,
      "style": { "font": "fonts/Inter-Bold.ttf", "font_size": 64, "fill": "#FFFFFF" }
    }
  ]
})JSON";

} // namespace

int main(int argc, char* argv[]) {
    const char* assets_root = (argc > 1) ? argv[1] : "assets";

    c3d::sdk::RenderSettings settings{};
    settings.width = 1920;
    settings.height = 1080;
    settings.antialiasing_samples = 1;
    settings.deterministic = true;
    settings.dirty_rects = false;
    settings.motion_blur = false;

    c3d::sdk::RenderEngine engine{settings};
    engine.set_assets_root(std::filesystem::path{assets_root});

    auto program = consumer::compile_plan(engine, kPlanJson, "camera");
    if (!program) {
        return 1;
    }

    auto result = engine.render_program(*program, c3d::sdk::Frame{30});
    if (!result.has_value()) {
        std::fprintf(stderr,
                     "[CAMERA-FAIL] sdk::RenderEngine::render_program failed: "
                     "code=%s message=%s\n",
                     c3d::sdk::render_error_code_name(result.error().code),
                     result.error().message.c_str());
        return 1;
    }

    const c3d::sdk::RenderOutput& out = result.value();
    if (out.pixels == nullptr || out.width <= 0 || out.height <= 0) {
        std::fprintf(stderr, "[CAMERA-FAIL] render returned empty output\n");
        return 1;
    }

    c3d::Framebuffer fb{out.width, out.height};
    consumer::copy_pixels(out, fb);

    constexpr float kThreshold = 5.0f / 255.0f;
    std::size_t nonzero_count = 0;
    const c3d::Color* fdata = fb.data();
    for (std::size_t i = 0; i < fb.pixel_count(); ++i) {
        const c3d::Color c = fdata[i];
        if (c.r > kThreshold || c.g > kThreshold || c.b > kThreshold) {
            ++nonzero_count;
        }
    }
    if (nonzero_count == 0) {
        std::fprintf(stderr,
                     "[CAMERA-FAIL] all %zu pixels below 5/255 — "
                     "output PNG would be black\n",
                     fb.pixel_count());
        return 1;
    }

    const std::filesystem::path output_path = "sdk_camera_consumer_output.png";
    if (!c3d::save_png(fb, output_path.string())) {
        std::fprintf(stderr, "[CAMERA-FAIL] save_png failed for %s\n",
                     output_path.string().c_str());
        return 1;
    }
    if (!std::filesystem::exists(output_path)
        || std::filesystem::file_size(output_path) == 0) {
        std::fprintf(stderr, "[CAMERA-FAIL] output PNG missing/empty: %s\n",
                     output_path.string().c_str());
        return 1;
    }

    const auto file_size = std::filesystem::file_size(output_path);
    std::printf("[CAMERA-OK] public camera facade consumer rendered %dx%d "
                "PNG (%zu bytes, %zu/%zu pixels >5/255, "
                "scene=plan camera perspective, format=Rgba8)\n",
                out.width, out.height,
                static_cast<std::size_t>(file_size),
                nonzero_count, fb.pixel_count());
    return 0;
}
