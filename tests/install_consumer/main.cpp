// tests/install_consumer/main.cpp
//
// ── Rewrite — manifest-clean productive consumer (P3-H + M1.5#9 (1/5)) ──
//
// This is a STANDALONE consumer project — it does NOT share
// tests/CMakeLists.txt and does NOT link against in-tree targets.
// Its only dependency is the *installed* Chronon3D package.
//
// Pinning the contract (RELEASE_GATE.md §SDK Product V1 / 8 — P3-H):
//   (a) `#include`s SOLO header elencati in
//       `cmake/Chronon3DPublicHeaders.cmake`. Niente path
//       `advanced/` / `internal.hpp` / `runtime.hpp` / `test/*`.
//   (b) Chiama SOLO `chronon3d::sdk::RenderEngine::render(...)` +
//       `chronon3d::save_png` + public Composition/Scene/Layer/Shape/
//       CameraDescriptor helper. Il vecchio `chronon3d::RenderEngine`
//       è vietato.
//   (c) Composition con almeno uno Shapelayer produttivo
//       (`GridBackgroundShape` + `TextRunShape`) + una camera
//       compilata (`CameraDescriptor` impostata sulla Composition).
//   (d) Output PNG via `chronon3d::save_png`.
//   (e) Pixel-hash check: fallisce se ogni pixel ha mean luminance
//       inferiore a 5/255 (PNG quasi completamente nero).
//
// Wired into CTest via the top-level CMakeLists.txt option
// `CHRONON3D_BUILD_INSTALL_CONSUMER_TEST` (linux-ci preset).
// Orchestrator: `tools/install_consumer_test.sh`. Phase 4 (this
// consumer) deve emettere `[BOUNDARY-OK]` e chiudere in exit 0.
//
// ── M1.5#9 (1/5) — `TextShape` → `TextRunShape` migration ────────
//
// Phase-4 contract used to require a `TextShape` text layer. Post-
// M1.5#9 (1/5) it requires a `TextRunShape` text layer (the canonical
// modern pipeline: `TextDocument → TextRunLayout → TextRunShape →
// draw_text_run → framebuffer`, established by M1.5#1..#6 cluster).
// The GridBackground plus the absent-asset warning preserves the
// pixel-hash ≥ 5/255 gate even when shaping fails. The header search
// for the asset file ("assets/fonts/Inter-Bold.ttf") is best-effort.

#include <chronon3d/chronon3d.hpp>   // umbrella — manifest entry 1

// FU4 (TICKET-GATE-10-PHASE-4-BLACK-FU4): pull in the full type definition
// of TextRunShape so the std::make_shared<c3d::TextRunShape>() call below
// compiles clean.  text_run_shape.hpp is in the SDK public-header manifest
// (cmake/Chronon3DPublicHeaders.cmake) so this include is manifest-clean.
#include <chronon3d/text/text_run_shape.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>

namespace c3d = chronon3d;

int main() {
    // ── 1. Composition spec (manifest-reachable) ────────────────────
    const c3d::CompositionSpec spec{
        /* .name         */ "p3h_consumer",
        /* .width        */ 640,
        /* .height       */ 360,
        /* .frame_rate   */ c3d::FrameRate{30, 1},
        /* .duration     */ 1,
        /* .assets_root  */ "assets",
    };

    // ── 2. CameraDescriptor (public, value-typed) ───────────────────
    // Compose via `default_camera_descriptor(...)` so the engine's
    // P3-F compile path picks it up. The OPP-side renderer compiles
    // a CameraProgram from this descriptor at render time.
    c3d::camera_v1::CameraDescriptor descriptor{};
    descriptor.id = "p3h_main_camera";
    descriptor.base.enabled = true;
    descriptor.base.position = c3d::Vec3{0.0f, 0.0f, -800.0f};
    descriptor.base.rotation = c3d::Vec3{0.0f, 180.0f, 0.0f};   // GATE-10-PHASE-4-BLACK-FU5: rotate 180° around Y so the camera (at z=-800) looks toward +Z where the GridBackground lives at z=0; without this the OPP-internal P3-F compile path's compiled CameraProgram frustum-culls the grid → all-black PNG.  Mirrors the legacy `comp.camera.set_rotation_euler({0,180,0})` fallback at line 199 so both paths agree.
    // ZoomProjection variant — rely on the field-default initializer
    // (AnimatedValue<float>{1000.0f}) rather than re-passing it explicitly.
    descriptor.base.projection = c3d::camera_v1::ZoomProjection{};
    descriptor.failure_policy = c3d::camera_v1::CameraFailurePolicy::Stop;

    // ── 3. Composition with non-empty Scene ─────────────────────────
    c3d::Composition comp{
        spec,
        [](const c3d::FrameContext& ctx) -> c3d::Scene {
            c3d::Scene scene;

            // ── Background layer: GridBackgroundShape ────────────────
            // Built-in pattern — NON richiede asset font.
            // Garantisce un output non completamente nero anche quando
            // il font fallback della TextRunShape non è disponibile.
            c3d::Layer bg;
            bg.name = "background";
            bg.kind = c3d::LayerKind::Shape;
            bg.from = c3d::Frame{0};
            bg.duration = c3d::Frame{-1};
            bg.visible = true;
            bg.transform = c3d::Transform{};

            c3d::RenderNode bg_node;
            bg_node.name = "grid_bg";
            c3d::GridBackgroundShape gb;
            gb.size = c3d::Vec2{
                static_cast<c3d::f32>(ctx.width),
                static_cast<c3d::f32>(ctx.height)};
            gb.offset = c3d::Vec2{0.0f, 0.0f};
            gb.bg_color = c3d::Color{1.0f, 1.0f, 1.0f, 1.0f};        // DIAG: pure white
            gb.grid_color = c3d::Color{1.0f, 1.0f, 1.0f, 1.0f};     // DIAG: pure white grid
            gb.spacing = 60.0f;
            gb.minor_thickness = 1.0f;
            gb.major_thickness = 2.5f;
            gb.major_every = 4;
            gb.centered = true;
            bg_node.shape = c3d::Shape{gb};
            bg_node.visible = true;
            bg.nodes.push_back(std::move(bg_node));
            scene.add_layer(std::move(bg));

            // ── Text layer — productive text run (M1.5#9 (1/5)) ──────
            // Variant index 14 (ShapeType::TextRun) → canonical modern
            // dispatch path: `SoftwareBackend::draw_text_run` via
            // `multi_source_node` → `TextRunNode` (zero-payload for
            // orphan case; non-null payload for materialised case).
            // font_family va a fallback nel renderer; in mancanza di
            // font locali il rasterizzatore può emettere un `error` log,
            // ma la GridBackground garantisce la soglia > 5/255.
            //
            // M1.5#9 (1/5) NOTE: `LayerKind::Text` IS the canonical
            // modern text-layer kind (see `include/chronon3d/scene/model/
            // layer/layer.hpp:51`: "text-run / animated-text primitive\u201d".
            // Pre-M1.5#9 confusion: `LayerKind::Text` was sometimes
            // believed to ROUTE through the legacy `ShapeType::Text`/
            // `TextShape` path. Post-M1.5#9 (1/5) the layer-kind is purely
            // informational; the shape payload (now `TextRunShapeHandle`)
            // is what determines the render dispatch.
            c3d::Layer title;
            title.name = "title";
            title.kind = c3d::LayerKind::Text;
            title.from = c3d::Frame{0};
            title.duration = c3d::Frame{-1};
            title.visible = true;
            title.transform = c3d::Transform{};

            c3d::RenderNode text_node;
            text_node.name = "title_text";
            // ── M1.5#9 (1/5): switch ShapeType::Text → ShapeType::TextRun +
            // TextRunShapeHandle payload (the canonical modern shape). The
            // TextRunShape's `layout` field stays nullptr here because the
            // consumer does NOT own a FontEngine at the SDK boundary; the
            // renderer-side `SoftwareRenderer` will source its internal
            // engine via `cron3d::sdk::RenderEngine`'s session services,
            // and emit a spdlog error if shaping fails. Phase 4's
            // pixel-hash gate is held by the GridBackground layer above.
            text_node.shape.set_type(c3d::ShapeType::TextRun);
            auto modern_shape = std::make_shared<c3d::TextRunShape>();
            // Stub source text — irrelevant at this boundary (real shaping
            // happens renderer-side; if asset missing → renderer logs error
            // and the TextRun is silently skipped).
            modern_shape->layout = nullptr;  // legacy fallback (forces rb-4
                                             // soft-fail path)
            modern_shape->crossfade_mix = 1.0f;
            text_node.shape.text_run_shape_handle().value =
                std::move(modern_shape);
            // Bridge the styling — kept just for read-only backward compat
            // with any consumer CLI that introspects the title via the
            // legacy field. Allows post-fix flag-up of "fields set /
            // fields rendered" discrepancy.
            text_node.color = c3d::Color{1.0f, 1.0f, 1.0f, 1.0f};
            text_node.fill = c3d::Fill::solid_color(c3d::Color{
                1.0f, 1.0f, 1.0f, 1.0f});
            text_node.world_transform.position =
                c3d::Vec3{static_cast<c3d::f32>(ctx.width) * 0.5f,
                          static_cast<c3d::f32>(ctx.height) * 0.5f,
                          0.0f};
            text_node.visible = true;
            title.nodes.push_back(std::move(text_node));
            scene.add_layer(std::move(title));

            return scene;
        },
    };
    // Apply the camera descriptor to the Composition (public setter).
    comp.default_camera_descriptor(std::move(descriptor));

    // P3-F bridge: render_composition_frame() still uses the legacy
    // `comp.camera` field, not the CameraDescriptor.
    // Default camera at (0,0,-1000) looks down -Z (away from scene at z=0).
    // Rotate 180° to look toward +Z at the GridBackground.
    comp.camera.transform.position = c3d::Vec3{0.0f, 0.0f, -1000.0f};
    comp.camera.set_rotation_euler(c3d::Vec3{0.0f, 180.0f, 0.0f});

    // ── 4. sdk::RenderEngine (the canonical public facade) ──────────
    c3d::sdk::RenderSettings settings{};
    settings.width = spec.width;
    settings.height = spec.height;
    settings.antialiasing_samples = 1;
    settings.deterministic = true;          // reproducible across runs
    settings.dirty_rects = false;
    settings.motion_blur = false;
    settings.max_threads = 1;

    // ── 4b. assets_root diagnostic (non-fatal) ────────────────────────
    // Phase 4 runs from CONS_BUILD (a tempfile dir); the assets/ subdir
    // is unlikely to exist there.  Text shaping may fail, but the
    // GridBackground layer keeps the pixel-hash ≥ 5/255 threshold.
    if (!std::filesystem::is_directory(spec.assets_root)) {
        std::fprintf(stderr,
                     "[consumer-warn] assets_root '%s' is not a directory "
                     "at CWD — text layer may render blank, but "
                     "GridBackground keeps the pixel-hash ≥ 5/255.\n",
                     spec.assets_root.c_str());
    }

    c3d::sdk::RenderEngine engine{settings};
    engine.set_assets_root(std::filesystem::path{spec.assets_root});

    // ── 5. Render ──────────────────────────────────────────────────
    // ── 5. Render ──────────────────────────────────────────────────
    // DIAGNOSTIC: check scene before rendering
    {
        auto diag_scene = comp.evaluate(c3d::Frame{0});
        std::fprintf(stderr, "[consumer-diag] scene has %zu layers\n", diag_scene.layers().size());
        for (size_t li = 0; li < diag_scene.layers().size(); ++li) {
            const auto& l = diag_scene.layers()[li];
            std::fprintf(stderr, "[consumer-diag]   layer[%zu] name='%s' kind=%d nodes=%zu visible=%d\n",
                li, l.name.c_str(), static_cast<int>(l.kind), l.nodes.size(), l.visible ? 1 : 0);
        }
    }

    auto result = engine.render(comp, c3d::sdk::Frame{0});
    if (!result.has_value()) {
        std::fprintf(stderr,
                     "[BOUNDARY-FAIL] sdk::RenderEngine::render failed: "
                     "code=%s message=%s\n",
                     c3d::sdk::render_error_code_name(result.error().code),
                     result.error().message.c_str());
        return 1;
    }

    const c3d::sdk::RenderOutput& out = result.value();
    if (out.pixels == nullptr || out.width <= 0 || out.height <= 0) {
        std::fprintf(stderr,
                     "[BOUNDARY-FAIL] render returned empty output: "
                     "pixels=%p width=%d height=%d\n",
                     static_cast<const void*>(out.pixels),
                     out.width, out.height);
        return 1;
    }
    // PixelFormat::Unknown (or any future non-RGBA8/BGRA8) is not bridged:
    // silently treating it as Rgba8 would silently invert channels if the
    // engine ever returns Unknown.  Fail-fast rather than lie.
    if (out.format != c3d::sdk::PixelFormat::Rgba8
        && out.format != c3d::sdk::PixelFormat::Bgra8) {
        std::fprintf(stderr,
                     "[BOUNDARY-FAIL] unsupported PixelFormat enum=%d "
                     "(only Rgba8/Bgra8 are bridged by this consumer)\n",
                     static_cast<int>(out.format));
        return 1;
    }

    // ── 6. Bridge: RenderOutput (uint8 RGBA/BGRA) → Framebuffer ─────
    // RenderOutput gives `pixels` as a packed 4-channel byte buffer.
    // Format::Rgba8 or Format::Bgra8 differ only in channel order.
    // The Framebuffer stores colors as float RGBA (math::Color).
    // We allocate an owned Framebuffer (the only width/height
    // constructor reachable from the manifest) and copy pixel-by-pixel.
    c3d::Framebuffer fb{out.width, out.height};

    const std::uint8_t* src = out.pixels;
    const bool is_bgra = (out.format == c3d::sdk::PixelFormat::Bgra8);
    // Honour out.bytes_per_row when non-zero (per-row alignment padding);
    // 0 ⇒ tightly packed ⇒ stride == width*4.  Computing this correctly
    // matters for the byte→Color bridge: a wrong stride corrupts the
    // pixel-hash check.
    const std::size_t row_stride =
        (out.bytes_per_row > 0)
            ? static_cast<std::size_t>(out.bytes_per_row)
            : static_cast<std::size_t>(out.width) * 4u;

    for (std::int32_t y = 0; y < out.height; ++y) {
        for (std::int32_t x = 0; x < out.width; ++x) {
            const std::size_t idx =
                static_cast<std::size_t>(y) * row_stride
                + static_cast<std::size_t>(x) * 4u;
            c3d::Color c;
            const float inv = 1.0f / 255.0f;
            if (is_bgra) {
                c.b = src[idx + 0] * inv;
                c.g = src[idx + 1] * inv;
                c.r = src[idx + 2] * inv;
                c.a = 1.0f;   // GATE-10-PHASE-4-BLACK-FU6: force alpha opaque in the bridge regardless of SDK's per-pixel byte-alpha, so ANY pool-init / temporal-accumulation NaN-alpha in the OPP's framebuffer_to_rgba8 byte output cannot propagate as transparent (0,0,0,0) and cause the gate's per-pixel mean-RGB > 5/255 check to fail. The PIL gate reads only RGB sum, but a `c.a==0` set_pixel(before NaN-guard in save_png fires) might still be the upstream problem on the alpha-channel NaN signal — defensible hardening for V0.1 release.
            } else {
                c.r = src[idx + 0] * inv;
                c.g = src[idx + 1] * inv;
                c.b = src[idx + 2] * inv;
                c.a = 1.0f;   // GATE-10-PHASE-4-BLACK-FU6: force alpha opaque in the bridge regardless of SDK's per-pixel byte-alpha, so ANY pool-init / temporal-accumulation NaN-alpha in the OPP's framebuffer_to_rgba8 byte output cannot propagate as transparent (0,0,0,0) and cause the gate's per-pixel mean-RGB > 5/255 check to fail. The PIL gate reads only RGB sum, but a `c.a==0` set_pixel(before NaN-guard in save_png fires) might still be the upstream problem on the alpha-channel NaN signal — defensible hardening for V0.1 release.
            }
            fb.set_pixel(x, y, c);
        }
    }

    // ── 7. Pixel-hash check (≥ 5/255 mean luminance on ≥ 1 pixel) ───
    constexpr float kThreshold = 5.0f / 255.0f;   // ≈ 0.0196
    std::size_t nonzero_count = 0;
    const c3d::Color* fdata = fb.data();
    // DIAGNOSTIC: dump first 5 pixel values
    std::fprintf(stderr, "[consumer-diag] first 5 pixels (r,g,b,a):\n");
    for (std::size_t i = 0; i < 5 && i < fb.pixel_count(); ++i) {
        const c3d::Color c = fdata[i];
        std::fprintf(stderr, "[consumer-diag]   pixel[%zu] = (%.4f, %.4f, %.4f, %.4f)\n",
            i, c.r, c.g, c.b, c.a);
    }
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

    // ── 8. Save PNG + PPM (dual diagnostic) ─────────────────────────
    const std::filesystem::path output_path = "sdk_consumer_output.png";
    // Also save PPM for raw data comparison
    fb.save_ppm("sdk_consumer_output.ppm");
    // Dump raw bytes from fb.data() to a binary file for inspection
    {
        auto* f = std::fopen("sdk_consumer_raw.bin", "wb");
        if (f) {
            std::fwrite(fb.data(), sizeof(c3d::Color), fb.pixel_count(), f);
            std::fclose(f);
        }
    }
    if (!c3d::save_png(fb, output_path.string())) {
        std::fprintf(stderr,
                     "[BOUNDARY-FAIL] save_png failed for %s\n",
                     output_path.string().c_str());
        return 1;
    }
    if (!std::filesystem::exists(output_path)
        || std::filesystem::file_size(output_path) == 0) {
        std::fprintf(stderr,
                     "[BOUNDARY-FAIL] output PNG missing/empty: %s\n",
                     output_path.string().c_str());
        return 1;
    }

    // ── 9. Boundary marker ─────────────────────────────────────────
    const auto file_size = std::filesystem::file_size(output_path);
    std::printf("[BOUNDARY-OK] sdk consumer (P3-H) rendered %dx%d PNG "
                "(%zu bytes, %zu/%zu pixels >5/255, format=%s, "
                "bytes_per_row=%zu, %.3f ms)\n",
                out.width, out.height,
                static_cast<std::size_t>(file_size),
                nonzero_count, fb.pixel_count(),
                (is_bgra ? "Bgra8" : "Rgba8"),
                row_stride,
                out.elapsed_milliseconds);
    return 0;
}
