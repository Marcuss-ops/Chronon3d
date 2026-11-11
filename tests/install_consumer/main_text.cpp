// tests/install_consumer/main_text.cpp
//
// Installed-SDK text consumer (Text Export V1 certifier) on the typed lane.
// Uses ONLY Chronon3D::SDK: no advanced/, internal.hpp, runtime.hpp, test/*.
//
// Two RenderPlan documents are compiled and rendered:
//   (a) background-only rect → reference pixels
//   (b) the same rect + a text layer → final pixels
// If no pixel differs by more than 30/255 in at least one channel, the text
// is not visible and the consumer fails (anti-false-green).

#include "consumer_plan.hpp"

#include <chronon3d/backends/image/image_writer.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/sdk/render_engine.hpp>
#include <chronon3d/sdk/render_output.hpp>
#include <chronon3d/sdk/render_settings.hpp>

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace c3d = chronon3d;

namespace {

constexpr const char* kBackgroundPlan = R"JSON({
  "schema": "chronon.render-plan.v3",
  "version": 3,
  "job_id": "sdk_text_bg_only",
  "canvas": { "width": 640, "height": 360, "fps_num": 30, "fps_den": 1, "duration_frames": 1 },
  "output": { "path": "sdk_text_bg_only.png", "format": "png" },
  "layers": [
    {
      "id": "bg",
      "type": "shape",
      "size": [640, 360],
      "position": [320, 180],
      "start_frame": 0,
      "duration_frames": 1,
      "shape": { "type": "rect", "fill": [0.2, 0.2, 0.3, 1.0] }
    }
  ]
})JSON";

constexpr const char* kTextPlan = R"JSON({
  "schema": "chronon.render-plan.v3",
  "version": 3,
  "job_id": "sdk_text_consumer",
  "canvas": { "width": 640, "height": 360, "fps_num": 30, "fps_den": 1, "duration_frames": 1 },
  "output": { "path": "sdk_text_consumer_output.png", "format": "png" },
  "layers": [
    {
      "id": "bg",
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
      "text": "TEXT EXPORT V1",
      "size": [640, 360],
      "position": [320, 180],
      "start_frame": 0,
      "duration_frames": 1,
      "style": { "font": "fonts/Inter-Bold.ttf", "font_size": 48, "fill": "#FFFFFF" }
    }
  ]
})JSON";

bool render_to_float_rgba(c3d::sdk::RenderEngine& engine, const char* json,
                          const char* label, std::vector<float>& rgba) {
    auto program = consumer::compile_plan(engine, json, label);
    if (!program) return false;

    auto result = engine.render_program(*program, c3d::sdk::Frame{0});
    if (!result.has_value()) {
        std::fprintf(stderr, "[TEXT-FAIL] %s render failed: %s\n",
                     label, result.error().message.c_str());
        return false;
    }

    const c3d::sdk::RenderOutput& out = result.value();
    if (out.pixels == nullptr || out.width <= 0 || out.height <= 0 ||
        out.format != c3d::sdk::PixelFormat::RGBA8) {
        std::fprintf(stderr, "[TEXT-FAIL] %s returned an unusable output\n", label);
        return false;
    }

    const std::size_t count = static_cast<std::size_t>(out.width) * out.height;
    rgba.assign(count * 4, 0.0f);
    const std::uint8_t* src = out.pixels;
    const std::size_t row_stride = out.bytes_per_row > 0
        ? static_cast<std::size_t>(out.bytes_per_row)
        : static_cast<std::size_t>(out.width) * 4u;
    constexpr float inv = 1.0f / 255.0f;

    for (std::int32_t y = 0; y < out.height; ++y) {
        for (std::int32_t x = 0; x < out.width; ++x) {
            const std::size_t src_idx = static_cast<std::size_t>(y) * row_stride
                + static_cast<std::size_t>(x) * 4u;
            const std::size_t dst_idx =
                (static_cast<std::size_t>(y) * out.width + x) * 4;
            rgba[dst_idx + 0] = src[src_idx + 0] * inv;
            rgba[dst_idx + 1] = src[src_idx + 1] * inv;
            rgba[dst_idx + 2] = src[src_idx + 2] * inv;
            rgba[dst_idx + 3] = 1.0f;
        }
    }
    return true;
}

std::size_t count_differing_pixels(const std::vector<float>& a,
                                   const std::vector<float>& b,
                                   float threshold) {
    if (a.size() != b.size()) return a.size() / 4;
    std::size_t count = 0;
    const std::size_t pixels = a.size() / 4;
    for (std::size_t i = 0; i < pixels; ++i) {
        const std::size_t off = i * 4;
        if (std::fabs(a[off + 0] - b[off + 0]) > threshold ||
            std::fabs(a[off + 1] - b[off + 1]) > threshold ||
            std::fabs(a[off + 2] - b[off + 2]) > threshold) {
            ++count;
        }
    }
    return count;
}

} // namespace

int main(int argc, char* argv[]) {
    const char* assets_root = (argc > 1) ? argv[1] : "assets";
    std::printf("[consumer-info] assets_root=%s\n", assets_root);

    c3d::sdk::RenderSettings settings{};
    settings.width = 640;
    settings.height = 360;
    settings.antialiasing_samples = 1;
    settings.deterministic = true;
    settings.dirty_rects = false;
    settings.motion_blur = false;

    c3d::sdk::RenderEngine engine{settings};
    engine.set_assets_root(std::filesystem::path{assets_root});

    // PASS 1: background-only render (baseline).
    std::vector<float> bg_pixels;
    if (!render_to_float_rgba(engine, kBackgroundPlan, "background", bg_pixels)) {
        return 1;
    }
    std::fprintf(stderr, "[consumer-diag] background render: %zu pixels\n",
                 bg_pixels.size() / 4);

    // PASS 2: render with text.
    std::vector<float> text_pixels;
    if (!render_to_float_rgba(engine, kTextPlan, "text", text_pixels)) {
        return 1;
    }

    // ANTI-FALSE-GREEN: background-only vs with-text.
    constexpr float kDeltaThreshold = 30.0f / 255.0f;
    const std::size_t total_pixels = text_pixels.size() / 4;
    const std::size_t delta_count =
        count_differing_pixels(bg_pixels, text_pixels, kDeltaThreshold);

    std::fprintf(stderr,
                 "[consumer-diag] anti-false-green: %zu/%zu pixels differ by >%.1f/255\n",
                 delta_count, total_pixels, kDeltaThreshold * 255.0f);

    if (delta_count == 0) {
        std::fprintf(stderr,
                     "[TEXT-FAIL] anti-false-green: ZERO pixels differ between "
                     "background-only and with-text renders. Text is NOT visible.\n");
        return 1;
    }

    constexpr float kLight = 200.0f / 255.0f;
    std::size_t light_pixels = 0;
    for (std::size_t i = 0; i < total_pixels; ++i) {
        const std::size_t off = i * 4;
        if (text_pixels[off + 0] + text_pixels[off + 1] + text_pixels[off + 2] >
            kLight * 3.0f) {
            ++light_pixels;
        }
    }

    // Save the text render.
    auto program = consumer::compile_plan(engine, kTextPlan, "text-save");
    if (!program) return 1;
    auto result = engine.render_program(*program, c3d::sdk::Frame{0});
    if (!result.has_value()) {
        std::fprintf(stderr, "[TEXT-FAIL] text re-render failed: %s\n",
                     result.error().message.c_str());
        return 1;
    }
    const c3d::sdk::RenderOutput& out = result.value();
    c3d::Framebuffer fb{out.width, out.height};
    consumer::copy_pixels(out, fb);

    const char* output_path = "sdk_text_consumer_output.png";
    if (!c3d::save_png(fb, output_path)) {
        std::fprintf(stderr, "[TEXT-FAIL] save_png failed\n");
        return 1;
    }

    const auto file_size = std::filesystem::file_size(output_path);
    std::printf("[TEXT-OK] rendered %dx%d PNG (%zu bytes, "
                "%zu/%zu delta pixels, %zu/%zu light pixels)\n",
                out.width, out.height,
                static_cast<std::size_t>(file_size),
                delta_count, total_pixels,
                light_pixels, total_pixels);
    return 0;
}
