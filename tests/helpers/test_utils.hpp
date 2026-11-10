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
#include <chronon3d/backends/software/runtime_adapter.hpp>
#include <chronon3d/backends/image/stb_image_backend.hpp>
#include <chronon3d/render_graph/render_graph_context.hpp>   // graph::RenderFrameInfo
#include <chronon3d/render_graph/pipeline/render_pipeline.hpp>
#include <chronon3d/timeline/compile_evaluate.hpp>
#include <chronon3d/scene/model/camera/camera.hpp>

// TICKET-118/119 — internal processor-services bridge is the only path
// that knows how to turn a public `SoftwareBackendServices` bundle into
// a fully-populated `SoftwareProcessorContext`.  Reachable from tests
// because `chronon3d_backend_software` exports `${CMAKE_SOURCE_DIR}/src/backends/software`
// as a PUBLIC include directory (see src/backends/software/CMakeLists.txt).
#include "internal/software_processor_services.hpp"

#include <xxhash.h>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>

namespace chronon3d::test {

// ── Repository-root helpers ───────────────────────────────────────────────

/// Derive the repository root from the location of this header.
/// Works regardless of the current working directory.
inline std::filesystem::path test_repo_root() {
    std::filesystem::path src(__FILE__);
    // __FILE__ == <repo>/tests/helpers/test_utils.hpp
    return std::filesystem::absolute(src).parent_path().parent_path().parent_path();
}

/// Return an absolute path to a bundled asset under the repo root.
inline std::string bundled_font_path(const std::string& rel) {
    return (test_repo_root() / rel).string();
}

// ── Renderer factories ────────────────────────────────────────────────────

/// Attach a `SoftwareBackend` to the renderer's runtime.  Required before
/// calling canonical render entry points — `RuntimeContext::backend()`
/// throws when called before `attach_backend()`.  Call after `set_settings()`.
///
/// TICKET-118 closure — `SoftwareBackendServices::owner` REMOVED.  The
/// `SoftwareRenderer* m_owner` back-pointer is gone; orchestrator-only
/// fields (registry / image_backend / font_engine) are attached post-
/// construction via `SoftwareBackend::attach_processor_context(...)`,
/// routed through `backends::software::internal::make_processor_context`.
/// Lifetime invariant (preserved): `renderer->runtime()` owns the
/// backend, and `~RuntimeContext()` runs BEFORE `~SoftwareRenderer()`.
///
/// TICKET-079: takes `SoftwareRenderer*` (pointer) instead of
/// `SoftwareRenderer&` (lvalue ref) — gate-3 I5 compliance.
inline void attach_software_backend(SoftwareRenderer* renderer,
                                    runtime::RuntimeContext& runtime) {
    // Keep tests on the same explicit execution-context path as production.
    // Backend wiring below borrows the renderer's FontEngine, so initialize
    // the runtime-owned text service before asking the renderer for it.
    renderer->attach_execution_context(
        backends::software::make_software_execution_context(renderer, runtime));
    chronon3d::SoftwareBackendServices services{
        /* owner REMOVED IN TICKET-118 — was: renderer, */
        /* counters           = */ renderer->counters(),
        /* settings           = */ &renderer->render_settings(),
        /* framebuffer_pool   = */ runtime.framebuffer_pool_shared(),
        /* asset_resolver     = */ &runtime.resolver(),
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
    // `~RuntimeContext()` runs BEFORE `~SoftwareRenderer()`).
    backends::software::internal::ProcessorSourceExtras extras{};
    extras.registry      = &renderer->software_registry();
    extras.image_backend = renderer->image_backend();
    extras.image_renderer = &renderer->image_renderer();
    // Session-owned scratch, exactly like the production call sites: without
    // it the scalar effects / mesh depth pool / DOF scratch would be null.
    {
        auto& software_resources = renderer->software_session().software;
        extras.effect_scratch = &software_resources.effect_scratch;
        extras.depth_buffer_pool = &software_resources.depth_buffer_pool;
        extras.dof_scratch = &software_resources.dof_scratch;
    }
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
    runtime.attach_backend(std::move(backend));
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

    renderer->attach_execution_context(
        backends::software::make_software_execution_context(renderer, runtime));
}

// ── TestRenderer wrapper ──────────────────────────────────────────────────

/// Test-only SSAA box downsample. Production no longer carries a Composition
/// executor; this mirrors the old finalize step for fixtures that request SSAA.
inline std::shared_ptr<Framebuffer> downsample_box(
    const Framebuffer& src, int dst_w, int dst_h) {
    auto dst = std::make_shared<Framebuffer>(dst_w, dst_h);
    const float sx = static_cast<float>(src.width()) / static_cast<float>(dst_w);
    const float sy = static_cast<float>(src.height()) / static_cast<float>(dst_h);
    for (int y = 0; y < dst_h; ++y) {
        for (int x = 0; x < dst_w; ++x) {
            const int x0 = static_cast<int>(x * sx);
            const int y0 = static_cast<int>(y * sy);
            const int x1 = std::min(static_cast<int>((x + 1) * sx), src.width());
            const int y1 = std::min(static_cast<int>((y + 1) * sy), src.height());
            float r = 0, g = 0, b = 0, a = 0;
            int count = 0;
            for (int iy = y0; iy < y1; ++iy) {
                for (int ix = x0; ix < x1; ++ix) {
                    const Color c = src.get_pixel(ix, iy);
                    r += c.r; g += c.g; b += c.b; a += c.a;
                    ++count;
                }
            }
            const float inv = count > 0 ? 1.0f / static_cast<float>(count) : 0.0f;
            dst->set_pixel(x, y, Color{r * inv, g * inv, b * inv, a * inv});
        }
    }
    return dst;
}

struct TestRuntimeOwner {
    std::unique_ptr<runtime::RuntimeContext> m_test_runtime;
    explicit TestRuntimeOwner(const Config& cfg = Config{}) {
        auto res = runtime::RuntimeContext::create(
            runtime::RuntimeConfig{.config = cfg, .assets_root = std::nullopt});
        if (!res) throw std::runtime_error("TestRenderer: runtime creation failed: " + res.error().message);
        m_test_runtime = std::move(res).value();
        if (!m_test_runtime->resolver().has_mount()) {
            m_test_runtime->resolver().mount(std::filesystem::current_path());
        }
    }
    explicit TestRuntimeOwner(std::unique_ptr<runtime::RuntimeContext> rt)
        : m_test_runtime(std::move(rt)) {}
};

class TestRenderer : private TestRuntimeOwner, public SoftwareRenderer {
public:
    explicit TestRenderer(Config config = Config{}, float ssaa = 1.0f)
        : TestRuntimeOwner(config)
        , SoftwareRenderer(
            backends::software::make_software_renderer_context(*m_test_runtime),
            std::move(config))
    {
        RenderSettings settings;
        if (ssaa > 1.0f) settings.ssaa_factor = ssaa;
        set_settings(settings);
        set_image_backend(std::make_shared<image::StbImageBackend>());
        attach_software_backend(this, *m_test_runtime);
    }

    /// Test-only Composition entry point. The legacy executor is gone from
    /// production; fixtures still author Compositions, so evaluate one frame
    /// and hand it to the canonical graph entry (`render_scene_via_graph`).
    std::shared_ptr<Framebuffer> render(const Composition& composition,
                                        Frame frame) {
        auto compiled = compile_composition(
            composition, CompositionCompileContext{});
        if (!compiled) {
            throw std::runtime_error(
                "TestRenderer: composition compile failed: " +
                compiled.error().message);
        }
        const auto& spec = composition.spec();
        const SampleTime sample = SampleTime::from_frame(
            static_cast<double>(frame), spec.frame_rate);
        const auto* runtime = m_test_runtime.get();
        const FrameContext ctx = make_frame_context(FrameContextParams{
            .global_time = sample,
            .local_time = sample,
            .duration = spec.duration,
            .width = spec.width,
            .height = spec.height,
            .assets_root = runtime->resolver().mount_root().string(),
            .font_engine = &font_engine(),
            .runtime = runtime,
        });
        auto evaluated = evaluate(
            *compiled, CompositionEvaluateContext{.frame_context = ctx},
            ctx.local_time());
        if (!evaluated) {
            throw std::runtime_error(
                "TestRenderer: composition evaluation failed: " +
                evaluated.error().message);
        }
        Scene scene = std::move(evaluated.value().scene);
        const int layer_count = static_cast<int>(scene.layers().size());
        float ssaa = render_settings().ssaa_factor;
        if (!std::isfinite(ssaa) || ssaa < 1.0f) ssaa = 1.0f;
        const int render_w = static_cast<int>(
            static_cast<double>(spec.width) * static_cast<double>(ssaa));
        const int render_h = static_cast<int>(
            static_cast<double>(spec.height) * static_cast<double>(ssaa));
        auto fb = graph::render_scene_via_graph(
            backend(), node_cache(), scene, Camera{},
            render_w, render_h, frame, 0.0f, render_settings(),
            composition_registry(), video_decoder(),
            static_cast<float>(spec.frame_rate.fps()), spec.name, this,
            nullptr);
        if (fb && ssaa > 1.0f) {
            fb = downsample_box(*fb, spec.width, spec.height);
        }
        dirty_telemetry().last_layer_count = layer_count;
        return fb;
    }

    TestRenderer(TestRenderer&& other) noexcept
        : TestRuntimeOwner(std::move(other.m_test_runtime))
        , SoftwareRenderer(std::move(other))
    {
    }

    TestRenderer& operator=(TestRenderer&& other) noexcept {
        SoftwareRenderer::operator=(std::move(other));
        m_test_runtime = std::move(other.m_test_runtime);
        return *this;
    }

    TestRenderer(const TestRenderer&) = delete;
    TestRenderer& operator=(const TestRenderer&) = delete;
};

/// Canonical renderer factory — returns shared_ptr<TestRenderer> with full
/// wiring (settings, image backend, software backend). Returning the concrete
/// test type preserves the test-only Composition render helper.
inline std::shared_ptr<TestRenderer> make_renderer_shared() {
    return std::make_shared<TestRenderer>();
}

/// Renderer factory returning TestRenderer (which is-a SoftwareRenderer).
inline TestRenderer make_renderer() {
    return TestRenderer{};
}

/// Renderer factory with SSAA factor.
inline TestRenderer make_renderer_ssaa(float factor) {
    return TestRenderer{Config{}, factor};
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

inline FrameContext make_ctx(Frame frame_value, int width = 1920, int height = 1080) {
    const FrameRate rate{30, 1};
    const SampleTime st = SampleTime::from_frame_int(frame_value, rate);
    return make_frame_context(FrameContextParams{
        .global_time = st,
        .local_time = st,
        .duration = Frame{0},
        .width = width,
        .height = height,
    });
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
