#pragma once

// ──────────────────────────────────────────────────────────────────────────────
// cpu_gpu_parity.hpp — CPU-vs-GPU parity harness for the CommandPlan surface
// pipeline.
//
// Runs the same CommandPlan twice:
//   1. on a scalar CPU reference that mirrors the GPU compute kernels exactly
//      (transform / blur / composite / color_adjust), producing the oracle;
//   2. on a surface-capable RenderBackend (VulkanBackend) via
//      execute_command_plan().
// Then it compares the output pixels (max/mean delta) and reports wall-clock
// timings + speedup.
//
// The CPU side is the kernel-equivalent scalar reference, NOT SoftwareBackend:
// SoftwareBackend's production blur uses a different (box-filter) algorithm, so
// pixel parity is measured against the scalar implementation of the same math
// the GPU executes — the only definition under which "the GPU rendered the
// right thing" is checkable.  Callers create the surfaces + upload the input
// pixels first; the harness only measures and compares.
// ──────────────────────────────────────────────────────────────────────────────

#include <chronon3d/render_graph/checkbackend.hpp>
#include <chronon3d/render_graph/executor/command_plan_executor.hpp>
#include <chronon3d/runtime/gpu_command_plan.hpp>
#include <chronon3d/runtime/render_surface.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace chronon3d::test {

struct CpuGpuParityResult {
    bool matched{false};
    double cpu_ms{0.0};
    double gpu_ms{0.0};
    double speedup{0.0};  // cpu_ms / gpu_ms; > 1 means the GPU was faster
    graph::PixelCompareResult comparison{};
};

namespace detail {

using runtime::RenderSurfaceHandle;

inline std::size_t pixel_index(std::uint32_t x, std::uint32_t y, std::uint32_t width) {
    return (static_cast<std::size_t>(y) * width + x) * 4;
}

// transform.comp: out(dst) = src(dst - offset) * opacity, else transparent.
inline void cpu_transform(std::vector<float>& dst, const std::vector<float>& src,
                          std::uint32_t w, std::uint32_t h,
                          int offset_x, int offset_y, float opacity) {
    for (std::uint32_t y = 0; y < h; ++y) {
        for (std::uint32_t x = 0; x < w; ++x) {
            const std::int64_t sx = static_cast<std::int64_t>(x) - offset_x;
            const std::int64_t sy = static_cast<std::int64_t>(y) - offset_y;
            const std::size_t out = pixel_index(x, y, w);
            if (sx >= 0 && sy >= 0 && sx < w && sy < h) {
                const std::size_t s = pixel_index(static_cast<std::uint32_t>(sx),
                                                  static_cast<std::uint32_t>(sy), w);
                for (int c = 0; c < 4; ++c) dst[out + c] = src[s + c] * opacity;
            } else {
                dst[out + 0] = 0.0f;
                dst[out + 1] = 0.0f;
                dst[out + 2] = 0.0f;
                dst[out + 3] = 0.0f;
            }
        }
    }
}

// blur.comp: one separable gaussian pass (sigma = max(radius, 0.5)).
inline void cpu_blur(std::vector<float>& dst, const std::vector<float>& src,
                     std::uint32_t w, std::uint32_t h, float radius, bool horizontal) {
    const float sigma = std::max(radius, 0.5f);
    const int extent = std::clamp(static_cast<int>(std::ceil(sigma * 2.0f)), 1, 32);
    for (std::uint32_t y = 0; y < h; ++y) {
        for (std::uint32_t x = 0; x < w; ++x) {
            float acc[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            float wsum = 0.0f;
            for (int off = -extent; off <= extent; ++off) {
                const std::uint32_t sx = horizontal
                    ? static_cast<std::uint32_t>(std::clamp<std::int64_t>(
                          static_cast<std::int64_t>(x) + off, 0, w - 1))
                    : x;
                const std::uint32_t sy = horizontal
                    ? y
                    : static_cast<std::uint32_t>(std::clamp<std::int64_t>(
                          static_cast<std::int64_t>(y) + off, 0, h - 1));
                const float dist = static_cast<float>(off);
                const float weight = static_cast<float>(
                    std::exp(-0.5 * dist * dist / (sigma * sigma)));
                const std::size_t s = pixel_index(sx, sy, w);
                for (int c = 0; c < 4; ++c) acc[c] += src[s + c] * weight;
                wsum += weight;
            }
            const std::size_t out = pixel_index(x, y, w);
            for (int c = 0; c < 4; ++c) dst[out + c] = acc[c] / wsum;
        }
    }
}

// composite.comp: Normal = premultiplied source-over, Add = src + dst.
inline void cpu_composite(std::vector<float>& dst, const std::vector<float>& src,
                          std::uint32_t w, std::uint32_t h, int blend_mode) {
    const std::size_t count = static_cast<std::size_t>(w) * h;
    for (std::size_t p = 0; p < count; ++p) {
        const float src_a = src[p * 4 + 3];
        if (blend_mode == 1) {
            for (int c = 0; c < 4; ++c) dst[p * 4 + c] = src[p * 4 + c] + dst[p * 4 + c];
        } else {
            for (int c = 0; c < 3; ++c) {
                dst[p * 4 + c] = src[p * 4 + c] + dst[p * 4 + c] * (1.0f - src_a);
            }
            dst[p * 4 + 3] = src_a + dst[p * 4 + 3] * (1.0f - src_a);
        }
    }
}

// color_adjust.comp: clamp((rgb + b - 0.5) * contrast + 0.5), then mix tint.
inline void cpu_color_adjust(std::vector<float>& dst, const std::vector<float>& src,
                             std::uint32_t w, std::uint32_t h,
                             float brightness, float contrast,
                             const float tint[3], float tint_amount) {
    const float amount = std::clamp(tint_amount, 0.0f, 1.0f);
    const std::size_t count = static_cast<std::size_t>(w) * h;
    for (std::size_t p = 0; p < count; ++p) {
        if (src[p * 4 + 3] <= 0.0f) {  // transparent passthrough
            for (int c = 0; c < 4; ++c) dst[p * 4 + c] = src[p * 4 + c];
            continue;
        }
        for (int c = 0; c < 3; ++c) {
            const float adjusted = std::clamp(
                (src[p * 4 + c] + brightness - 0.5f) * contrast + 0.5f, 0.0f, 1.0f);
            dst[p * 4 + c] = adjusted * (1.0f - amount) + tint[c] * amount;
        }
        dst[p * 4 + 3] = src[p * 4 + 3];
    }
}

} // namespace detail

/// Execute `plan` with the scalar CPU reference.  `surfaces` maps every handle
/// referenced by `plan` to its descriptor; `input_pixels` supplies the initial
/// pixels for input surfaces.  Returns the pixels of `output_handle`, or an
/// empty vector when a pass kind is unsupported or a referenced surface is
/// missing from `surfaces`.
inline std::vector<float> execute_command_plan_cpu(
    const runtime::CommandPlan& plan,
    const std::unordered_map<runtime::RenderSurfaceHandle, runtime::SurfaceDesc>& surfaces,
    const std::unordered_map<runtime::RenderSurfaceHandle, std::vector<float>>& input_pixels,
    runtime::RenderSurfaceHandle output_handle) {
    std::unordered_map<runtime::RenderSurfaceHandle, std::vector<float>> bufs;
    for (const auto& [handle, desc] : surfaces) {
        bufs[handle].assign(static_cast<std::size_t>(desc.width) * desc.height * 4, 0.0f);
    }
    for (const auto& [handle, pixels] : input_pixels) {
        bufs[handle] = pixels;
    }
    const auto desc_of = [&](runtime::RenderSurfaceHandle h) -> const runtime::SurfaceDesc* {
        const auto it = surfaces.find(h);
        return it == surfaces.end() ? nullptr : &it->second;
    };

    for (const auto& pass : plan.passes.passes) {
        if (const auto* p = std::get_if<runtime::TransformPass>(&pass.params)) {
            const auto* d = desc_of(p->destination);
            const auto* s = desc_of(p->source);
            if (!d || !s) return {};
            detail::cpu_transform(bufs[p->destination], bufs[p->source],
                                  d->width, d->height, p->offset_x, p->offset_y, p->opacity);
        } else if (const auto* p = std::get_if<runtime::BlurPass>(&pass.params)) {
            const auto* d = desc_of(p->destination);
            const auto* s = desc_of(p->source);
            if (!d || !s) return {};
            detail::cpu_blur(bufs[p->destination], bufs[p->source],
                             d->width, d->height, p->radius, p->horizontal != 0);
        } else if (const auto* p = std::get_if<runtime::CompositePass>(&pass.params)) {
            const auto* d = desc_of(p->destination);
            const auto* s = desc_of(p->source);
            if (!d || !s) return {};
            detail::cpu_composite(bufs[p->destination], bufs[p->source],
                                  d->width, d->height, p->blend_mode);
        } else if (const auto* p = std::get_if<runtime::ColorAdjustPass>(&pass.params)) {
            const auto* d = desc_of(p->destination);
            const auto* s = desc_of(p->source);
            if (!d || !s) return {};
            const float tint[3] = {p->tint[0], p->tint[1], p->tint[2]};
            detail::cpu_color_adjust(bufs[p->destination], bufs[p->source],
                                     d->width, d->height, p->brightness, p->contrast,
                                     tint, p->tint_amount);
        } else {
            return {};  // CPU reference does not implement this pass kind yet
        }
    }

    const auto it = bufs.find(output_handle);
    return it == bufs.end() ? std::vector<float>{} : it->second;
}

/// Run `plan` on the scalar CPU reference and on `gpu_backend`, then compare
/// the `output_handle` pixels and report timings + speedup.  The caller must
/// already have created every referenced surface in `registry` and
/// `gpu_backend` and uploaded the input pixels.
inline CpuGpuParityResult run_cpu_gpu_parity(
    graph::RenderBackend& gpu_backend,
    runtime::RenderSurfaceRegistry& registry,
    const runtime::CommandPlan& plan,
    const std::unordered_map<runtime::RenderSurfaceHandle, runtime::SurfaceDesc>& surfaces,
    const std::unordered_map<runtime::RenderSurfaceHandle, std::vector<float>>& input_pixels,
    runtime::RenderSurfaceHandle output_handle,
    const graph::PixelTolerance& tolerance = {}) {
    CpuGpuParityResult result;

    const auto t0 = std::chrono::steady_clock::now();
    const auto reference = execute_command_plan_cpu(plan, surfaces, input_pixels, output_handle);
    result.cpu_ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    if (reference.empty()) {
        return result;  // CPU reference unsupported → matched stays false
    }

    const auto out_it = surfaces.find(output_handle);
    if (out_it == surfaces.end()) {
        return result;
    }
    const std::size_t out_floats =
        static_cast<std::size_t>(out_it->second.width) * out_it->second.height * 4;

    const auto g0 = std::chrono::steady_clock::now();
    const bool executed = runtime::execute_command_plan(gpu_backend, registry, plan);
    std::vector<float> gpu_pixels(out_floats, 0.0f);
    const bool downloaded = executed &&
        gpu_backend.download_surface(output_handle, gpu_pixels).ok();
    result.gpu_ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - g0).count();
    if (!downloaded) {
        return result;
    }

    result.comparison = graph::compare_pixels(reference, gpu_pixels, tolerance);
    result.matched = result.comparison.matched;
    if (result.gpu_ms > 0.0) {
        result.speedup = result.cpu_ms / result.gpu_ms;
    }
    return result;
}

} // namespace chronon3d::test
