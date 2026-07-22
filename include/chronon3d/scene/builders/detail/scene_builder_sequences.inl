// ── include/chronon3d/scene/builders/detail/scene_builder_sequences.inl
//
// C2 — unified sequence compilation.
//
// compile_sequence() is the single canonical implementation shared by
// both SceneBuilder::sequence() and SequenceBuilder::sequence().
// It eliminates the pre-C2 code duplication and fixes the nested-manifest
// divergence (both call sites now pass the correct target_scene +
// shape_registry through the same code path).
//
// Phase-3.3 mechanical split.  Out-of-line template definitions.
// Templates are implicitly inline.

#pragma once

#include <utility>   // std::forward

namespace chronon3d {

    // ═════════════════════════════════════════════════════════════════════
    // compile_sequence<Fn> — single canonical sequence compilation
    // ═════════════════════════════════════════════════════════════════════
    //
    // Parameters:
    //   cf           Current frame (source depends on caller).
    //                SceneBuilder passes current_integer_frame();
    //                SequenceBuilder passes m_local_frame.
    //   parent_ctx   Parent FrameContext (m_ctx).
    //   spec         Sequence spec (from, duration, trim_before).
    //   fn           Lambda — either (SceneBuilder&) or (SequenceBuilder&).
    //   target_scene Where to merge the manifest and active layers/nodes.
    //   shape_reg    ShapeRegistry pointer (may be nullptr).
    //
    // Contract (A1 — unified):
    //   • ALWAYS execute the lambda → build sub-scene → merge asset
    //     manifest into target_scene, even when the sequence is inactive
    //     (needed by AssetPreflightResolver FullComposition mode).
    //   • ONLY add spatial layers/nodes to target_scene if the sequence
    //     is active at cf.

    template <typename Fn>
    void SceneBuilder::compile_sequence(
        Frame cf,
        const FrameContext& parent_ctx,
        SequenceSpec spec,
        Fn&& fn,
        Scene& target_scene,
        registry::ShapeRegistry* shape_reg)
    {
        // Active range includes premount/postmount and trim_after extension.
        const Frame active_start = spec.from - spec.premount;
        const Frame active_end = spec.from + spec.duration + spec.postmount + spec.trim_after;
        bool active = cf >= active_start && cf < active_end;

        // Compute local frame relative to sequence start.
        Frame local = cf - spec.from;

        // Clamp to [0, duration - 1] for premount/postmount.
        if (local < Frame{0}) {
            local = Frame{0};
        } else if (spec.duration > Frame{0} && local >= spec.duration) {
            local = Frame{spec.duration.integral() - 1};
        }

        // Apply looping over loop_duration if specified.
        if (spec.loop_duration.has_value() && *spec.loop_duration > Frame{0}) {
            local = Frame{local.integral() % spec.loop_duration->integral()};
        }

        // Apply freeze_at if specified (wins over loop and clamp).
        if (spec.freeze_at.has_value()) {
            local = *spec.freeze_at;
        }

        // Apply trim_before offset for internal authoring shift.
        local = local + spec.trim_before;

        FrameContext local_ctx = parent_ctx;
        local_ctx.sample_time = SampleTime::from_frame(
            static_cast<double>(local) + parent_ctx.sample_time.fraction(),
            parent_ctx.frame_rate);
        local_ctx.frame = local;
        local_ctx.duration = spec.duration;

        f32 progress = (spec.duration > Frame{0})
            ? std::clamp(
                static_cast<f32>(local) / static_cast<f32>(spec.duration),
                0.0f, 1.0f)
            : 0.0f;

        // ALWAYS build the sub-scene — asset manifest collection
        // must happen regardless of active status.
        SceneBuilder sub_builder(local_ctx, shape_reg);

        if constexpr (std::is_invocable_v<Fn, SequenceBuilder&>) {
            SequenceBuilder seq(sub_builder, local_ctx, local, spec.duration, progress);
            std::forward<Fn>(fn)(seq);
        } else {
            std::forward<Fn>(fn)(sub_builder);
        }

        Scene sub_scene = sub_builder.build();

        // ALWAYS merge child assets into the target scene manifest.
        // target_scene is either scene_ (SceneBuilder) or
        // m_builder.scene_ (SequenceBuilder → same scene via friend access).
        target_scene.asset_manifest().merge(sub_scene.asset_manifest());

        // ONLY add spatial layers/nodes if the sequence is active.
        if (active) {
            for (auto& layer : sub_scene.layers()) {
                if (layer.duration >= 0) {
                    layer.from += spec.from;
                } else {
                    layer.from = spec.from;
                    layer.duration = spec.duration;
                }
                target_scene.add_layer(std::move(layer));
            }
            for (auto& node : sub_scene.nodes()) {
                target_scene.add_node(std::move(node));
            }
        }
    }

    // ═════════════════════════════════════════════════════════════════════
    // SceneBuilder::sequence — delegates to compile_sequence
    // ═════════════════════════════════════════════════════════════════════

    template <typename Fn>
    SceneBuilder& SceneBuilder::sequence(const std::string& /*name*/,
                                          SequenceSpec spec, Fn&& fn) {
        compile_sequence(current_integer_frame(), m_ctx, spec,
                         std::forward<Fn>(fn), scene_, m_shape_registry);
        return *this;
    }

} // namespace chronon3d
