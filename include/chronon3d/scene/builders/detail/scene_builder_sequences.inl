// ── include/chronon3d/scene/builders/detail/scene_builder_sequences.inl
//
// Phase-3.3 mechanical split.  Out-of-line template definition of
// SceneBuilder::sequence<F>(name, spec, fn).  Behaviour preserved
// verbatim — `cf >= spec.from && cf < spec.from + spec.duration`
// active-only gate, FrameContext propagation with sub-frame fraction
// preservation, sub_builder.dispatch with the `(const FrameContext&)`
// SceneBuilder constructor, scene_.add_layer/add_node merge.
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

        FrameContext local_ctx = m_ctx;
        local_ctx.frame = Frame{cf - spec.from};
        local_ctx.local_frame = local_ctx.frame;
        local_ctx.duration = spec.duration;
        // Preserve sub-frame fraction from the parent time.
        local_ctx.frame_time = m_ctx.frame_time;

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

        return *this;
    }

} // namespace chronon3d
