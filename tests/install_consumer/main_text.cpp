// tests/install_consumer/main_text.cpp
//
// ── FASE 2.2 — SDK consumer con testo reale (certificazione prodotto) ──
//
// Questo consumer USA SOLO Chronon3D::SDK (find_package + umbrella).
// NESSUN header interno: niente advanced/, internal.hpp, runtime.hpp, test/*.
//
// Dimostra che un progetto esterno può:
//   1. Usare `composition()` + `SceneBuilder` + `l.text_run()` per creare testo
//   2. Renderizzare con `sdk::RenderEngine`
//   3. Salvare un PNG con testo visibile (non solo background)

#include <chronon3d/chronon3d.hpp>

#include <cstdio>
#include <filesystem>
#include <memory>

namespace c3d = chronon3d;

int main(int argc, char* argv[]) {
    // ── 0. Assets root: CLI arg or default ───────────────────────
    const char* assets_root = (argc > 1) ? argv[1] : "assets";
    std::printf("[consumer-info] assets_root=%s\n", assets_root);

    // ── 1. Composition con testo via canonical authoring API ─────────
    // ── 1a. Camera descriptor (required for 3D pipeline — default camera
    //         faces -Z but our scene lives at Z=0, so without this
    //         rotation everything is frustum-culled → all-black). ───
    c3d::camera_v1::CameraDescriptor cam_desc{};
    cam_desc.id = "main_camera";
    cam_desc.base.enabled = true;
    cam_desc.base.position = c3d::Vec3{0.0f, 0.0f, -800.0f};
    cam_desc.base.rotation = c3d::Vec3{0.0f, 180.0f, 0.0f};
    cam_desc.base.projection = c3d::camera_v1::ZoomProjection{};
    cam_desc.failure_policy = c3d::camera_v1::CameraFailurePolicy::Stop;

    auto comp = c3d::composition(
        {.name = "sdk_text_consumer",
         .width = 640,
         .height = 360,
         .frame_rate = c3d::FrameRate{30, 1},
         .duration = 1,
         .assets_root = assets_root},
        [](const c3d::FrameContext& ctx) -> c3d::Scene {
            c3d::SceneBuilder s(ctx);

            // Forward font_engine from render pipeline so text can be shaped
            if (ctx.font_engine) {
                s.font_engine(ctx.font_engine);
            }

            // Background layer
            s.layer("bg", [](c3d::LayerBuilder& l) {
                l.rect("bg_rect", c3d::RectParams{
                    .size = {640.0f, 360.0f},
                    .color = c3d::Color{0.2f, 0.2f, 0.3f, 1.0f},
                    .pos = {320.0f, 180.0f, 0.0f},
                    .fill = c3d::FillStyle::solid(c3d::Color{0.2f, 0.2f, 0.3f, 1.0f})
                });
            });

            // Layer testo centrato — use l.text() (simpler API used by in-tree tests)
            s.layer("title", [](c3d::LayerBuilder& l) {
                l.kind(c3d::LayerKind::Text);
                l.text("hello", c3d::TextSpec{
                    .content = {.value = "Hello Chronon3D"},
                    .font = {.font_path = "fonts/Inter-Bold.ttf",
                             .font_family = "Inter",
                             .font_weight = 700,
                             .font_size = 48.0f},
                    .layout = {.box = {640.0f, 360.0f},
                               .align = c3d::TextAlign::Center,
                               .vertical_align = c3d::VerticalAlign::Middle},
                    .appearance = {.color = c3d::Color::white()},
                    .position = {320.0f, 180.0f, 0.0f}
                });
            });

            return s.build();
        });

    comp.default_camera_descriptor(std::move(cam_desc));
    // Legacy camera fallback (P3-F bridge): rotate 180° to face +Z
    comp.camera.transform.position = c3d::Vec3{0.0f, 0.0f, -1000.0f};
    comp.camera.set_rotation_euler(c3d::Vec3{0.0f, 180.0f, 0.0f});

    // ── 2. sdk::RenderEngine (canonical public facade) ─────────────
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

    // ── 3. Render ──────────────────────────────────────────────────
    auto result = engine.render(comp, c3d::sdk::Frame{0});
    if (!result.has_value()) {
        std::fprintf(stderr,
                     "[TEXT-FAIL] sdk::RenderEngine::render failed: code=%s message=%s\n",
                     c3d::sdk::render_error_code_name(result.error().code),
                     result.error().message.c_str());
        return 1;
    }

    const c3d::sdk::RenderOutput& out = result.value();
    if (out.pixels == nullptr || out.width <= 0 || out.height <= 0) {
        std::fprintf(stderr, "[TEXT-FAIL] empty render output\n");
        return 1;
    }

    // ── 4. Bridge RenderOutput → Framebuffer → PNG ──────────────────
    c3d::Framebuffer fb{out.width, out.height};
    const uint8_t* src = out.pixels;
    const bool is_bgra = (out.format == c3d::sdk::PixelFormat::Bgra8);
    const size_t row_stride = (out.bytes_per_row > 0)
        ? static_cast<size_t>(out.bytes_per_row)
        : static_cast<size_t>(out.width) * 4u;
    const float inv = 1.0f / 255.0f;

    for (int32_t y = 0; y < out.height; ++y) {
        for (int32_t x = 0; x < out.width; ++x) {
            const size_t idx = static_cast<size_t>(y) * row_stride
                             + static_cast<size_t>(x) * 4u;
            c3d::Color c;
            if (is_bgra) {
                c = c3d::Color{
                    src[idx + 2] * inv,
                    src[idx + 1] * inv,
                    src[idx + 0] * inv,
                    1.0f};
            } else {
                c = c3d::Color{
                    src[idx + 0] * inv,
                    src[idx + 1] * inv,
                    src[idx + 2] * inv,
                    1.0f};
            }
            fb.set_pixel(x, y, c);
        }
    }

    // ── 5. Pixel check — ci devono essere pixel chiari (testo bianco) ──
    const float kDark  = 15.0f / 255.0f;
    const float kLight = 200.0f / 255.0f;
    size_t dark_pixels = 0;
    size_t light_pixels = 0;
    const c3d::Color* fdata = fb.data();
    for (size_t i = 0; i < fb.pixel_count(); ++i) {
        const float lum = fdata[i].r + fdata[i].g + fdata[i].b;
        if (lum < kDark) ++dark_pixels;
        if (lum > kLight) ++light_pixels;
    }
    const size_t total = fb.pixel_count();

    // Se TUTTI i pixel sono scuri → nessun testo visibile → FAIL
    if (light_pixels == 0 && dark_pixels > total * 99 / 100) {
        std::fprintf(stderr,
                     "[TEXT-FAIL] all pixels dark — no visible text rendered. "
                     "Font assets may be missing at CWD.\n");
        return 1;
    }

    // ── 6. Save PNG ────────────────────────────────────────────────
    const char* output_path = "sdk_text_consumer_output.png";
    if (!c3d::save_png(fb, output_path)) {
        std::fprintf(stderr, "[TEXT-FAIL] save_png failed\n");
        return 1;
    }

    const auto file_size = std::filesystem::file_size(output_path);
    std::printf("[TEXT-OK] rendered %dx%d PNG (%zu bytes, %zu/%zu light pixels)\n",
                out.width, out.height,
                static_cast<size_t>(file_size),
                light_pixels, total);
    return 0;
}
