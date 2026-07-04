// ═══════════════════════════════════════════════════════════════════════════
// src/backends/software/processors/text_run/text_run_processor/scratch.cpp
// ═══════════════════════════════════════════════════════════════════════════
//
// M1.5#6 — Stage utility: scratch pool wrappers + helpers that USE the
// scratch pool (so they must NOT allocate per-draw).
//
// What's in this file:
//   - acquire_scratch_handle(rctx) — first entry-point; RAII Handle into
//     TextScratchManager (engine-lifetime, vend per-call state, TSan-clean).
//   - acquire_surface / release_surface — forwarders over TextScratchState
//     to keep caller code from reaching through Handle->* unconditionally.
//   - apply_separable_box_blur — sliding-window O(w*h) box blur on PRGB32;
//     uses scratch_state.blur_buffer for amortized buffer reuse (NO vector
//     created per draw).
//   - downsample_supersampled — box-filter downsample (Phase FASE 3b); uses
//     scratch pool for the destination image.
//   - force_parallel_mode() — read-once env-var toggle for the bench
//     golden test; current code path is unconditional serial, the flag is
//     a marker for future parallel work without changing the API surface.
//
// AGENTS.md v0.1 freeze Cat-3 internal — strictly in src/backends/software/.
// NOT promoted to include/chronon3d/.

#include "text_run_stages.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>

namespace chronon3d::renderer::text_run_stages {

// ── acquire_scratch_handle ──────────────────────────────────────────────────
//
// First entry-point.  Returns RAII Handle (vendor TextScratchState on
// acquire; releases back to pool on scope exit).  Every other stage MUST
// reach scratch through `s.scratch_handle->...` — never raw `new`/`delete`.
[[nodiscard]] TextScratchManager::Handle acquire_scratch_handle(
    const SoftwareProcessorContext& rctx
) {
    return rctx.text_resources->scratch_manager.acquire();
}

// ── forwarders (avoid `s.scratch_handle->acquire_surface(w, h, fmt)` noise) ─

[[nodiscard]] BLImage acquire_surface(
    TextRunStageState& s,
    int w, int h,
    std::uint32_t fmt
) {
    return s.scratch_handle->acquire_surface(w, h, fmt);
}

void release_surface(
    TextRunStageState& s,
    BLImage img
) {
    s.scratch_handle->release_surface(std::move(img));
}

// ── apply_separable_box_blur ───────────────────────────────────────────────
//
// Horizontal + vertical sliding-window box blur.  Operates on PRGB32
// (premultiplied alpha; every channel averaged independently so the
// alpha/color ratio is preserved).  Cost ~ 2 * w * h independent of radius.
// The temp buffer is `s.scratch_handle->blur_buffer` — the TextScratchState
// member owned by the per-call pool.  We only resize when the buffer is
// too small (amortized growth across draws, NO vector created per draw).
void apply_separable_box_blur(BLImage& image, int radius, TextRunStageState& s) {
    if (radius <= 0) return;
    BLImageData data;
    if (image.getData(&data) != BL_SUCCESS) return;
    const int w = data.size.w;
    const int h = data.size.h;
    if (w <= 0 || h <= 0) return;
    const int stride = static_cast<int>(data.stride / sizeof(std::uint32_t));
    std::uint32_t* base = static_cast<std::uint32_t*>(data.pixelData);

    const std::size_t pixels = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
    if (s.scratch_handle->blur_buffer.size() < pixels) {
        s.scratch_handle->blur_buffer.resize(pixels);
    }
    std::uint32_t* tmp = s.scratch_handle->blur_buffer.data();

    const int r = radius;
    auto unpack = [](std::uint32_t c, int& a, int& rr, int& g, int& b) {
        a = static_cast<int>((c >> 24) & 0xFF);
        rr = static_cast<int>((c >> 16) & 0xFF);
        g = static_cast<int>((c >>  8) & 0xFF);
        b = static_cast<int>( c        & 0xFF);
    };
    auto pack = [](int a, int rr, int g, int b) -> std::uint32_t {
        return (static_cast<std::uint32_t>(std::clamp(a, 0, 255)) << 24) |
               (static_cast<std::uint32_t>(std::clamp(rr, 0, 255)) << 16) |
               (static_cast<std::uint32_t>(std::clamp(g, 0, 255)) <<  8) |
               (static_cast<std::uint32_t>(std::clamp(b, 0, 255)));
    };

    // Horizontal pass (sliding window).
    for (int y = 0; y < h; ++y) {
        const std::uint32_t* row = base + static_cast<std::size_t>(y) * stride;
        int sum_a = 0, sum_r = 0, sum_g = 0, sum_b = 0, count = 0;

        const int init_right = std::min(w - 1, r);
        for (int k = 0; k <= init_right; ++k) {
            int pa, pr, pg, pb;
            unpack(row[k], pa, pr, pg, pb);
            sum_a += pa; sum_r += pr; sum_g += pg; sum_b += pb;
            ++count;
        }
        tmp[static_cast<std::size_t>(y) * w] =
            pack(sum_a / count, sum_r / count, sum_g / count, sum_b / count);

        for (int x = 1; x < w; ++x) {
            const int leave = x - r - 1;
            if (leave >= 0) {
                int pa, pr, pg, pb;
                unpack(row[leave], pa, pr, pg, pb);
                sum_a -= pa; sum_r -= pr; sum_g -= pg; sum_b -= pb;
                --count;
            }
            const int enter = x + r;
            if (enter < w) {
                int pa, pr, pg, pb;
                unpack(row[enter], pa, pr, pg, pb);
                sum_a += pa; sum_r += pr; sum_g += pg; sum_b += pb;
                ++count;
            }
            tmp[static_cast<std::size_t>(y) * w + x] =
                pack(sum_a / count, sum_r / count, sum_g / count, sum_b / count);
        }
    }

    // Vertical pass (sliding window).
    for (int x = 0; x < w; ++x) {
        int sum_a = 0, sum_r = 0, sum_g = 0, sum_b = 0, count = 0;

        const int init_bottom = std::min(h - 1, r);
        for (int k = 0; k <= init_bottom; ++k) {
            int pa, pr, pg, pb;
            unpack(tmp[static_cast<std::size_t>(k) * w + x], pa, pr, pg, pb);
            sum_a += pa; sum_r += pr; sum_g += pg; sum_b += pb;
            ++count;
        }
        base[static_cast<std::size_t>(0) * stride + x] =
            pack(sum_a / count, sum_r / count, sum_g / count, sum_b / count);

        for (int y = 1; y < h; ++y) {
            const int leave = y - r - 1;
            if (leave >= 0) {
                int pa, pr, pg, pb;
                unpack(tmp[static_cast<std::size_t>(leave) * w + x], pa, pr, pg, pb);
                sum_a -= pa; sum_r -= pr; sum_g -= pg; sum_b -= pb;
                --count;
            }
            const int enter = y + r;
            if (enter < h) {
                int pa, pr, pg, pb;
                unpack(tmp[static_cast<std::size_t>(enter) * w + x], pa, pr, pg, pb);
                sum_a += pa; sum_r += pr; sum_g += pg; sum_b += pb;
                ++count;
            }
            base[static_cast<std::size_t>(y) * stride + x] =
                pack(sum_a / count, sum_r / count, sum_g / count, sum_b / count);
        }
    }
}

// ── downsample_supersampled ────────────────────────────────────────────────
//
// Box-filter downsample: each destination pixel = mean of ss×ss source pixels.
// Operates on PRGB32 (same channel-independent averaging invariant).  When
// ss == 1 this is a no-op (caller guards).
void downsample_supersampled(
    BLImage& dst, const BLImage& src, int ss,
    TextRunStageState& /* s — kept for sym API w/ static helpers */
) {
    if (ss <= 1) return;

    BLImageData src_data, dst_data;
    if (src.getData(&src_data) != BL_SUCCESS) return;
    if (dst.getData(&dst_data) != BL_SUCCESS) return;

    const int sw = src_data.size.w;
    const int sh = src_data.size.h;
    const int dw = dst_data.size.w;
    const int dh = dst_data.size.h;
    const int src_stride = static_cast<int>(src_data.stride / sizeof(std::uint32_t));
    const int dst_stride = static_cast<int>(dst_data.stride / sizeof(std::uint32_t));
    const std::uint32_t* src_base = static_cast<const std::uint32_t*>(src_data.pixelData);
    std::uint32_t* dst_base = static_cast<std::uint32_t*>(dst_data.pixelData);

    // Clear dst to transparent before downsample (no stale-data leakage).
    {
        BLContext ctx(dst);
        ctx.setCompOp(BL_COMP_OP_SRC_COPY);
        ctx.setFillStyle(BLRgba32(0, 0, 0, 0));
        ctx.fillAll();
        ctx.end();
    }

    const float inv_area = 1.0f / static_cast<float>(ss * ss);

    for (int dy = 0; dy < dh; ++dy) {
        std::uint32_t* dst_row = dst_base + static_cast<std::size_t>(dy) * dst_stride;
        for (int dx = 0; dx < dw; ++dx) {
            const int sx0 = dx * ss;
            const int sy0 = dy * ss;

            float sum_r = 0, sum_g = 0, sum_b = 0, sum_a = 0;
            for (int oy = 0; oy < ss; ++oy) {
                const int sy = std::min(sy0 + oy, sh - 1);
                const std::uint32_t* src_row = src_base + static_cast<std::size_t>(sy) * src_stride;
                for (int ox = 0; ox < ss; ++ox) {
                    const int sx = std::min(sx0 + ox, sw - 1);
                    const std::uint32_t c = src_row[sx];
                    sum_r += static_cast<float>((c >> 16) & 0xFF);
                    sum_g += static_cast<float>((c >>  8) & 0xFF);
                    sum_b += static_cast<float>( c        & 0xFF);
                    sum_a += static_cast<float>((c >> 24) & 0xFF);
                }
            }

            const std::uint32_t r = static_cast<std::uint32_t>(std::clamp(sum_r * inv_area, 0.0f, 255.0f));
            const std::uint32_t g = static_cast<std::uint32_t>(std::clamp(sum_g * inv_area, 0.0f, 255.0f));
            const std::uint32_t b = static_cast<std::uint32_t>(std::clamp(sum_b * inv_area, 0.0f, 255.0f));
            const std::uint32_t a = static_cast<std::uint32_t>(std::clamp(sum_a * inv_area, 0.0f, 255.0f));
            dst_row[dx] = (a << 24) | (r << 16) | (g << 8) | b;
        }
    }
}

// ── force_parallel_mode ────────────────────────────────────────────────────
//
// Read-once env-var toggle (`CHRONON3D_TEXT_BENCH_PARALLEL`).  The bench
// test asserts hash-equality between serial + this flag-set mode.  Current
// raster path does not branch on this flag (it is a marker for future
// parallel work); the flag exists so the goldens can lock the contract
// today BEFORE the parallel code lands.
[[nodiscard]] bool force_parallel_mode() noexcept {
    static const bool enabled = []() noexcept -> bool {
        const char* v = std::getenv("CHRONON3D_TEXT_BENCH_PARALLEL");
        if (v == nullptr) return false;
        if (v[0] == '\0' || v[0] == '0') return false;
        return true;
    }();
    return enabled;
}

} // namespace chronon3d::renderer::text_run_stages
