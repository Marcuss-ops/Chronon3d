// tests/install_consumer/main_text.cpp
//
// ── FASE 2.2 — SDK consumer con testo reale (certificazione prodotto) ──
//
// Questo consumer USA SOLO Chronon3D::SDK (find_package + umbrella).
// NESSUN header interno: niente advanced/, internal.hpp, runtime.hpp, test/*.
//
// Dimostra che un progetto esterno può:
//   1. Usare `composition()` + `SceneBuilder` + `l.text()` per creare testo
//   2. Renderizzare con `sdk::RenderEngine`
//   3. Salvare un PNG con testo visibile (non solo background)
//
// ── Anti-false-green validation ──────────────────────────────────────────
// Per eliminare il rischio che il background mascheri un testo assente,
// il consumer esegue DUE render:
//   (a) Background-only (senza layer testo) → pixel di riferimento
//   (b) Con testo → pixel finali
// Se la differenza pixel-by-pixel è zero (soglia > 30/255 in almeno
// un canale) → FAIL: il testo non è visibile.

#include <chronon3d/chronon3d.hpp>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <vector>

namespace c3d = chronon3d;

// ── Helpers ──────────────────────────────────────────────────────────────

/// Bridge RenderOutput → vector<float> RGBA (4 floats per pixel).
/// Returns empty vector on failure.
static std::vector<float> bridge_to_float_rgba(const c3d::sdk::RenderOutput& out) {
    if (out.pixels == nullptr || out.width <= 0 || out.height <= 0) return {};
    if (out.format != c3d::sdk::PixelFormat::Rgba8 &&
        out.format != c3d::sdk::PixelFormat::Bgra8) return {};

    const size_t count = static_cast<size_t>(out.width) * out.height;
    std::vector<float> rgba(count * 4);
    const uint8_t* src = out.pixels;
    const bool is_bgra = (out.format == c3d::sdk::PixelFormat::Bgra8);
    const size_t row_stride = (out.bytes_per_row > 0)
        ? static_cast<size_t>(out.bytes_per_row)
        : static_cast<size_t>(out.width) * 4u;
    constexpr float inv = 1.0f / 255.0f;

    for (int32_t y = 0; y < out.height; ++y) {
        for (int32_t x = 0; x < out.width; ++x) {
            const size_t src_idx = static_cast<size_t>(y) * row_stride
                                 + static_cast<size_t>(x) * 4u;
            const size_t dst_idx = (static_cast<size_t>(y) * out.width + x) * 4;
            if (is_bgra) {
                rgba[dst_idx + 0] = src[src_idx + 2] * inv; // R
                rgba[dst_idx + 1] = src[src_idx + 1] * inv; // G
                rgba[dst_idx + 2] = src[src_idx + 0] * inv; // B
            } else {
                rgba[dst_idx + 0] = src[src_idx + 0] * inv; // R
                rgba[dst_idx + 1] = src[src_idx + 1] * inv; // G
                rgba[dst_idx + 2] = src[src_idx + 2] * inv; // B
            }
            rgba[dst_idx + 3] = 1.0f; // A always opaque
        }
    }
    return rgba;
}

/// Count pixels that differ between two RGBA buffers by > threshold
/// in at least one channel.
static size_t count_differing_pixels(const std::vector<float>& a,
                                     const std::vector<float>& b,
                                     float threshold) {
    if (a.size() != b.size()) return a.size() / 4; // size mismatch = all differ
    size_t count = 0;
    const size_t pixels = a.size() / 4;
    for (size_t i = 0; i < pixels; ++i) {
        const size_t off = i * 4;
        if (std::fabs(a[off+0] - b[off+0]) > threshold ||
            std::fabs(a[off+1] - b[off+1]) > threshold ||
            std::fabs(a[off+2] - b[off+2]) > threshold) {
            ++count;
        }
    }
    return count;
}

/// Build the background-only composition (no text layer).
static c3d::Composition make_bg_composition(const char* assets_root) {
    return c3d::composition(
        {.name = "sdk_text_bg_only",
         .width = 640, .height = 360,
         .frame_rate = c3d::FrameRate{30, 1},
         .duration = 1,
         .assets_root = assets_root},
        [](const c3d::FrameContext& ctx) -> c3d::Scene {
            c3d::SceneBuilder s(ctx);
            if (ctx.font_engine) s.font_engine(ctx.font_engine);

            // Background layer only
            s.layer("bg", [](c3d::LayerBuilder& l) {
                l.rect("bg_rect", c3d::RectParams{
                    .size = {640.0f, 360.0f},
                    .color = c3d::Color{0.2f, 0.2f, 0.3f, 1.0f},
                    .pos = {320.0f, 180.0f, 0.0f},
                    .fill = c3d::FillStyle::solid(c3d::Color{0.2f, 0.2f, 0.3f, 1.0f})
                });
            });
            return s.build();
        });
}

/// Build the full composition with text layer.
static c3d::Composition make_text_composition(const char* assets_root) {
    return c3d::composition(
        {.name = "sdk_text_consumer",
         .width = 640, .height = 360,
         .frame_rate = c3d::FrameRate{30, 1},
         .duration = 1,
         .assets_root = assets_root},
        [](const c3d::FrameContext& ctx) -> c3d::Scene {
            c3d::SceneBuilder s(ctx);
            if (ctx.font_engine) s.font_engine(ctx.font_engine);

            // Background layer
            s.layer("bg", [](c3d::LayerBuilder& l) {
                l.rect("bg_rect", c3d::RectParams{
                    .size = {640.0f, 360.0f},
                    .color = c3d::Color{0.2f, 0.2f, 0.3f, 1.0f},
                    .pos = {320.0f, 180.0f, 0.0f},
                    .fill = c3d::FillStyle::solid(c3d::Color{0.2f, 0.2f, 0.3f, 1.0f})
                });
            });

            // Text layer
            s.layer("title", [](c3d::LayerBuilder& l) {
                l.kind(c3d::LayerKind::Text);
                l.text("hello", c3d::TextSpec{.content = {.value = "TEXT EXPORT V1"}, .placement = c3d::TextPlacement{
                        c3d::TextPlacementKind::Absolute, {320.0f, 180.0f}}, .font = {.font_path = "fonts/Inter-Bold.ttf",
                             .font_family = "Inter",
                             .font_weight = 700,
                             .font_size = 48.0f}, .layout = {.box = {640.0f, 360.0f},
                               .align = c3d::TextAlign::Center,
                               .vertical_align = c3d::VerticalAlign::Middle}, .appearance = {.color = c3d::Color::white()}});
            });
            return s.build();
        });
}

// ── Main ─────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    const char* assets_root = (argc > 1) ? argv[1] : "assets";
    std::printf("[consumer-info] assets_root=%s\n", assets_root);

    // ── Common settings ─────────────────────────────────────────────
    c3d::sdk::RenderSettings settings{};
    settings.width = 640;
    settings.height = 360;
    settings.antialiasing_samples = 1;
    settings.deterministic = true;
    settings.dirty_rects = false;
    settings.motion_blur = false;
    settings.max_threads = 1;

    c3d::sdk::RenderEngine engine{settings};
    engine.set_assets_root(std::filesystem::path{assets_root});

    // ── Common camera descriptor ────────────────────────────────────
    auto make_cam = []() {
        c3d::camera_v1::CameraDescriptor cam{};
        cam.id = "main_camera";
        cam.base.enabled = true;
        cam.base.position = c3d::Vec3{0.0f, 0.0f, -800.0f};
        cam.base.rotation = c3d::Vec3{0.0f, 180.0f, 0.0f};
        cam.base.projection = c3d::camera_v1::ZoomProjection{};
        cam.failure_policy = c3d::camera_v1::CameraFailurePolicy::Stop;
        return cam;
    };

    // ═══════════════════════════════════════════════════════════════
    // PASS 1: Background-only render (baseline)
    // ═══════════════════════════════════════════════════════════════
    auto bg_comp = make_bg_composition(assets_root);
    bg_comp.default_camera_descriptor(make_cam());
    bg_comp.camera.transform.position = c3d::Vec3{0.0f, 0.0f, -1000.0f};
    bg_comp.camera.set_rotation_euler(c3d::Vec3{0.0f, 180.0f, 0.0f});

    auto bg_result = engine.render(bg_comp, c3d::sdk::Frame{0});
    if (!bg_result.has_value()) {
        std::fprintf(stderr, "[TEXT-FAIL] background render failed: %s\n",
                     bg_result.error().message.c_str());
        return 1;
    }

    auto bg_pixels = bridge_to_float_rgba(bg_result.value());
    if (bg_pixels.empty()) {
        std::fprintf(stderr, "[TEXT-FAIL] background pixel bridge failed\n");
        return 1;
    }
    std::fprintf(stderr, "[consumer-diag] background render: %dx%d, %zu pixels\n",
                 bg_result.value().width, bg_result.value().height,
                 bg_pixels.size() / 4);

    // ═══════════════════════════════════════════════════════════════
    // PASS 2: Full render with text
    // ═══════════════════════════════════════════════════════════════
    auto text_comp = make_text_composition(assets_root);
    text_comp.default_camera_descriptor(make_cam());
    text_comp.camera.transform.position = c3d::Vec3{0.0f, 0.0f, -1000.0f};
    text_comp.camera.set_rotation_euler(c3d::Vec3{0.0f, 180.0f, 0.0f});

    auto text_result = engine.render(text_comp, c3d::sdk::Frame{0});
    if (!text_result.has_value()) {
        std::fprintf(stderr, "[TEXT-FAIL] text render failed: %s\n",
                     text_result.error().message.c_str());
        return 1;
    }

    const c3d::sdk::RenderOutput& out = text_result.value();
    if (out.pixels == nullptr || out.width <= 0 || out.height <= 0) {
        std::fprintf(stderr, "[TEXT-FAIL] empty render output\n");
        return 1;
    }

    auto text_pixels = bridge_to_float_rgba(out);
    if (text_pixels.empty()) {
        std::fprintf(stderr, "[TEXT-FAIL] text pixel bridge failed\n");
        return 1;
    }

    // ═══════════════════════════════════════════════════════════════
    // ANTI-FALSE-GREEN: compare background-only vs with-text
    // ═══════════════════════════════════════════════════════════════
    constexpr float kDeltaThreshold = 30.0f / 255.0f;  // ~0.118
    const size_t total_pixels = text_pixels.size() / 4;
    const size_t delta_count = count_differing_pixels(bg_pixels, text_pixels, kDeltaThreshold);

    std::fprintf(stderr, "[consumer-diag] anti-false-green: %zu/%zu pixels differ by >%.1f/255\n",
                 delta_count, total_pixels, kDeltaThreshold * 255.0f);

    if (delta_count == 0) {
        std::fprintf(stderr,
                     "[TEXT-FAIL] anti-false-green: ZERO pixels differ between "
                     "background-only and with-text renders. Text is NOT visible.\n");
        return 1;
    }

    // ═══════════════════════════════════════════════════════════════
    // LEGACY CHECK: also verify some pixels are "light" (white text)
    // ═══════════════════════════════════════════════════════════════
    constexpr float kLight = 200.0f / 255.0f;
    size_t light_pixels = 0;
    for (size_t i = 0; i < total_pixels; ++i) {
        const size_t off = i * 4;
        if (text_pixels[off+0] + text_pixels[off+1] + text_pixels[off+2] > kLight * 3.0f) {
            ++light_pixels;
        }
    }

    // ═══════════════════════════════════════════════════════════════
    // Save output PNG (from text render)
    // ═══════════════════════════════════════════════════════════════
    c3d::Framebuffer fb{out.width, out.height};
    const uint8_t* src = out.pixels;
    const bool is_bgra = (out.format == c3d::sdk::PixelFormat::Bgra8);
    const size_t row_stride = (out.bytes_per_row > 0)
        ? static_cast<size_t>(out.bytes_per_row)
        : static_cast<size_t>(out.width) * 4u;
    constexpr float inv = 1.0f / 255.0f;

    for (int32_t y = 0; y < out.height; ++y) {
        for (int32_t x = 0; x < out.width; ++x) {
            const size_t idx = static_cast<size_t>(y) * row_stride
                             + static_cast<size_t>(x) * 4u;
            c3d::Color c;
            if (is_bgra) {
                c = c3d::Color{src[idx+2]*inv, src[idx+1]*inv, src[idx+0]*inv, 1.0f};
            } else {
                c = c3d::Color{src[idx+0]*inv, src[idx+1]*inv, src[idx+2]*inv, 1.0f};
            }
            fb.set_pixel(x, y, c);
        }
    }

    const char* output_path = "sdk_text_consumer_output.png";
    if (!c3d::save_png(fb, output_path)) {
        std::fprintf(stderr, "[TEXT-FAIL] save_png failed\n");
        return 1;
    }

    const auto file_size = std::filesystem::file_size(output_path);

    // ═══════════════════════════════════════════════════════════════
    // [TEXT-OK] marker — all checks passed
    // ═══════════════════════════════════════════════════════════════
    std::printf("[TEXT-OK] rendered %dx%d PNG (%zu bytes, "
                "%zu/%zu delta pixels, %zu/%zu light pixels)\n",
                out.width, out.height,
                static_cast<size_t>(file_size),
                delta_count, total_pixels,
                light_pixels, total_pixels);
    return 0;
}
