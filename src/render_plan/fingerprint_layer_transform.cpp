#include "fingerprint_detail.hpp"

namespace chronon3d::render_plan::fingerprint_detail {

void add_layer_transform_and_timing(CanonicalPlanWriter& out, const LayerPlan& layer) {
    for (const auto value : layer.size) out.add_float(value);
    out.add_size(layer.size_dimensions);
    for (const auto value : layer.color) out.add_float(value);
    for (const auto value : layer.position) out.add_float(value);
    out.add_size(layer.position_dimensions);
    for (const auto value : layer.scale) out.add_float(value);
    out.add_size(layer.scale_dimensions);
    for (const auto value : layer.rotation) out.add_float(value);
    out.add_size(layer.rotation_dimensions);
    out.add_float(layer.skew_angle_deg);
    out.add_float(layer.skew_axis_deg);
    out.add_bool(layer.enable_3d);
    if (layer.casts_shadows || layer.accepts_shadows) {
        out.add_bool(true);
        add_optional(out, layer.casts_shadows,
                     [](auto& writer, bool value) { writer.add_bool(value); });
        add_optional(out, layer.accepts_shadows,
                     [](auto& writer, bool value) { writer.add_bool(value); });
    }
    out.add_bool(layer.screen_space);
    // Preserve byte-for-byte fingerprints for plans authored before safe
    // insets existed; the presence marker is only emitted for this extension.
    if (layer.safe_insets) {
        out.add_bool(true);
        for (const float inset : *layer.safe_insets) out.add_float(inset);
    }
    add_optional(out, layer.depth_role,
                 [](auto& writer, const std::string& value) { writer.add_string(value); });
    out.add_float(layer.depth_offset);

    add_optional(out, layer.radius,
                 [](auto& writer, float value) { writer.add_float(value); });

    add_optional(out, layer.start_frame,
                 [](auto& writer, Frame value) { writer.add_frame(value); });
    add_optional(out, layer.duration_frames,
                 [](auto& writer, Frame value) { writer.add_frame(value); });
    add_optional(out, layer.transition_in,
                 [](auto& writer, const TransitionPlan& value) { add_transition(writer, value); });
    add_optional(out, layer.transition_out,
                 [](auto& writer, const TransitionPlan& value) { add_transition(writer, value); });
    add_optional(out, layer.fit,
                 [](auto& writer, FitMode value) { writer.add_enum(value); });
    add_optional(out, layer.crop,
                 [](auto& writer, const ImageCropPlan& value) {
        writer.add_bool(value.enabled);
        for (const float v : value.origin) writer.add_float(v);
        for (const float v : value.size) writer.add_float(v);
        writer.add_bool(value.flip_x);
    });

    if (!layer.transform_samples.empty()) {
        out.add_string("resolved-transform-samples-v1");
        out.add_size(layer.transform_samples.size());
        for (const auto& sample : layer.transform_samples) {
            out.add_double(sample.frame);
            for (float value : sample.position) out.add_float(value);
            for (float value : sample.scale) out.add_float(value);
            for (float value : sample.rotation_deg) out.add_float(value);
            out.add_float(sample.opacity);
        }
    }
    add_optional(out, layer.animation,
                 [](auto& writer, const AnimationPlan& animation) {
        writer.add_size(animation.tracks.size());
        for (const auto& track : animation.tracks) add_track(writer, track);
    });
    add_optional(out, layer.fill_gradient_animation,
                 [](auto& writer, const GradientAnimationPlan& animation) {
        add_gradient_animation(writer, animation);
    });
    out.add_size(layer.text_animators.size());
    for (const auto& animator : layer.text_animators)
        add_text_animator(out, animator);

    add_optional(out, layer.blend_mode,
                 [](auto& writer, chronon3d::BlendMode value) {
                     writer.add_enum(value);
                 });
    add_optional(out, layer.opacity,
                 [](auto& writer, float value) { writer.add_float(value); });
    out.add_bool(layer.loop);
    out.add_bool(layer.auto_orient);
}

} // namespace chronon3d::render_plan::fingerprint_detail
