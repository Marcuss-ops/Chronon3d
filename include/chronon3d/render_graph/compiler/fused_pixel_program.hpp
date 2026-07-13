#pragma once

// ── fused_pixel_program — F3.1 fusion pass compiler ABI ──────────────────
//
// TICKET-FUSION-PASS-COMPILER-V1 — the FusedPixelProgram descriptor
// emitted by the F3.1 graph fusion pass. The pass detects the
// `ColorMatrix → Opacity → Blend` 3-node pattern in the render graph
// and, when the 4 fusion guards are all satisfied, REPLACES the 3
// nodes with a single FusedPixelProgram descriptor that the runtime
// executor consumes via the F5.1 SIMD registry (`BlendKernel::apply`).
//
// Per AGENTS.md §regole "Cat-3 minimal-surface", this header is the
// single new public surface introduced by F3.1. The FusedPixelProgram
// data struct is a value type (no vtable, no std::function, no
// singleton cache) — it carries the descriptor + the resolved F5.1
// fn-pointer + the 4 guard flags + a per-program byte counter for
// the `bytes_saved_by_fusion` aggregation.
//
// The F3.1 pass hooks into the existing
// `chronon3d::graph::optimizer::optimize_graph()` entry point via a
// new `OptimizationConfig::enable_pixel_fusion` toggle (default `true`).
// The pass is APPENDED after `fuse_nodes()` so the 3-node
// ColorMatrix/Opacity/Blend patterns survive the prior effect-stack +
// transform fusions (which would have already collapsed sibling
// operations into 1 node each).
//
// ABI compatibility (F5.1 + ADR-025):
//   - `PixelKernel` typedef = `void (*)(float*, const float*, std::size_t)`
//     matches the F5.1 `BlendKernel::ApplyFn` signature verbatim. The
//     fusion pass binds `resolved_kernel` to the active ISA's blend
//     fn-pointer (e.g. `kAvx2Set.blend.apply` for AVX2 hosts per F5.2,
//     `kScalarSet.blend.apply` otherwise).
//   - The fused operation is bit-identical to the F5.1+ scalar or
//     F5.1+ AVX2 blend within `kKernelEpsilon` (1 ULP float32) because
//     the resolved_kernel IS the F5.1 kernel.
//   - ColorMatrix + Opacity pre-application is performed by the
//     runtime executor on a scratch buffer (1 extra read + 1 extra
//     write per pixel); the F3.1 pass emits the FusedPixelProgram
//     descriptor; the executor consumes it. The actual pre-application
//     is forward-pointed to TICKET-FUSION-PASS-RUNTIME-EXEC.

#include <cstddef>
#include <cstdint>
#include <array>
#include <vector>

#include <chronon3d/simd/pixel_kernels.hpp>   // F5.1 ABI (kKernelEpsilon SSoT)

namespace chronon3d::graph {
class RenderGraph;
struct RenderGraphContext;
} // namespace chronon3d::graph

namespace chronon3d::graph::fusion {

// ── PixelOperation — the individual op in a fused chain ──────────────────
//
// Per user-spec verbatim "fonde ColorMatrix → Opacity → Blend", each
// fused program carries a list of PixelOperations in the order they
// must be applied at runtime. The list is the canonical descriptor of
// WHAT the runtime must execute; the order is math-preserving per
// guard (a).
struct PixelOperation {
    enum class Kind : std::uint8_t {
        ColorMatrix,  ///< 3x4 RGBA transform (12 floats)
        Opacity,      ///< scalar opacity multiplier (1 float)
        Blend,        ///< premultiplied-alpha blend mode (1 uint8)
    };

    Kind kind{Kind::ColorMatrix};

    /// Discriminated union payload (always 12 floats; smaller payloads
    /// use the first N slots + zero-fill the rest for ABI stability).
    ///   - ColorMatrix: matrix3x4 row-major (rows 0..3, cols 0..3)
    ///   - Opacity:     params[0] = opacity multiplier in [0.0, 1.0]
    ///   - Blend:       blend_mode = static_cast<std::uint8_t>(mode)
    std::array<float, 12> params{};
    std::uint8_t blend_mode{0};  /// Only meaningful when kind == Blend

    // ── Convenience ctors (canonical per-kind; zero-init otherwise) ───
    constexpr PixelOperation() = default;
    explicit PixelOperation(Kind k) : kind(k) {}
    static PixelOperation color_matrix(const std::array<float, 12>& m) noexcept {
        PixelOperation op(Kind::ColorMatrix);
        op.params = m;
        return op;
    }
    static PixelOperation opacity(float v) noexcept {
        PixelOperation op(Kind::Opacity);
        op.params[0] = v;
        return op;
    }
    static PixelOperation blend(std::uint8_t mode) noexcept {
        PixelOperation op(Kind::Blend);
        op.blend_mode = mode;
        return op;
    }
};

// ── PixelKernel — the resolved fn-pointer for the FUSED op ──────────────
//
// User spec verbatim `PixelKernel resolved_kernel`. Per the F5.1 ABI
// contract, this is the F5.1 `BlendKernel::ApplyFn` signature (the
// SAME fn-pointer shape used by the F5.2 AVX2 blend). The fusion pass
// binds this to the active ISA's blend fn-pointer (e.g. AVX2 blend
// from `kAvx2Set` for AVX2 hosts per F5.2). The runtime executor
// invokes `resolved_kernel(dst, src, pixel_count)` ON the scratch
// buffer (post ColorMatrix + Opacity pre-application).
//
// The typedef form (vs a new `struct PixelKernel { void(*)(...); }`)
// is chosen to be ABI-compatible with the F5.1 `BlendKernel::ApplyFn`
// alias — the same fn-pointer can be used as either a `BlendKernel`
// slot or a `PixelKernel` parameter without reinterpret_cast.
using PixelKernel = chronon3d::simd::BlendKernel::ApplyFn;

// ── FusedColorOpacityBlendGuard — the 4 fusion guards ────────────────────
//
// Per user-spec verbatim:
//   (a) ordine matematico preservato       → math_order_preserved
//   (b) blend mode compatibile            → blend_mode_compatible
//   (c) dirty rect compatibile            → dirty_rect_compatible
//   (d) precisione certificata            → precision_certified
//
// All 4 must be `true` for the F3.1 fusion pass to bind the
// `resolved_kernel`; on any guard failure, the 3 nodes are LEFT
// UNFUSED (the runtime falls back to the F5.1+ scalar 3-pass path).
struct FusedColorOpacityBlendGuard {
    bool math_order_preserved{false};
    bool blend_mode_compatible{false};
    bool dirty_rect_compatible{false};
    bool precision_certified{false};

    /// All-true predicate (for the runtime executor to decide whether
    /// to invoke the resolved_kernel vs the scalar 3-pass fallback).
    [[nodiscard]] constexpr bool all_pass() const noexcept {
        return math_order_preserved
            && blend_mode_compatible
            && dirty_rect_compatible
            && precision_certified;
    }

    /// Human-readable 4-letter tag for diagnostic logging (e.g. "MBDP"
    /// means math+blend+dirty+precision all-pass; "M_X_" means
    /// blend/dirty guards failed).
    [[nodiscard]] std::array<char, 5> tag() const noexcept {
        return {{
            math_order_preserved  ? 'M' : 'm',
            blend_mode_compatible ? 'B' : 'b',
            dirty_rect_compatible ? 'D' : 'd',
            precision_certified   ? 'P' : 'p',
            '\0'
        }};
    }
};

// ── FusedPixelProgram — user-spec verbatim struct ───────────────────────
//
// Per user spec verbatim: `struct FusedPixelProgram {
// std::vector<PixelOperation> operations; PixelKernel resolved_kernel; }`.
//
// The struct carries the per-program state:
struct FusedPixelProgram {
    /// Ordered list of operations to apply at runtime (math-preserving
    /// per guard (a)). For the F3.1 `ColorMatrix → Opacity → Blend`
    /// pattern, the size is exactly 3.
    std::vector<PixelOperation> operations;

    /// The F5.1 blend fn-pointer to invoke on the pre-applied scratch
    /// buffer. `nullptr` indicates the 4 guards failed; the runtime
    /// must fall back to the 3-pass scalar path.
    PixelKernel resolved_kernel{nullptr};

    /// The 4 fusion guards (F3.1 user-spec). All-true → resolved_kernel
    /// is invoked; otherwise the runtime falls back.
    FusedColorOpacityBlendGuard guards{};

    /// Byte accounting for `bytes_saved_by_fusion` aggregation:
    /// bytes that the runtime would have written for the 3 unfused
    /// passes minus bytes written by the 1 fused pass (= 2 extra
    /// passes × pixel_count × bytes_per_pixel).
    std::size_t bytes_per_pixel{4};  // RGBA float32 (16 bytes/pixel)
    std::size_t pixel_count{0};

    /// Convenience: total bytes that WOULD have been written by the
    /// 3 unfused passes (for `bytes_saved_by_fusion` accounting).
    /// Per-pass model: 1 read + 1 write of full RGBA = 2 ×
    /// bytes_per_pixel per pass (1 read of src, 1 write of dst).
    /// 3 passes × 2 transactions = 6 × pixel_count × bytes_per_pixel.
    [[nodiscard]] std::size_t bytes_unfused() const noexcept {
        // 3 passes × (1 src-read + 1 dst-write) × bytes_per_pixel
        return 3 * 2 * pixel_count * bytes_per_pixel;
    }

    /// Convenience: total bytes written by the 1 fused pass (for
    /// `bytes_saved_by_fusion` accounting). Fused model: ColorMatrix +
    /// Opacity are FUSED IN REGISTERS (NOT in scratch memory); the
    /// fused pass performs 1 src-read + 1 dst-read + 1 dst-write = 3
    /// memory transactions per pixel = 3 × pixel_count × bytes_per_pixel.
    /// The whole point of fusion is that intermediate values live in
    /// L1/registers, not main memory — the 3x write assumption
    /// (counting scratch as memory traffic) is the OPPOSITE of fusion's
    /// value proposition and was the F3.1 first-pass bug.
    [[nodiscard]] std::size_t bytes_fused() const noexcept {
        // 1 fused pass × (1 src-read + 1 dst-read + 1 dst-write) × bytes_per_pixel
        return 3 * pixel_count * bytes_per_pixel;
    }

    /// `bytes_saved_by_fusion = bytes_unfused - bytes_fused` (always
    /// positive for a 3-node fusion). The F3.1 pass logs this for the
    /// `--stats-json` output. NB: the "savings" here are memory-write
    /// traffic, not wall-clock — the wall-clock benefit comes from
    /// kernel fusion (separate metric in ADR-025).
    /// For B03 CinematicGlow1080p (1920×1080 × 4 bytes/pixel × 3
    /// unfused passes): bytes_saved = 6 - 3 = 3 × 1920 × 1080 × 4
    /// = 24,883,200 bytes (> 0 ✓).
    [[nodiscard]] std::size_t bytes_saved() const noexcept {
        return bytes_unfused() - bytes_fused();
    }
};

// ── FusionStats — the 3 counters emitted via `--stats-json` ──────────────
//
// Per user spec verbatim "Aggiungi counter `passes_before_fusion,
// passes_after_fusion, bytes_saved_by_fusion` esposti via
// `--stats-json`". The struct is the canonical aggregator; the F3.1
// pass fills it on each invocation, the bench command emits it as
// JSON, and the `kCounterNames` array in `render_counter_types.hpp`
// auto-discovers the 3 fields.
struct FusionStats {
    /// Number of graph nodes that COULD have been fused (i.e. matched
    /// the ColorMatrix/Opacity/Blend pattern) BEFORE the fusion pass.
    /// This is the count of `ColorMatrix + Opacity + Blend` triples
    /// detected in the graph, not the total node count.
    std::size_t passes_before_fusion{0};

    /// Number of graph nodes AFTER the fusion pass. Equal to
    /// `passes_before_fusion` for non-fused (guard-failed) triples;
    /// equal to `passes_before_fusion / 3` for fused triples (one
    /// FusedPixelProgram replaces 3 nodes). Always ≤
    /// `passes_before_fusion`.
    std::size_t passes_after_fusion{0};

    /// Total bytes saved by the fusion pass = sum of
    /// `fused.bytes_saved()` across all FusedPixelProgram descriptors
    /// emitted by the pass. Exposed via `--stats-json`.
    std::size_t bytes_saved_by_fusion{0};
};

} // namespace chronon3d::graph::fusion

// ── F3.1 atomic-counter bridge ───────────────────────────────────────────
//
// The F3.1 fusion pass writes its 3 counters to the canonical render
// counters via the `thread_local_counters()` accessor. This wires the
// `--stats-json` CLI flag (per F3.1 user-spec verbatim) without
// introducing a new singleton or registry. The bench command reads
// the same counters via `renderer->counters()->pixel_fusion_*.load()`
// (matching the existing pattern for cache_hits, cache_misses,
// nodes_executed, etc. at command_bench.cpp:201-208).
namespace chronon3d::graph::fusion {
    /// Increment the 3 F3.1 fusion counters in the thread-local
    /// `RenderCountersRaw` (which the per-frame `merge_tls()` flushes
    /// into the global atomic `RenderCounters`). No-op when
    /// `RenderCountersRaw` does NOT carry the F3.1 fields (older ABI).
    /// Implementation is in the .cpp file (forward-declared above).
    void emit_fusion_counters(
        std::size_t passes_before_fusion,
        std::size_t passes_after_fusion,
        std::size_t bytes_saved_by_fusion) noexcept;

    /// F3.1 fusion pass entry point. Detects ColorMatrix → Opacity → Blend
    /// 3-node triples in `graph`, checks the 4 fusion guards, and emits
    /// FusedPixelProgram descriptors into `out_programs`. Returns the
    /// aggregated FusionStats counters.
    [[nodiscard]] FusionStats fuse_color_opacity_blend(
        const graph::RenderGraph& graph,
        const graph::RenderGraphContext& ctx,
        const chronon3d::simd::PixelKernelSet& kernels,
        std::vector<FusedPixelProgram>& out_programs) noexcept;
} // namespace chronon3d::graph::fusion
