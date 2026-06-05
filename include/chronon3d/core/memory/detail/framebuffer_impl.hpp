#pragma once

// Internal implementation helpers for Framebuffer.
// Included only by framebuffer.hpp (inside namespace chronon3d) to keep
// the class declaration concise.
// NOT part of the public API — do not include directly.

// ── Allocation tracking ────────────────────────────────────────────

inline void framebuffer_increment_allocations(size_t bytes) {
    uint64_t current = profiling::g_live_framebuffer_bytes.fetch_add(bytes, std::memory_order_relaxed) + bytes;
    uint64_t peak = profiling::g_peak_live_framebuffer_bytes.load(std::memory_order_relaxed);
    while (current > peak && !profiling::g_peak_live_framebuffer_bytes.compare_exchange_weak(peak, current, std::memory_order_relaxed)) {}
    if (profiling::g_current_counters) profiling::g_current_counters->framebuffer_allocations.fetch_add(1, std::memory_order_relaxed);
}

inline void framebuffer_decrement_allocations(size_t bytes) {
    uint64_t prev = profiling::g_live_framebuffer_bytes.load(std::memory_order_relaxed);
    uint64_t next = (prev > bytes) ? (prev - bytes) : 0;
    while (!profiling::g_live_framebuffer_bytes.compare_exchange_weak(prev, next, std::memory_order_relaxed)) {
        next = (prev > bytes) ? (prev - bytes) : 0;
    }
}

// ── Validation ─────────────────────────────────────────────────────

inline void framebuffer_validate_dimensions(i32 width, i32 height) {
    if (width <= 0 || height <= 0) {
        throw std::invalid_argument("Framebuffer dimensions must be positive");
    }
}

inline bool framebuffer_is_clear_color(const Color& color) {
    return color.r == 0.0f && color.g == 0.0f && color.b == 0.0f && color.a == 0.0f;
}

// ── Clear helpers ──────────────────────────────────────────────────

inline void framebuffer_clear_contiguous(Color* data, usize pixel_count, const Color& color) {
    simd::clear_framebuffer(data, static_cast<int>(pixel_count), color);
}

inline void framebuffer_clear_strided(Color* data, i32 allocated_width, i32 x, i32 y, i32 w, i32 h, const Color& color) {
    if (w <= 0 || h <= 0) return;
    // Fast-path: when the clip spans the entire row width, use a single
    // contiguous clear on the whole rectangle instead of h per-row calls.
    // This enables memset for zero fills (the common case for ClearNode)
    // to operate on a contiguous block, which is ~2-4× faster than
    // per-row memset because the CPU can stream writes through L1 cache
    // without evicting cache lines between row boundaries.
    if (x == 0 && w == allocated_width) {
        framebuffer_clear_contiguous(data + static_cast<usize>(y) * allocated_width,
                                      static_cast<usize>(h) * allocated_width, color);
        return;
    }
    Color* row = data + static_cast<usize>(y) * allocated_width + x;
    for (i32 yy = 0; yy < h; ++yy) {
        simd::clear_framebuffer(row, w, color);
        row += static_cast<size_t>(allocated_width);
    }
}
