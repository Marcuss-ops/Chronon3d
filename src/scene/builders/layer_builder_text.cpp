// ============================================================================
// layer_builder_text.cpp — TextRun registration + Layer materialization
// ============================================================================
//
// Contains text-related LayerBuilder methods: animated_text(), the text()
// overloads, and the final Layer::build() materialization (which resolves
// timing, bakes animated transforms/effects, and materialises pending
// text-run specs into RenderNodes).
//
// Extracted from layer_builder_compile.cpp as part of the domain split
// (core, transform, layout, text, shapes, effects, media, masks).
// ============================================================================

#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/scene/builders/text_run_builder.hpp>
#include <chronon3d/scene/model/render/render_node_factory.hpp>
#include <chronon3d/math/transform.hpp>
#include <chronon3d/core/types/sample_time.hpp>
#include <chronon3d/effects/effect_ids.hpp>
#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/text/text_run.hpp>
#include <chronon3d/text/text_animator_property.hpp>
#include <chronon3d/text/text_definition.hpp>
#include <chronon3d/text/prepared_text.hpp>
#include <chronon3d/text/resolve_text_placement.hpp>     // F2 — resolve_placement_origin for build-time semantic placement
// TICKET-104 -- internal helper consumed by the per-spec
// materialization site below.  Forward declaration is intentionally
// NOT exposed via the PUBLIC HPP (cat-3 freeze: zero new public
// symbols, even in a sub-namespace).  Relative path matches the
// convention used by tests/text/test_builder_consumed_commit_validation.cpp
// and reach the internalization header directly.
#include "../../text/pending_text_run_impl.hpp"
#include <spdlog/spdlog.h>

#include <cmath>
#include <utility>

namespace chronon3d {

// ═══════════════════════════════════════════════════════════════════════════
// TextRunBuilder — PR 4 (TextAnimator V2)
// ═══════════════════════════════════════════════════════════════════════════

TextRunBuilder& LayerBuilder::animated_text(std::string name, TextRunSpec params) {
    // Text runs use the text-specific coordinate path in the render graph.
    // Keep the layer kind aligned with the primitive being registered;
    // otherwise a normal-layer canvas transform shifts glyphs off-screen.
    m_layer.kind = LayerKind::Text;

    // Apply layer-level defaults for font path and font size.
    // Font path: only applied when the caller left it empty.
    // Font size: always applied when set (callers can override per-run
    // by chaining .font_size() on the returned TextRunBuilder).
    if (params.text.font.font_path.empty() && !m_default_font_path.empty()) {
        params.text.font.font_path = m_default_font_path;
    }
    if (m_default_font_size.has_value()) {
        params.text.font.font_size = *m_default_font_size;
    }

    // Sequence V2: collect font asset reference
    if (!params.text.font.font_path.empty()) {
        m_layer.asset_manifest.add_font(
            params.text.font.font_path,
            std::string(m_layer.name) + "/" + name);
    }

    auto spec_uptr = std::make_unique<PendingTextRun>(PendingTextRun{
        .name = std::move(name),
        .params = std::move(params),
        .consumed = false,
    });
    PendingTextRun* spec_ptr = spec_uptr.get();
    m_text_runs.push_back(std::move(spec_uptr));
    // Push a fresh builder into the pool, keyed to the same spec we
    // just added.  The builder holds a non-owning pointer so its
    // mutators write directly into the spec inside m_text_runs.
    // Pool storage means the returned reference stays
    // valid for the lifetime of the LayerBuilder — even across many
    // `.animated_text(...)` calls.
    m_text_run_builders.push_back(
        std::make_unique<TextRunBuilder>(this, spec_ptr));
    return *m_text_run_builders.back();
}

Layer LayerBuilder::build() {
    if (m_until_frame && !m_duration_explicit) {
        m_layer.duration = *m_until_frame - m_layer.from;
    }

    if (m_layer.depth_role != DepthRole::None) {
        m_layer.transform.position.z =
            DepthRoleResolver::z_for(m_layer.depth_role) + m_layer.depth_offset;
    }
    m_layer.font_engine = m_font_engine;
    // Evaluate transform when ANY component is time-dependent (keyframes OR expressions).
    // Expression-only properties (e.g. "sin(time * 2)") have no keyframes, so
    // is_animated() alone would skip evaluation — causing stale values.
    if (m_layer.anim_transform.is_time_dependent()) {
        const SampleTime local_time = m_layer.local_time(m_current_time);
        Transform baked = m_layer.anim_transform.evaluate(local_time);
        if (m_layer.anim_transform.position.is_time_dependent())
            m_layer.transform.position = baked.position;
        if (m_layer.anim_transform.rotation_euler.is_time_dependent())
            m_layer.transform.rotation = baked.rotation;
        if (m_layer.anim_transform.scale.is_time_dependent())
            m_layer.transform.scale = baked.scale;
        if (m_layer.anim_transform.anchor.is_time_dependent())
            m_layer.transform.anchor = baked.anchor;
        if (m_layer.anim_transform.opacity.is_time_dependent())
            m_layer.transform.opacity = baked.opacity;
    }
    // Bake animated blur into the effect stack at the current sub-frame time.
    // Blur can also be expression-only.
    if (m_layer.anim_transform.blur.is_time_dependent()) {
        const SampleTime local_time = m_layer.local_time(m_current_time);
        f32 blur_radius = m_layer.anim_transform.blur.evaluate(local_time);
        bool found = false;
        for (auto& effect : m_layer.effects()) {
            if (auto* blur = std::get_if<BlurParams>(&effect.params)) {
                blur->radius = blur_radius;
                found = true;
                break;
            }
        }
        if (!found) {
            m_layer.effects().push_back(EffectInstance{
                effects::EffectDescriptor{.id = std::string{effects::ids::BlurGaussian}},
                BlurParams{blur_radius}
            });
        }
    }

    // ── PR 4 — Materialize pending text-run specs ───────────────────
    //
    // For each PendingTextRun pushed via `LayerBuilder::animated_text(name,
    // TextRunSpec)`, evaluate the animator stack at the layer's
    // current local time and append a corresponding RenderNode
    // flagged with ShapeType::TextRun.  The graph-builder
    // source-pass (PR 3) auto-routes these to a TextRunNode.
    //
    // Each entry uses the layer's FontEngine if one was set, falling
    // back to the process-wide shared FontEngine.  Shaping failures
    // log warn-level and skip the entry (the layer otherwise renders
    // as a normal Layer — explicit empty-place behavior).
    //
    // P1 refactor note — the placeholder RenderNode is ALWAYS emitted
    // (ShapeType::TextRun, name set, default transform / color / fill)
    // even when CHRONON3D_USE_BLEND2D is undefined.  Without this,
    // BLEND2D-less builds silently swallow every text-run entry and
    // `built.nodes` ends up empty for text-only layers.  The shape
    // materialization itself stays gated on BLEND2D (it requires the
    // harfbuzz-shaped TextLayout / TextRunShape builders); the empty
    // `text_run_shape_handle().value` matches the per-failure semantics
    // graph_builder_source_pass already emits a one-shot
    // `spdlog::error` for.
    if (!m_text_runs.empty()) {
        const SampleTime local_time = m_layer.local_time(m_current_time);
        std::pmr::memory_resource* res = m_layer.nodes.get_allocator().resource();

        for (auto& spec_uptr : m_text_runs) {
            PendingTextRun& spec = *spec_uptr;
            if (spec.consumed) continue;

            RenderNode& node = m_layer.nodes.emplace_back(res);
            node.name = std::pmr::string{spec.name, res};
            node.shape.set_type(ShapeType::TextRun);
            node.font_engine = m_font_engine;

            // X2 canonical static-text path: use PreparedText directly.
            if (spec.prepared.has_value()) {
                const PreparedText& prepared = *spec.prepared;

                node.world_transform.position = Vec3{
                    prepared.frame.placement.offset.x,
                    prepared.frame.placement.offset.y,
                    0.0f
                };
                node.world_transform.anchor = Vec3{0.0f, 0.0f, 0.0f};
                node.world_transform.scale = Vec3{1.0f, 1.0f, 1.0f};
                node.world_transform.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
                node.color = prepared.style.color;
                node.fill = Fill::solid_color(prepared.style.color);

#ifdef CHRONON3D_USE_BLEND2D
                FontEngine* engine_for_shape = spec.font_engine ? spec.font_engine : m_font_engine;
                auto shape = materialize_prepared_text(
                    prepared, engine_for_shape, local_time, spec.animated_doc);
                if (shape) {
                    shape->placement_kind = prepared.frame.placement.kind;
                    node.world_transform.anchor = resolve_text_anchor(
                        prepared.frame.anchor,
                        shape->layout
                            ? shape->layout->bounds
                            : prepared.frame.size);
                    node.shape.text_run_shape_handle().value = std::move(shape);
                }
#endif
                (void)chronon3d::text_internal::mark_consumed(spec);
                continue;
            }

            // Legacy TextRunSpec path (kept for animated_text and deprecated
            // text_run entry points until X5).
            node.world_transform.position = Vec3{
                spec.params.text.placement.offset.x,
                spec.params.text.placement.offset.y,
                0.0f
            };
            // The raster surface is sized to the laid-out ink, not to the
            // authored layout box.  Resolve the anchor against that actual
            // surface after materialization so CanvasCenter centers the ink
            // exactly once while TopLeft remains box-local.
            node.world_transform.anchor = Vec3{0.0f, 0.0f, 0.0f};
            node.world_transform.scale = Vec3{1.0f, 1.0f, 1.0f};
            node.world_transform.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
            node.color = spec.params.text.appearance.color;
            node.fill = Fill::solid_color(spec.params.text.appearance.color);

#ifdef CHRONON3D_USE_BLEND2D
            // Per-spec FontEngine override (set via trb.font_engine(...))
            // wins over the layer's default font_engine when present.
            FontEngine* engine_for_shape = spec.font_engine ? spec.font_engine : m_font_engine;
            TextRunSpec materialize_params = spec.params;
            if (materialize_params.text.placement.kind ==
                TextPlacementKind::CanvasCenter) {
                // CanvasCenter already centers the rasterized ink.  Applying
                // box-relative vertical middle alignment as well would add
                // the same centering offset a second time.
                materialize_params.text.layout.vertical_align = VerticalAlign::Top;
            }

            auto shape = materialize_text_run_shape(
                materialize_params, engine_for_shape, local_time,
                spec.animated_doc);
            if (shape) {
                shape->placement_kind = materialize_params.text.placement.kind;
                node.world_transform.anchor = resolve_text_anchor(
                    materialize_params.text.layout.anchor,
                    shape->layout
                        ? shape->layout->bounds
                        : materialize_params.text.layout.box);
                node.shape.text_run_shape_handle().value = std::move(shape);
            }
#endif
            (void)chronon3d::text_internal::mark_consumed(spec);
        }
    }

    return std::move(m_layer);
}

} // namespace chronon3d
