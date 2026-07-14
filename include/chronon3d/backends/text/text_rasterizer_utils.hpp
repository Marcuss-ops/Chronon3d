#pragma once

#include <chronon3d/scene/model/shape/shape.hpp>
#include <chronon3d/text/text_material.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/assets/asset_resolver.hpp>  // WP-8 PR 8.0 — explicit-resolver plumbing
#ifdef CHRONON3D_USE_BLEND2D
#include <blend2d.h>
#endif
#include <optional>

// TICKET-007 - per-instance debug gating. Pull in DebugConfig's full
// definition so `rasterize_text_to_bl_image`'s
// `const chronon3d::DebugConfig*` parameter is a complete type at every
// call-site (replaces the previously-removed `detail::g_debug_config`).
#include <chronon3d/core/config.hpp>

namespace chronon3d {

class FontEngine;  // forward declaration

#ifdef CHRONON3D_USE_BLEND2D

struct TextRasterization {
    BLImage image;
    float x_offset;
    float y_offset;
    BLTextMetrics metrics;
    BLFont font;
};

/// `resolver` is the explicit AssetResolver sourced by the caller
/// (typically `sw_renderer->runtime().resolver()` for production paths,
/// or `runtime::typed_resolver_for_deep_code()` for test/dev-climate
/// paths that don't have a runtime in scope).  WP-8 PR 8.0 mandates the
/// explicit pass; the deep cache (`Blend2DResources::get_face`) +
/// FreeType path-builder (`FtGlyphPathBuilder::load_face`) used to read
/// `runtime::typed_resolver_for_deep_code()` directly, but per PR 8.0
/// they now each receive the resolver as an argument.
///
/// `font_engine` is REQUIRED (WP-8 PR 8.1 — no nullable, no
/// process-global fallback, no per-call construction).  Production
/// source is `sw_renderer->font_engine()` (the per-renderer hoist);
/// CLI/dev/test sources construct their own `FontEngine{resolver}` on
/// the localised resolver.
///
/// `debug_cfg` is the per-instance DebugConfig pointer forwarded from
/// the owning RenderGraphContext / SoftwareRenderer.  When nullptr,
/// debug overlays are disabled (matches the safe default for test /
/// diagnostic paths that build a `RenderGraphContext` without
/// populating `options::debug_config`).  See TICKET-007.
std::optional<TextRasterization> rasterize_text_to_bl_image(
    const TextShape& text,
    float effective_size,
    int padding,
    const chronon3d::assets::AssetResolver& resolver,
    bool* cache_hit,
    const Mat4* transform,
    FontEngine& font_engine,
    const chronon3d::DebugConfig* debug_cfg = nullptr
);

/// Apply TextMaterial effects (gradient, bevel, highlight, shade, emissive)
/// to a rasterized text BLImage in-place.
void apply_text_material(BLImage& img, const TextMaterial& mat);

#endif // CHRONON3D_USE_BLEND2D

// P1-8: `set_text_cache_capacity` + `clear_text_raster_cache` are DELETED.
// Their state moved to `TextRenderResources::raster_cache` (see
// `text_render_resources.{hpp,cpp}`); production callers inject + clear
// via `sw_renderer->text_render_resources()->set_raster_cache_capacity(N)`
// / `clear_raster_cache()`.

uint64_t hash_text_style(
    const TextShape& t,
    float effective_size,
    int padding,
    const Mat4* transform = nullptr
);

} // namespace chronon3d
