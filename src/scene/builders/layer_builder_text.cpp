// ============================================================================
// layer_builder_text.cpp — TextRun registration + Layer materialization
// ============================================================================
//
// Contains text-related LayerBuilder methods: text_run(), the text()
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
#include <chronon3d/text/resolve_text_placement.hpp>     // Canonical TextRun placement resolver
#include <chronon3d/layout/layout_solver.hpp>              // Layer pin → Canvas conversion
// TICKET-104 -- internal helper consumed by the per-spec
// materialization site below.  Forward declaration is intentionally
// NOT exposed via the PUBLIC HPP (cat-3 freeze: zero new public
// symbols, even in a sub-namespace).  Relative path matches the
// convention used by tests/text/test_builder_consumed_commit_validation.cpp
// and reach the internalization header directly.
#include "../../text/pending_text_run_impl.hpp"
#include "../../text/prepared_text_internal.hpp"
#include <spdlog/spdlog.h>

#include <cmath>
#include <utility>

namespace chronon3d {

// ═══════════════════════════════════════════════════════════════════════════
// TextRunBuilder — PR 4 (TextAnimator V2)
// ═══════════════════════════════════════════════════════════════════════════

TextRunBuilder& LayerBuilder::text_run(std::string name, PreparedText params) {
    // Text runs use the text-specific coordinate path in the render graph.
    // Keep the layer kind aligned with the primitive being registered;
    // otherwise a normal-layer canvas transform shifts glyphs off-screen.
    m_layer.kind = LayerKind::Text;

    // Sequence V2: collect font asset reference
    if (!params.style.font.font_path.empty()) {
        m_layer.asset_manifest.add_font(
            params.style.font.font_path,
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
    // `.text_run(...)` calls.
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
    // For each PendingTextRun pushed via `LayerBuilder::text_run(name,
    // PreparedText)`, evaluate the animator stack at the layer's
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

            // The pending payload is the one unified text transport for
            // authoring and compiled construction.
            const PreparedText prepared =
                text_internal::normalize_prepared_text(spec.params);
            // The resolver owns TextPlacement, anchor, and box-size
            // semantics. Its layout_origin is Canvas-space; only the
            // Canvas → layer basis conversion belongs at this boundary.
            const CanvasInfo canvas = CanvasInfo::from_dimensions(
                m_screen_width, m_screen_height);
            TextPlacement resolved_placement = prepared.frame.placement;
            Vec2 layer_pin{0.0f, 0.0f};
            if (m_layer.layout.pin.has_value()) {
                layer_pin = anchor_position(
                    *m_layer.layout.pin,
                    static_cast<i32>(m_screen_width),
                    static_cast<i32>(m_screen_height),
                    m_layer.layout.margin);
                if (resolved_placement.kind == TextPlacementKind::Absolute) {
                    // Absolute is local to a pinned layer: lift its offset
                    // into Canvas coordinates before the canonical resolver
                    // computes the anchor-adjusted box origin.
                    resolved_placement.offset += layer_pin;
                }
            }

            const auto resolved = resolve_text_placement(
                canvas,
                prepared.frame.size,
                resolved_placement,
                prepared.frame.anchor);
            Vec2 local_origin = resolved.layout_origin;

            if (m_layer.layout.pin.has_value()) {
                // The layer owns the Canvas pin; the node receives only the
                // resolver's box origin relative to that pin.
                local_origin -= layer_pin;
            } else {
                // The 2D graph basis is centered at (0,0), while the
                // canonical resolver intentionally remains top-left Canvas.
                // Every unpinned layer is shifted by +canvas-half in
                // layer_resolver.cpp (modular path); subtracting the half
                // here keeps the resolved pin (Canvas top-left space for ALL
                // placement kinds, Absolute included per ADR-019) in place
                // after that layer shift.  Exempting Absolute (55d3c590e)
                // double-shifted its explicit pin by exactly one canvas
                // center: the +center comes from the layer world matrix via
                // layer_resolver, not from resolve_text_run_placement, so
                // skipping the compensation moved every unpinned Absolute
                // pin off by (width/2, height/2).
                local_origin -= Vec2{canvas.width * 0.5f, canvas.height * 0.5f};
            }

            node.world_transform.position = Vec3{
                local_origin.x, local_origin.y, 0.0f};
            // resolve_text_placement() already applied the box anchor to
            // layout_origin. Keeping the node anchor zero prevents a second
            // anchor subtraction in Transform::to_mat4().
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
                // ANCHOR EXACTLY-ONCE CONTRACT:
                // resolve_text_placement() already consumed the box anchor
                // when it computed layout_origin (= pin − anchor_offset),
                // and world_transform.anchor above stays {0,0,0} so
                // Transform::to_mat4() must not subtract it again.
                // Setting the transform anchor here re-applied T(−anchor)
                // a second time, shifting the rendered ink by exactly
                // −(box_size/2) for TextAnchor::Center (observed as the
                // pinned-absolute off-center regression). Glyphs are
                // box-local; the node matrix maps them directly.
                node.shape.text_run_shape_handle().value = std::move(shape);
            }
#endif
            (void)chronon3d::text_internal::mark_consumed(spec);
        }
    }

    // Text materialization can conservatively mark a layer static when the
    // text payload itself has no animators.  Layer animation is configured
    // independently afterwards (for example opacity_anim() in benchmark
    // scenes), so the final layer is dynamic whenever any evaluated
    // transform property depends on time.  Leaving cache_static set here
    // freezes the first transparent frame and makes an entire clip appear
    // watermark-only even though the animation is authored correctly.
    if (m_layer.anim_transform.is_time_dependent()) {
        m_layer.cache_static = false;
    }

    return std::move(m_layer);
}

} // namespace chronon3d
