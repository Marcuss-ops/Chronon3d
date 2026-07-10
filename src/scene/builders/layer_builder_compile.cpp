// ============================================================================
// layer_builder_compile.cpp — TextRun registration + Layer materialization
// ============================================================================
//
// FASE 22 split: this file owns the FINAL-COMPILE phase of LayerBuilder:
//
//   text_run(name, params)
//       Register a pending text-run spec with a TextRunBuilder that
//       mutates the spec in place.  Multiple `.text_run(...)` calls
//       per layer are allowed: each spawns a separate RenderNode +
//       downstream TextRunNode at compose time.
//
//   build()
//       Finalise the layer:
//         1. Resolve `until_frame`-driven duration into `m_layer.duration`.
//         2. Apply depth_role Z-offset (if not None).
//         3. Bind the layer-level FontEngine pointer.
//         4. Bake time-dependent transform (position / rotation / scale /
//            anchor / opacity) from `anim_transform` at the layer's
//            current local time.
//         5. Bake time-dependent blur into either the existing Blur
//            effect slot (if any) or a freshly-pushed Blur effect.
//         6. Materialise pending text-run specs → RenderNodes flagged
//            with ShapeType::TextRun, then mark each spec consumed via
//            the canonical `text_internal::mark_consumed` counter
//            (TICKET-104 contract).
//
// Stage-creation setters stay in layer_builder.cpp (and partially in
// commands/*.cpp under src/scene/builders/).  This file is the atomic
// composition surface only.
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
#include <chronon3d/text/text_definition.hpp>  // F3.D — to_text_run_spec() for TextDefinition overload (F2.D lossless reverse adapter)
#include <chronon3d/text/text_placement_resolver.hpp>  // F2 — resolve_placement_origin for build-time semantic placement
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

TextRunBuilder& LayerBuilder::text_run(std::string name, TextRunSpec params) {
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
    // `.text_run(...)` calls.
    m_text_run_builders.push_back(
        std::make_unique<TextRunBuilder>(this, spec_ptr));
    return *m_text_run_builders.back();
}

TextRunBuilder& LayerBuilder::text_run(std::string name, const TextDefinition& def) {
    // F3.D — forward-point wiring: route via to_text_run_spec (the F2.D
    // lossless reverse adapter) instead of the F2.C path that read
    // `text_run.text = from_text_definition(def)` (lossy — drops the 6
    // spec-only animation fields).
    //
    // This makes the 17 helper-site call sites end-to-end lossless:
    // centered_text(), glow_text(), typewriter_text(), and any direct
    // TextDefinition constructed by the user now carry the 6 spec-only
    // animation fields (animators, selectors, direction, language, script,
    // cache_layout) all the way through to TextRunSpec and downstream into
    // materialize_text_run_shape() / evaluate_animator_stack().
    //
    // Frame envelope (TextAnimation.start_delay + .duration) IS lossily
    // dropped by to_text_run_spec per the F2.D LIFECYCLE contract — these
    // live on Layer (Frame envelope of the run), not on TextRunSpec.
    //
    // Symmetric with text(name, TextDefinition) (F3.D — same overload family
    // in shape_commands.cpp).
    return text_run(std::move(name), to_text_run_spec(def));
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
    // For each PendingTextRun pushed via `LayerBuilder::text_run(name,
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

            // F2 — resolve semantic placement at build time using
            // canvas dimensions from the layer context.  Constructs a
            // temporary TextPlacement combining the stored kind with
            // the flat offset, then resolves to a concrete pin point
            // via resolve_placement_origin().
            {
                TextPlacement tp = spec.params.text.placement;
                tp.offset = spec.params.text.offset;  // flat offset → resolver input
                CanvasInfo canvas{m_screen_width, m_screen_height};
                Vec2 pin = resolve_placement_origin(
                    canvas, spec.params.text.layout.box, tp);
                node.world_transform.position = Vec3{pin.x, pin.y, 0.0f};
            }
            node.world_transform.anchor = resolve_text_anchor(
                spec.params.text.layout.anchor,
                spec.params.text.layout.box);
            node.world_transform.scale = Vec3{1.0f, 1.0f, 1.0f};
            node.world_transform.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
            node.color = spec.params.text.appearance.color;
            node.fill = Fill::solid_color(spec.params.text.appearance.color);

#ifdef CHRONON3D_USE_BLEND2D
            // Per-spec FontEngine override (set via trb.font_engine(...))
            // wins over the layer's default font_engine when present.
            FontEngine* engine_for_shape = spec.font_engine ? spec.font_engine : m_font_engine;

            // ── PR 9 — forward the AnimatedTextDocument binding ────
            // When the scene author attached an AnimatedTextDocument
            // via `.from_animated_document(doc)`, the materializer
            // samples it at the layer's local integral frame and
            // routes the resulting ActiveTextState through
            // `apply_active_state_to_text_run_shape`, so transitions
            // (Hold / Cut / CrossfadeLayouts / Scramble / Morph)
            // drive layout swaps automatically.  nullptr → unchanged
            // behaviour (initial spec.text.content.value stays as the
            // static literal).
            auto shape = materialize_text_run_shape(
                spec.params, engine_for_shape, local_time,
                spec.animated_doc);
            if (shape) {
                node.shape.text_run_shape_handle().value = std::move(shape);
            }
#endif
            // On failure (or BLEND2D OFF), text_run_shape_handle().value
            // remains null and we rely on the graph-builder source-pass
            // to emit its existing one-shot `spdlog::error` for null-
            // shape fallthrough.  We do NOT silently drop the
            // placeholder node, so the user sees the failure at compose
            // time, not just in build-time logs.
            //
            // TICKET-104 — mark the spec as consumed via the canonical
            // process-wide counter (`text_internal::mark_consumed`).
            // The previous direct assignment (`spec.consumed = true;`)
            // was a silent no-op: the field flipped but no observer
            // ever noticed.  The new helper increments
            // `g_consumed_decrements` so tests can assert that the
            // decrement is REAL.  See src/text/pending_text_run_impl.hpp
            // for the counter contract.
            (void)chronon3d::text_internal::mark_consumed(spec);
        }
    }

    return std::move(m_layer);
}

} // namespace chronon3d
