#include <chronon3d/scene/model/render/render_node_factory.hpp>
#include <chronon3d/scene/builders/text_run_builder.hpp>
#include <chronon3d/text/font_engine.hpp>

#include <utility>

namespace chronon3d {

RenderNode RenderNodeFactory::base(std::pmr::memory_resource* res, std::string name) {
    RenderNode node(res);
    node.name = std::pmr::string{std::move(name), res};
    node.surface_policy = SurfacePolicy::IntrinsicSize;
    node.transform_policy = TransformPolicy::MatrixOnly;
    return node;
}

RenderNode RenderNodeFactory::rect(std::pmr::memory_resource* res, std::string name, const RectParams& p) {
    auto node = base(res, std::move(name));
    node.shape.set_type(ShapeType::Rect);
    node.shape.rect().size = p.size;
    node.shape.rect().stroke = p.stroke.to_shape_stroke();
    node.world_transform.anchor = {p.size.x * 0.5f, p.size.y * 0.5f, 0.0f};
    node.world_transform.position = p.pos;
    node.color = p.color;
    node.fill = p.fill.has_value() ? p.fill->to_fill() : Fill::solid_color(p.color);
    return node;
}

RenderNode RenderNodeFactory::rounded_rect(std::pmr::memory_resource* res, std::string name, const RoundedRectParams& p) {
    auto node = base(res, std::move(name));
    node.shape.set_type(ShapeType::RoundedRect);
    node.shape.rounded_rect().size = p.size;
    node.shape.rounded_rect().radius = p.radius;
    node.shape.rounded_rect().stroke = p.stroke.to_shape_stroke();
    node.corner_radius = p.radius;
    node.world_transform.anchor = {p.size.x * 0.5f, p.size.y * 0.5f, 0.0f};
    node.world_transform.position = p.pos;
    node.color = p.color;
    node.fill = p.fill.has_value() ? p.fill->to_fill() : Fill::solid_color(p.color);
    return node;
}

RenderNode RenderNodeFactory::circle(std::pmr::memory_resource* res, std::string name, const CircleParams& p) {
    auto node = base(res, std::move(name));
    node.shape.set_type(ShapeType::Circle);
    node.shape.circle().radius = p.radius;
    node.shape.circle().stroke = p.stroke.to_shape_stroke();
    node.world_transform.anchor = {p.radius, p.radius, 0.0f};
    node.world_transform.position = p.pos;
    node.color = p.color;
    node.fill = p.fill.has_value() ? p.fill->to_fill() : Fill::solid_color(p.color);
    return node;
}

RenderNode RenderNodeFactory::line(std::pmr::memory_resource* res, std::string name, const LineParams& p) {
    auto node = base(res, std::move(name));
    node.shape.set_type(ShapeType::Line);
    node.shape.line().to = p.to - p.from;
    node.shape.line().thickness = p.thickness;
    node.shape.line().stroke.trim_start = p.stroke.trim_start;
    node.shape.line().stroke.trim_end = p.stroke.trim_end;
    node.shape.line().stroke.enabled = p.stroke.enabled;
    node.shape.line().stroke.color = p.stroke.color;
    node.shape.line().stroke.width = p.stroke.width;
    node.shape.line().stroke.alignment = p.stroke.alignment;
    node.world_transform.position = p.from;
    node.world_transform.anchor = {0, 0, 0};
    node.color = p.color;
    node.fill = Fill::solid_color(p.color);
    return node;
}

RenderNode RenderNodeFactory::path(std::pmr::memory_resource* res, std::string name, PathParams p) {
    auto node = base(res, std::move(name));
    node.shape.set_type(ShapeType::Path);
    node.shape.path().commands = std::move(p.commands);
    node.shape.path().stroke = p.stroke;
    node.shape.path().fill = p.fill;
    node.shape.path().closed = p.closed;
    node.world_transform.position = p.pos;
    node.color = p.color;
    node.fill = p.fill;
    return node;
}

RenderNode RenderNodeFactory::image(std::pmr::memory_resource* res, std::string name, ImageParams p) {
    auto node = base(res, std::move(name));
    node.shape.set_type(ShapeType::Image);
    // TICKET-LAYER-IMAGE-MANIFEST-CLEAN forward-point 0f+ — prefer the
    // manifest-clean `asset_path` field over the deprecated `path`,
    // delegated to the canonical `chronon3d::detail::image_params_resolve_path`
    // helper.  Return-by-value rvalue fits `std::string::operator=` move
    // assignment cleanly (no extra heap allocation beyond the moved-from
    // path's small-string optimization scope).  Cross-link: see
    // `builder_params.hpp` field-order invariant block for the closure
    // lineage of forward-point 0e → 0f+ single-source-of-truth helper.
    node.shape.image().path = chronon3d::detail::image_params_resolve_path(p);
    node.shape.image().size = p.size;
    node.shape.image().fit = p.fit;
    node.shape.image().focal_point = p.focal_point;
    node.shape.image().crop = p.crop;
    node.shape.image().opacity = p.opacity;
    node.shape.image().radius = p.radius;
    node.corner_radius = p.radius;
    node.world_transform.anchor = {p.size.x * 0.5f, p.size.y * 0.5f, 0.0f};
    node.world_transform.position = p.pos;
    node.color = Color{1, 1, 1, p.opacity};
    return node;
}

RenderNode RenderNodeFactory::tiled_image(std::pmr::memory_resource* res, std::string name, ImageParams p) {
    auto node = base(res, std::move(name));
    node.shape.set_type(ShapeType::TiledImage);
    // TICKET-LAYER-IMAGE-MANIFEST-CLEAN forward-point 0f+ — prefer the
    // manifest-clean `asset_path` field over the deprecated `path`,
    // delegated to the canonical `chronon3d::detail::image_params_resolve_path`
    // helper.  Return-by-value rvalue fits `std::string::operator=` move
    // assignment cleanly (no extra heap allocation beyond the moved-from
    // path's small-string optimization scope).  Cross-link: see
    // `builder_params.hpp` field-order invariant block for the closure
    // lineage of forward-point 0e → 0f+ single-source-of-truth helper.
    node.shape.image().path = chronon3d::detail::image_params_resolve_path(p);
    node.shape.image().size = p.size;
    node.shape.image().opacity = p.opacity;
    node.world_transform.position = p.pos;
    node.world_transform.anchor = {0, 0, 0};
    node.color = Color{1, 1, 1, p.opacity};
    return node;
}

RenderNode RenderNodeFactory::grid_background(std::pmr::memory_resource* res, std::string name, const GridBackgroundParams& p) {
    auto node = base(res, std::move(name));
    node.shape.set_type(ShapeType::GridBackground);
    node.surface_policy = SurfacePolicy::ViewportSize;
    node.transform_policy = TransformPolicy::RasterizeAfter;
    node.shape.grid_background().size = p.size;
    node.shape.grid_background().offset = p.offset;
    node.shape.grid_background().bg_color = p.bg_color;
    node.shape.grid_background().grid_color = p.grid_color;
    node.shape.grid_background().spacing = p.spacing;
    node.shape.grid_background().minor_thickness = p.minor_thickness;
    node.shape.grid_background().major_thickness = p.major_thickness;
    node.shape.grid_background().major_every = p.major_every;
    node.shape.grid_background().centered = p.centered;
    node.world_transform.anchor = {0.0f, 0.0f, 0.0f};
    node.color = p.bg_color;
    return node;
}

// NOTE: resolve_text_anchor() is now defined inline in
// include/chronon3d/scene/model/render/render_node_factory.hpp
// and shared with LayerBuilder::build() and RenderNodeFactory::text_run().

// ── M1.5#9 step 2 — RenderNodeFactory::text() ─────────────────────────
//
// Refactor: `text(...)` now wraps the supplied `TextSpec` into a
// `TextRunSpec` and delegates to `materialize_text_run_shape(...)`,
// the single canonical materializer shared with `text_run()` and
// `LayerBuilder::build()`.  The legacy `ShapeType::Text` code path
// that built a `TextShape` (paint / shadows / material / font fields /
// style.{...}/ box.{...}) is REMOVED from this factory.  Step 3
// (M1.5#9) will drop the orphan `create_text_processor()` + factory
// registration in builtin_processors.cpp; step 4 deletes the legacy
// software_text_processor tree; step 5 deletes the legacy rasterizer
// infra.
//
// Back-compat: existing 3-arg callers `text(res, name, TextSpec)`
// still compile and behave identically for non-materialization
// properties (anchor, position, color, fill, layout.box-derived
// transform).  Materialization of paint / shadows / material / font
// glyphs is now ROUTED through `materialize_text_run_shape` so
// downstream consumers see a unified `TextRunShapeHandle` (variant
// index 14).  When `engine == nullptr` the materializer logs an
// `spdlog::error` and returns nullptr — the factory still produces a
// RenderNode with `ShapeType::TextRun` + null `text_run_shape_handle()
// .value` so the renderer-side source-pass routes the entry to
// TextRunNode which surfaces the missing-shape diagnostic.
//
// World-transform / fill mirror the legacy fields that used to be
// carried on TextShape::style: `p.position`, `p.layout.box` (for
// anchor resolution), `p.layout.anchor` (TextAnchor enum), and
// `p.appearance.color` (carry color + solid fill).  These are
// independent of the materializer's runtime work.
RenderNode RenderNodeFactory::text(
    std::pmr::memory_resource* res,
    std::string name,
    TextSpec p,
    FontEngine* engine /* = nullptr, see header */
) {
    auto node = base(res, std::move(name));

    // M1.5#9 step 2: switch to ShapeType::TextRun + delegate to the
    // canonical materializer.  All paint / shadows / material fields
    // that previously lived on TextShape::style are now routed into
    // TextRunShape::paint/material/shadows at materialization time.
    node.shape.set_type(ShapeType::TextRun);

    // ── Read all legacy scalar/string fields BEFORE std::move(p) ──
    // World-transform (independent of materialization).  These mirror
    // the legacy fields that used to live on TextShape::{style,box}
    // so authoring pipeline still resolves text positioning correctly
    // even when materialization fails (e.g. engine == nullptr).
    //
    // ORDERING INVARIANT: read `p.layout.{anchor,box}` and
    // `p.position` and `p.appearance.{color,shadows,paint,material}`
    // HERE, before the std::move(p) into run_spec.text below.
    // Reordering breaks world_transform resolution (the source TextSpec
    // members would be moved-from by the time these reads happen).
    const TextAnchor   box_anchor = p.layout.anchor;
    const Vec2         box_size   = p.layout.box;
    const Vec3         world_pos  = p.position;
    const Color        text_color = p.appearance.color;

    // Fallback: if the caller left font path empty, use the project
    // default (Inter-Bold).  Without this, the renderer would fail to
    // locate a font when materializing via the supplied engine.
    // Preserved from the pre-step-2 implementation for back-compat so
    // external callers that construct minimal `TextSpec{.content = {...}}`
    // (e.g. tests, presets) still resolve a usable font path before
    // the engine path-load kicks in.
    if (p.font.font_path.empty()) {
        p.font.font_path = "assets/fonts/Inter-Bold.ttf";
    }

    node.world_transform.anchor   = resolve_text_anchor(box_anchor, box_size);
    node.world_transform.position = world_pos;
    node.color = text_color;
    node.fill  = Fill::solid_color(text_color);

    // ── Delegate to canonical materializer ──
    //
    // Wrap the legacy TextSpec into the canonical TextRunSpec and
    // delegate to `materialize_text_run_shape(...)`.  cache_layout=true
    // mirrors the TextRunBuilder default; animators/selectors are
    // empty for the simple text() path (callers wanting animated
    // text should use `text_run()` or `LayerBuilder::text_run(...)`).
    TextRunSpec run_spec;
    run_spec.text         = std::move(p);
    run_spec.cache_layout = true;

#ifdef CHRONON3D_USE_BLEND2D
    auto shape = materialize_text_run_shape(run_spec, engine, SampleTime{});
    if (shape) {
        // Materialization succeeded — engine + shaping/compile/cache
        // chain resolved.  Stash the materialized TextRunShape on the
        // handle.  Paint / material / shadows / glyph state live on
        // `shape` so downstream SoftwareBackend::draw_text_run sees
        // a fully-populated payload.
        node.shape.text_run_shape_handle().value = std::move(shape);
    } else {
        // Materialization failed (engine null OR shaping/compile
        // returned Err).  Leave `value = nullptr` so the renderer-
        // side source-pass routes the entry to TextRunNode which
        // emits an `spdlog::error` for missing-shape (graceful
        // degradation).  The factory still flags the entry as
        // ShapeType::TextRun so the registry routes it correctly.
        // PR 8.0 — when engine==nullptr, the materializer emits
        // `spdlog::error("no FontEngine available...")` BEFORE
        // returning; we just sink the nullptr result.
    }
#else  // !CHRONON3D_USE_BLEND2D
    (void)run_spec;
    (void)engine;
    // When BLEND2D is disabled, the materializer symbol is a no-op
    // (see src/scene/builders/text_run_builder.cpp: `#ifdef
    // CHRONON3D_USE_BLEND2D`).  The factory still emits a TextRun
    // node with a null handle so the cluster stays consistent.
#endif

    return node;
}

// ═══════════════════════════════════════════════════════════════════════════
// text_run factory
// ═══════════════════════════════════════════════════════════════════════════

RenderNode RenderNodeFactory::text_run(
    std::pmr::memory_resource* res,
    std::string name,
    TextRunSpec p,    // canonical composable (TextRunSpec was the prior alias)
    FontEngine* engine,
    SampleTime sample_time
) {
    auto node = base(res, std::move(name));
    node.shape.set_type(ShapeType::TextRun);
    node.font_engine = engine;

    // World transform from TextRunSpec (deep-nested field paths).
    node.world_transform.position = p.text.position;
    node.world_transform.anchor = resolve_text_anchor(
        p.text.layout.anchor, p.text.layout.box);

#ifdef CHRONON3D_USE_BLEND2D
    // ── Pass canonical composite TextRunSpec directly ───────────────
    auto shape = materialize_text_run_shape(p, engine, sample_time);
    if (!shape) {
        // Materialization failed (shaping / empty text).  Leave
        // text_run_shape_handle().value null so the graph-builder
        // source-pass routes to TextRunNode, which will emit an
        // `spdlog::error` for missing-shape.  Better than silently
        // dropping the entry.
    } else {
        node.shape.text_run_shape_handle().value = std::move(shape);
    }
#endif

    node.color = p.text.appearance.color;
    node.fill = Fill::solid_color(p.text.appearance.color);
    return node;
}

} // namespace chronon3d
