#pragma once

#include <chronon3d/chronon3d.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/image/stb_image_backend.hpp>
#include <chronon3d/render_graph/render_graph_context.hpp>   // graph::RenderFrameInfo
#include <xxhash.h>
#include <cstring>
#include <memory>
#include <string>

namespace chronon3d::test {

// ── Renderer factories ────────────────────────────────────────────────────

inline SoftwareRenderer make_renderer() {
    SoftwareRenderer renderer(Config{});
    RenderSettings settings;
    settings.use_modular_graph = true;
    renderer.set_settings(settings);
    renderer.set_image_backend(std::make_shared<image::StbImageBackend>());
    return renderer;
}

inline SoftwareRenderer make_renderer_ssaa(float factor) {
    SoftwareRenderer renderer(Config{});
    RenderSettings settings;
    settings.use_modular_graph = true;
    settings.ssaa_factor = std::max(1.0f, factor);
    renderer.set_settings(settings);
    renderer.set_image_backend(std::make_shared<image::StbImageBackend>());
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
inline SoftwareRenderer make_renderer(bool /*modular_coordinates_unused*/) {
    SoftwareRenderer renderer(Config{});
    RenderSettings settings;
    settings.use_modular_graph = true;     // preserved invariant
    renderer.set_settings(settings);
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
    SoftwareRenderer renderer(Config{});
    Composition comp = composition({.width = width, .height = height}, build_fn);
    return renderer.render_frame(comp, 0);
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
