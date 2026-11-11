// tests/install_consumer/main.cpp
//
// Installed-SDK boundary consumer on the canonical typed lane.
//
// Contract (RELEASE_GATE.md §SDK Product V1 / 8 — P3-H):
//   (a) Includes ONLY headers listed in cmake/Chronon3DPublicHeaders.cmake
//   (b) Calls ONLY sdk::RenderEngine::compile_plan_json() +
//       sdk::RenderEngine::render_program() + save_png + public value types
//   (c) RenderPlan with a shape background + a text layer
//   (d) Output PNG via save_png
//   (e) Pixel-hash check: fails if every pixel is below 5/255
//
// This is a STANDALONE consumer project — it does NOT share
// tests/CMakeLists.txt and does NOT link against in-tree targets.
// Its only dependency is the *installed* Chronon3D package.

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
  "job_id": "p3h_consumer",
  "canvas": { "width": 640, "height": 360, "fps_num": 30, "fps_den": 1, "duration_frames": 1 },
  "output": { "path": "sdk_consumer_output.png", "format": "png" },
  "layers": [
    {
      "id": "background",
      "type": "shape",
      "size": [640, 360],
      "position": [320, 180],
      "start_frame": 0,
      "duration_frames": 1,
      "shape": { "type": "rect", "fill": [0.2, 0.2, 0.3, 1.0] }
    },
    {
      "id": "title",
      "type": "text",
      "text": "BOUNDARY CHECK",
      "size": [640, 360],
      "position": [320, 180],
      "start_frame": 0,
      "duration_frames": 1,
      "style": { "font": "fonts/Inter-Bold.ttf", "font_size": 48, "fill": "#FFFFFF" }
    }
  ]
})JSON";

} // namespace

int main(int argc, char* argv[]) {
    const char* assets_root = (argc > 1) ? argv[1] : "assets";

    c3d::sdk::RenderSettings settings{};
    settings.width = 640;
    settings.height = 360;
    settings.antialiasing_samples = 1;
    settings.deterministic = true;
    settings.dirty_rects = false;
    settings.motion_blur = false;

    if (!std::filesystem::is_directory(assets_root)) {
        std::fprintf(stderr,
                     "[consumer-warn] assets_root '%s' is not a directory "
                     "— text layer may render blank, but the shape background "
                     "keeps the pixel-hash >= 5/255.\n",
                     assets_root);
    }

    c3d::sdk::RenderEngine engine{settings};
    engine.set_assets_root(std::filesystem::path{assets_root});

    auto program = consumer::compile_plan(engine, kPlanJson, "boundary");
    if (!program) {
        return 1;
    }

    auto result = engine.render_program(*program, c3d::sdk::Frame{0});
    if (!result.has_value()) {
        std::fprintf(stderr,
                     "[BOUNDARY-FAIL] sdk::RenderEngine::render_program failed: "
                     "code=%s message=%s\n",
                     c3d::sdk::render_error_code_name(result.error().code),
                     result.error().message.c_str());
        return 1;
    }

    const c3d::sdk::RenderOutput& out = result.value();
    if (out.pixels == nullptr || out.width <= 0 || out.height <= 0) {
        std::fprintf(stderr, "[BOUNDARY-FAIL] render returned empty output\n");
        return 1;
    }
    if (out.format != c3d::sdk::PixelFormat::RGBA8) {
        std::fprintf(stderr, "[BOUNDARY-FAIL] unsupported PixelFormat enum=%d\n",
                     static_cast<int>(out.format));
        return 1;
    }

    c3d::Framebuffer fb{out.width, out.height};
    consumer::copy_pixels(out, fb);

    // Pixel-hash check (≥ 5/255 on ≥ 1 pixel).
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
                     "[BOUNDARY-FAIL] all %zu pixels below 5/255 — "
                     "output PNG would be black\n",
                     fb.pixel_count());
        return 1;
    }

    const std::filesystem::path output_path = "sdk_consumer_output.png";
    if (!c3d::save_png(fb, output_path.string())) {
        std::fprintf(stderr, "[BOUNDARY-FAIL] save_png failed for %s\n",
                     output_path.string().c_str());
        return 1;
    }
    if (!std::filesystem::exists(output_path)
        || std::filesystem::file_size(output_path) == 0) {
        std::fprintf(stderr, "[BOUNDARY-FAIL] output PNG missing/empty: %s\n",
                     output_path.string().c_str());
        return 1;
    }

    const auto file_size = std::filesystem::file_size(output_path);
    std::printf("[BOUNDARY-OK] sdk consumer (P3-H) rendered %dx%d PNG "
                "(%zu bytes, %zu/%zu pixels >5/255, format=Rgba8)\n",
                out.width, out.height,
                static_cast<std::size_t>(file_size),
                nonzero_count, fb.pixel_count());
    return 0;
}
