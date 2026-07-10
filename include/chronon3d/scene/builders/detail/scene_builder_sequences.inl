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
        bool active = cf >= spec.from && cf < spec.from + spec.duration;

        // Apply trim_before offset.  When inactive, use trim_before
        // as-is (avoid negative local frame).
        const Frame local = active
            ? (cf - spec.from + spec.trim_before)
            : spec.trim_before;

        FrameContext local_ctx = parent_ctx;
        local_ctx.frame = local;
        local_ctx.local_frame = local;
        local_ctx.duration = spec.duration;
        local_ctx.frame_time = parent_ctx.frame_time;

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
