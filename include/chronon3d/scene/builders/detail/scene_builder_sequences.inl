// ── include/chronon3d/scene/builders/detail/scene_builder_sequences.inl
//
// Phase-3.3 mechanical split.  Out-of-line template definition of
// SceneBuilder::sequence<F>(name, spec, fn).
//
// Sequence V2: updated to support SequenceBuilder facade via if constexpr.
// When Fn is invocable with SequenceBuilder&, the lambda receives a
// SequenceBuilder with local_frame/progress/duration context.
// When Fn is invocable with SceneBuilder& (legacy), the original behavior
// is preserved verbatim.
//
// Implicitly inline (template definition); no `inline` keyword
// needed at the definition site.

#pragma once

#include <utility>   // std::forward

namespace chronon3d {

    template <typename Fn>
    SceneBuilder &SceneBuilder::sequence(const std::string & /*name*/, SequenceSpec spec, Fn &&fn) {
        const Frame cf = current_integer_frame();
        bool active = cf >= spec.from && cf < spec.from + spec.duration;
        if (!active) {
            return *this;
        }

        // Sequence V2: apply trim_before offset
        const Frame local = cf - spec.from + spec.trim_before;

        FrameContext local_ctx = m_ctx;
        local_ctx.frame = local;
        local_ctx.local_frame = local;
        local_ctx.duration = spec.duration;
        // Preserve sub-frame fraction from the parent time.
        local_ctx.frame_time = m_ctx.frame_time;

        f32 progress = (spec.duration > Frame{0})
            ? std::clamp(
                static_cast<f32>(local) / static_cast<f32>(spec.duration),
                0.0f, 1.0f)
            : 0.0f;

        if constexpr (std::is_invocable_v<Fn, SequenceBuilder&>) {
            // Sequence V2 path: pass SequenceBuilder with context
            SceneBuilder sub_builder(local_ctx, m_shape_registry);
            SequenceBuilder seq(sub_builder, local_ctx, local, spec.duration, progress);
            std::forward<Fn>(fn)(seq);

            Scene sub_scene = sub_builder.build();
            for (auto &layer : sub_scene.layers()) {
                if (layer.duration >= 0) {
                    layer.from += spec.from;
                } else {
                    layer.from = spec.from;
                    layer.duration = spec.duration;
                }
                scene_.add_layer(std::move(layer));
            }
            for (auto &node : sub_scene.nodes()) {
                scene_.add_node(std::move(node));
            }
        } else {
            // Legacy path: pass SceneBuilder (backward compatible)
            SceneBuilder sub_builder(local_ctx, m_shape_registry);
            std::forward<Fn>(fn)(sub_builder);

            Scene sub_scene = sub_builder.build();
            for (auto &layer : sub_scene.layers()) {
                if (layer.duration >= 0) {
                    layer.from += spec.from;
                } else {
                    layer.from = spec.from;
                    layer.duration = spec.duration;
                }
                scene_.add_layer(std::move(layer));
            }
            for (auto &node : sub_scene.nodes()) {
                scene_.add_node(std::move(node));
            }
        }

        return *this;
    }

} // namespace chronon3d
