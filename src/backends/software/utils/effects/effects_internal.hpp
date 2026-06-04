#pragma once

// ---------------------------------------------------------------------------
// effects_internal.hpp
//
// Private helpers shared across effect_blur.cpp, effect_color.cpp,
// effect_wave.cpp, and effect_stack.cpp.
// NOT for inclusion outside the effects implementation files.
// ---------------------------------------------------------------------------

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/cache/framebuffer_pool.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/scene/model/layer_effect.hpp>
#include <chronon3d/scene/model/effect_stack.hpp>
#include <algorithm>
#include <cmath>
#include <memory>

namespace chronon3d {
namespace renderer {

// ── Math helpers ─────────────────────────────────────────────────────────────

inline f32 clamp01(f32 v) {
    return std::clamp(v, 0.0f, 1.0f);
}

inline f32 safe_scale(f32 depth, f32 perspective) {
    return std::max(0.1f, 1.0f + depth * perspective / 100.0f);
}

// ── Framebuffer pool access ───────────────────────────────────────────────────
//
// Grabs a cleared framebuffer from the thread-local pool (if available),
// or heap-allocates one. Always returns a cleared buffer.
inline std::shared_ptr<Framebuffer> acquire_temp_framebuffer(int w, int h) {
    if (profiling::g_current_framebuffer_pool) {
        return profiling::g_current_framebuffer_pool->acquire_pooled(
            w, h,
            profiling::g_current_framebuffer_pool->shared_from_this(),
            true
        );
    }
    auto fb = std::make_shared<Framebuffer>(w, h);
    fb->clear(Color::transparent());
    return fb;
}

// ── Shadow compositing ────────────────────────────────────────────────────────
//
// Composites the pre-rendered shadow buffer behind the content buffer in-place.
inline void apply_shadow_buffer(Framebuffer& content, const Framebuffer& shadow) {
    const i32 w = content.width();
    const i32 h = content.height();
    for (i32 y = 0; y < h; ++y) {
        const Color* shadow_row = shadow.pixels_row(y);
        Color* content_row = content.pixels_row(y);
        for (i32 x = 0; x < w; ++x) {
            const Color shadow_px = shadow_row[x];
            if (shadow_px.a <= 0.0f) continue;
            content_row[x] = compositor::blend(content_row[x], shadow_px, BlendMode::Normal);
        }
    }
}

} // namespace renderer
} // namespace chronon3d
