#pragma once

#include <chronon3d/assets/asset_preflight_resolver.hpp>
#include <chronon3d/runtime/renderer_warmup.hpp>
#include <chronon3d/runtime/resource_preparation.hpp>
#include <chronon3d/timeline/compiled_composition.hpp>

#include <cstdint>
#include <optional>
#include <string>

namespace chronon3d {

class SoftwareRenderer;

namespace runtime {

struct RenderPreparationOptions {
    PreflightMode preflight_mode{PreflightMode::FullComposition};
    PreparationOptions resources{};
    bool warmup_renderer{true};
    RendererWarmupOptions warmup{};
    Frame reference_frame{0};
};

/// Wall-time breakdown of the synchronous preparation barrier.  A field is
/// engaged only when that phase is actually instrumented; phases not yet
/// separated stay nullopt (emitted as JSON null, never a misleading 0.0).
struct RenderPreparationTimings {
    std::optional<double> asset_preflight_ms;
    std::optional<double> asset_resolve_ms;
    std::optional<double> asset_decode_ms;
    std::optional<double> font_resolve_ms;
    std::optional<double> font_load_ms;
    std::optional<double> text_shape_ms;
    std::optional<double> text_layout_ms;
    std::optional<double> glyph_raster_ms;
    std::optional<double> glyph_atlas_upload_ms;
    std::optional<double> plan_compile_ms;
    std::optional<double> graph_compile_ms;
    std::optional<double> resource_plan_ms;
    std::optional<double> backend_prepare_ms;

    // ── Image asset pipeline sub-timings (prepare barrier) ────────────────
    // `image_io_ms` is not separable from decode in the stb backend and
    // `image_upload_ms` only applies to GPU backends; both stay nullopt and
    // emit as JSON null rather than a misleading 0.0.
    std::optional<double> image_resolve_ms;
    std::optional<double> image_io_ms;
    std::optional<double> image_decode_ms;
    std::optional<double> image_convert_ms;
    std::optional<double> image_upload_ms;
    std::optional<uint64_t> image_decode_count;
    std::optional<uint64_t> image_upload_count;

    // ── Cache efficiency during the prepare barrier (decode/font load) ────
    // Image + font cache hit/miss happen in prepare and are reset after
    // warmup, so they are captured here to survive into the final report.
    std::optional<uint64_t> image_cache_hits;
    std::optional<uint64_t> image_cache_misses;
    std::optional<uint64_t> font_cache_hits;
    std::optional<uint64_t> font_cache_misses;

    /// Add another preparation's timings into this one (phase-by-phase sum).
    /// A phase present in `other` but absent here is copied; a phase absent
    /// in `other` leaves this one untouched.
    void accumulate(const RenderPreparationTimings& other) {
        const auto add = [](std::optional<double>& dst, const std::optional<double>& src) {
            if (src) dst = dst ? std::optional<double>{*dst + *src} : src;
        };
        add(asset_preflight_ms, other.asset_preflight_ms);
        add(asset_resolve_ms, other.asset_resolve_ms);
        add(asset_decode_ms, other.asset_decode_ms);
        add(font_resolve_ms, other.font_resolve_ms);
        add(font_load_ms, other.font_load_ms);
        add(text_shape_ms, other.text_shape_ms);
        add(text_layout_ms, other.text_layout_ms);
        add(glyph_raster_ms, other.glyph_raster_ms);
        add(glyph_atlas_upload_ms, other.glyph_atlas_upload_ms);
        add(plan_compile_ms, other.plan_compile_ms);
        add(graph_compile_ms, other.graph_compile_ms);
        add(resource_plan_ms, other.resource_plan_ms);
        add(backend_prepare_ms, other.backend_prepare_ms);
        add(image_resolve_ms, other.image_resolve_ms);
        add(image_io_ms, other.image_io_ms);
        add(image_decode_ms, other.image_decode_ms);
        add(image_convert_ms, other.image_convert_ms);
        add(image_upload_ms, other.image_upload_ms);
        const auto add_u64 = [](std::optional<uint64_t>& dst, const std::optional<uint64_t>& src) {
            if (src) dst = dst ? std::optional<uint64_t>{*dst + *src} : src;
        };
        add_u64(image_decode_count, other.image_decode_count);
        add_u64(image_upload_count, other.image_upload_count);
        add_u64(image_cache_hits, other.image_cache_hits);
        add_u64(image_cache_misses, other.image_cache_misses);
        add_u64(font_cache_hits, other.font_cache_hits);
        add_u64(font_cache_misses, other.font_cache_misses);
    }
};

struct RenderPreparationResult {
    AssetPreflightResult preflight{};
    std::optional<PreparationError> preparation_error{};
    std::optional<PreparedAssets> prepared_assets{};
    RendererWarmupResult warmup{};
    bool warmup_performed{false};
    RenderPreparationTimings timings{};

    [[nodiscard]] bool ok() const noexcept {
        return preflight.ok() && !preparation_error.has_value();
    }

    [[nodiscard]] std::string diagnostic() const;
};

/// Prepare every resource required by a composition before encoding starts.
///
/// This is a synchronous barrier: composition evaluation and asset checks are
/// completed before optional renderer warmup. It owns no resolver, cache, or
/// mutable render state; all services remain owned by the renderer runtime.
[[nodiscard]] RenderPreparationResult prepare_render(
    SoftwareRenderer* renderer,
    const Composition& composition,
    const RenderPreparationOptions& options = {});

/// Prepare an immutable compiled composition without re-entering the
/// authoring registry or creating a second runtime composition.
[[nodiscard]] RenderPreparationResult prepare_render(
    SoftwareRenderer* renderer,
    const CompiledComposition& compiled,
    const RenderPreparationOptions& options = {});

} // namespace runtime
} // namespace chronon3d
