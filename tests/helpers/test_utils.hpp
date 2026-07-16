#pragma once
#include <chronon3d/sdk/render_engine.hpp>
#include <chronon3d/sdk/render_output.hpp>
#include <chronon3d/sdk/render_error.hpp>
#include <chronon3d/sdk/render_request.hpp>
#include <chronon3d/sdk/render_settings.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/text/text_run_shape.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/backends/image/image_writer.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/runtime/render_runtime.hpp>
#include <chronon3d/backends/software/software_backend.hpp>
#include <chronon3d/backends/image/stb_image_backend.hpp>
#include <chronon3d/render_graph/render_graph_context.hpp>   // graph::RenderFrameInfo

// TICKET-118/119 — internal processor-services bridge is the only path
// that knows how to turn a public `SoftwareBackendServices` bundle into
// a fully-populated `SoftwareProcessorContext`.  Reachable from tests
// because `chronon3d_backend_software` exports `${CMAKE_SOURCE_DIR}/src/backends/software`
// as a PUBLIC include directory (see src/backends/software/CMakeLists.txt).
#include "internal/software_processor_services.hpp"

#include <xxhash.h>
#include <cstring>
#include <memory>
#include <string>

namespace chronon3d::test {

// ── Renderer factories ────────────────────────────────────────────────────

/// Attach a `SoftwareBackend` to the renderer's runtime.  Required before
/// calling `render_frame` / `render_scene` — `RenderRuntime::backend()`
/// throws when called before `attach_backend()`.  Call after `set_settings()`.
///
/// TICKET-118 closure — `SoftwareBackendServices::owner` REMOVED.  The
/// `SoftwareRenderer* m_owner` back-pointer is gone; orchestrator-only
/// fields (registry / image_backend / font_engine) are attached post-
/// construction via `SoftwareBackend::attach_processor_context(...)`,
/// routed through `backends::software::internal::make_processor_context`.
/// Lifetime invariant (preserved): `renderer->runtime()` owns the
/// backend, and `~RenderRuntime()` runs BEFORE `~SoftwareRenderer()`.
///
/// TICKET-079: takes `SoftwareRenderer*` (pointer) instead of
/// `SoftwareRenderer&` (lvalue ref) — gate-3 I5 compliance.
inline void attach_software_backend(SoftwareRenderer* renderer) {
    chronon3d::SoftwareBackendServices services{
        /* owner REMOVED IN TICKET-118 — was: renderer, */
        /* counters           = */ renderer->counters(),
        /* settings           = */ &renderer->render_settings(),
        /* framebuffer_pool   = */ renderer->runtime().framebuffer_pool_shared(),
        /* asset_resolver     = */ &renderer->runtime().resolver(),
        /* text_resources     = */ renderer->text_render_resources(),
        /* images             = */ &renderer->image_renderer(),
        /* text_raster        = */ nullptr,
        /* debug_config       = */ nullptr,
    };
    auto backend =
        chronon3d::make_software_backend(std::move(services)).value();

    // TICKET-119 — wire the orchestrator-only fields through the
    // internal bridge (mirrors runtime_adapter.cpp).  Lifetime of
    // extras.* matches `*renderer` which outlives the backend (the
    // backend is owned by `renderer->runtime()`, and
    // `~RenderRuntime()` runs BEFORE `~SoftwareRenderer()`).
    backends::software::internal::ProcessorSourceExtras extras{};
    extras.registry      = &renderer->software_registry();
    extras.image_backend = renderer->image_backend();
    extras.image_renderer = &renderer->image_renderer();
#ifdef CHRONON3D_HAS_BACKEND_TEXT
    extras.font_engine   = &renderer->font_engine();
#endif
    auto processor_context =
        backends::software::internal::make_processor_context(services, extras);
    processor_context.image_renderer = &renderer->image_renderer();
    processor_context.image_backend = renderer->image_backend();
    backend->attach_processor_context(std::move(processor_context));
    backend->attach_image_services(&renderer->image_renderer(),
                                   renderer->image_backend());

    // Fase C2 — suppress deprecation warning; this is an internal test bridge.
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
    renderer->runtime().attach_backend(std::move(backend));
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
}

/// Canonical renderer factory — returns shared_ptr<SoftwareRenderer> with
/// full wiring (settings, image backend, software backend).  Self-contained:
/// no dependency on CLI headers.  Prefer this over make_renderer() in new tests.
inline std::shared_ptr<SoftwareRenderer> make_renderer_shared() {
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
    auto renderer = std::make_shared<SoftwareRenderer>(Config{});
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
    RenderSettings settings;
    settings.use_modular_graph = true;
    renderer->set_settings(settings);
    renderer->set_image_backend(std::make_shared<image::StbImageBackend>());
    attach_software_backend(renderer.get());
    return renderer;
}

/// @deprecated Prefer make_renderer_shared() which returns shared_ptr.
/// Kept for source compatibility with existing tests.
inline SoftwareRenderer make_renderer() {
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
    SoftwareRenderer renderer(Config{});
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
    RenderSettings settings;
    settings.use_modular_graph = true;
    renderer.set_settings(settings);
    renderer.set_image_backend(std::make_shared<image::StbImageBackend>());
    attach_software_backend(&renderer);
    return renderer;
}

/// @deprecated Prefer make_renderer_shared() — uses create_renderer() canonical path.
inline SoftwareRenderer make_renderer_ssaa(float factor) {
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
    SoftwareRenderer renderer(Config{});
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
    RenderSettings settings;
    settings.use_modular_graph = true;
    settings.ssaa_factor = std::max(1.0f, factor);
    renderer.set_settings(settings);
    renderer.set_image_backend(std::make_shared<image::StbImageBackend>());
    attach_software_backend(&renderer);
    return renderer;
}

/// Overload: equivalent to make_renderer() but accepts a `modular_coordinates`
/// flag for source-compatibility with callers that previously distinguished
/// the two toggles.  Centralises the (bool modular) signature used by all PR2
/// RG integration tests and goldens.  Note: this does NOT call
/// set_image_backend() because the PR2 node tests assume pure software backend
/// and don't write PNGs.
///
/// History: `RenderSettings::modular_coordinates` was retired by upstream
/// refactor (bf3aaa47).  Pre-refactor, the helper set BOTH `use_modular_graph
/// = true` AND `modular_coordinates = <arg>`, with `modular_coordinates` as a
/// fine-tune on top of the modular-graph invariant.  Post-refactor, the only
/// remaining knob is `use_modular_graph` (the on/off switch for "centered
/// modular graph coordinates vs top-left scene coordinates").  We preserve
/// the prior invariant (modular-graph ON regardless of `<arg>`) to avoid
/// silently changing observed rendering for tests that passed `false`.  The
/// parameter is kept for ABI/source compatibility and is documented as a
/// no-op until a downstream knob reappears.
/// @deprecated Prefer make_renderer_shared() — uses create_renderer() canonical path.
inline SoftwareRenderer make_renderer(bool /*modular_coordinates_unused*/) {
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
    SoftwareRenderer renderer(Config{});
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
    RenderSettings settings;
    settings.use_modular_graph = true;     // preserved invariant
    renderer.set_settings(settings);
    attach_software_backend(&renderer);
    return renderer;
}

// ── Framebuffer hashes ────────────────────────────────────────────────────

/// 64-bit hash of every pixel (RGBA).  Backed by XXH64 for speed; deterministic
/// across processes.  Identical inputs → identical hash.  Used by PR2 RG
/// integration tests for byte-equality assertions across two consecutive renders.
inline u64 framebuffer_hash(const Framebuffer& fb) {
    return XXH64(fb.pixels_row(0), fb.size_bytes(), 0);
}

/// 64-bit hash of the alpha channel only.  Robust to RGB jitter (blend modes,
/// fp32 ordering) — used by mask RG tests that want to verify alpha-occlusion
/// invariance under repeated render calls.
inline u64 framebuffer_alpha_hash(const Framebuffer& fb) {
    u64 h = XXH64(fb.pixels_row(0), fb.size_bytes(), 0);   // seed = full hash
    const int w = fb.width();
    const int h_px = fb.height();
    for (int y = 0; y < h_px; ++y) {
        const Color* row = fb.pixels_row(y);
        for (int x = 0; x < w; ++x) {
            std::uint32_t bits;
            std::memcpy(&bits, &row[x].a, 4);
            h ^= bits;
            h *= 0x100000001b3ULL;
        }
    }
    return h;
}

// ── Luma helpers ──────────────────────────────────────────────────────────

/// Perceptual luma (BT.601 weights) times alpha coverage.  Matches the helper
/// used by tests/renderer/effects/test_glow_torture.cpp.  Centralised here so
/// RG integration tests don't duplicate the formula.
inline float luma_alpha(const Color& c) {
    return (0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b) * c.a;
}

/// Pure luma (no alpha).  Used for "brightness above bg baseline" assertions
/// that ignore coverage — see glow/bloom RG integration tests.
inline float luma(const Color& c) {
    return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
}

// ── Frame / RenderFrameInfo helpers ───────────────────────────────────────

inline FrameContext make_ctx(Frame frame, int width = 1920, int height = 1080) {
    FrameContext ctx;
    ctx.frame = frame;
    ctx.frame_rate = FrameRate{30, 1};
    ctx.width = width;
    ctx.height = height;
    return ctx;
}

/// Minimal RenderFrameInfo for unit tests that call nodes' cache_key() or
/// predicted_bbox().  Centralises the (frame=0, fps=30, size=(w,h)) boilerplate
/// that was duplicated in test_shadow_node_unit / test_*_dof_node_unit /
/// test_mask_node_unit.
inline graph::RenderFrameInfo make_render_frame_info(int width, int height,
                                                    int frame = 0) {
    graph::RenderFrameInfo fi;
    fi.frame = Frame{frame};
    fi.width = width;
    fi.height = height;
    fi.fps = 30.0f;
    fi.time_seconds = static_cast<float>(frame) / 30.0f;
    return fi;
}

inline float average_luma_rect(const Framebuffer& fb, int x0, int y0, int x1, int y1) {
    float total = 0.0f;
    int count = 0;
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            Color c = fb.get_pixel(x, y);
            total += (c.r * 0.299f + c.g * 0.587f + c.b * 0.114f) * c.a;
            count++;
        }
    }
    return count > 0 ? total / (float)count : 0.0f;
}

inline std::shared_ptr<Framebuffer> render_fn(
    std::function<Scene(const FrameContext&)> build_fn,
    int width = 200,
    int height = 200
) {
    auto renderer = make_renderer_shared();
    Composition comp = composition({.width = width, .height = height}, build_fn);
    return renderer->render(comp, 0);
}

inline std::shared_ptr<Framebuffer> load_png_as_framebuffer(const std::string& path) {
    image::StbImageBackend backend;
    auto buf = backend.load_image(path);
    if (!buf) return nullptr;
    auto fb = std::make_shared<Framebuffer>(buf->width, buf->height);
    for (int y = 0; y < buf->height; ++y) {
        for (int x = 0; x < buf->width; ++x) {
            int idx = (y * buf->width + x) * 4;
            Color c{
                static_cast<float>(buf->pixels[idx]) / 255.0f,
                static_cast<float>(buf->pixels[idx+1]) / 255.0f,
                static_cast<float>(buf->pixels[idx+2]) / 255.0f,
                static_cast<float>(buf->pixels[idx+3]) / 255.0f
            };
            fb->set_pixel(x, y, c);
        }
    }
    return fb;
}

} // namespace chronon3d::test
