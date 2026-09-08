#include <chronon3d/compiler/typed_program_sampler.hpp>

#include <chronon3d/math/transform.hpp>

#include <algorithm>
#include <cmath>
#include <string_view>

namespace chronon3d::compiler {
namespace {

float sample_track(const CompiledProgram& program,
                   const AnimationTrackProgram& track,
                   Frame frame,
                   float fallback) {
    if (track.keyframe_count == 0 ||
        track.first_keyframe >= program.curve_keyframes().size()) {
        return fallback;
    }
    const auto begin = track.first_keyframe;
    const auto end = std::min<std::size_t>(
        begin + track.keyframe_count, program.curve_keyframes().size());
    const auto& keys = program.curve_keyframes();
    if (end <= begin) return fallback;
    if (frame.integral() <= keys[begin].frame) return keys[begin].value;
    if (frame.integral() >= keys[end - 1].frame) return keys[end - 1].value;
    for (std::size_t i = begin + 1; i < end; ++i) {
        if (frame.integral() > keys[i].frame) continue;
        const auto& left = keys[i - 1];
        const auto& right = keys[i];
        const auto span = static_cast<float>(right.frame - left.frame);
        const auto t = span == 0.0f
            ? 0.0f
            : static_cast<float>(frame.integral() - left.frame) / span;
        return left.value + (right.value - left.value) * t;
    }
    return keys[end - 1].value;
}

}  // namespace

FrameParameterBlock TypedProgramSampler::sample(const CompiledProgram& program,
                                                Frame frame) const {
    FrameParameterBlock block;
    block.frame = frame;
    block.layers.reserve(program.layers().size());

    for (const auto& layer : program.layers()) {
        SampledLayerParameters sampled;
        sampled.id = layer.id;
        sampled.kind = layer.kind;
        sampled.text_index = 0;

        const auto start = layer.start_frame;
        const auto end = start + layer.duration_frames;
        sampled.active = frame.integral() >= start && frame.integral() < end;

        switch (layer.kind) {
            case NodeKind::Color:
                sampled.payload = SampledColorParameters{layer.color};
                break;
            case NodeKind::Image:
                sampled.payload = SampledImageParameters{
                    layer.asset, layer.size, layer.radius};
                break;
            case NodeKind::Video:
                sampled.payload = SampledVideoParameters{
                    layer.source.empty() ? layer.asset : layer.source,
                    layer.size, layer.loop};
                break;
            case NodeKind::Text: {
                const auto text_it = std::find_if(
                    program.texts().begin(), program.texts().end(),
                    [&](const TextProgram& text) { return text.id == layer.id; });
                sampled.text_index = text_it == program.texts().end()
                    ? 0 : text_it->text_index;
                sampled.payload = SampledTextParameters{sampled.text_index, 0};
                break;
            }
            case NodeKind::Unknown:
                break;
        }

        Vec3 position{layer.position[0], layer.position[1], layer.position[2]};
        Vec3 scale{layer.scale[0], layer.scale[1], layer.scale[2]};
        sampled.opacity = layer.opacity;

        const auto animation_it = std::find_if(
            program.animations().begin(), program.animations().end(),
            [&](const AnimationProgram& animation) { return animation.id == layer.id; });
        if (animation_it != program.animations().end()) {
            const auto first = animation_it->first_track;
            const auto last = std::min<std::size_t>(
                first + animation_it->track_count, program.animation_tracks().size());
            for (std::size_t i = first; i < last; ++i) {
                const auto& track = program.animation_tracks()[i];
                const auto value = sample_track(program, track, frame, 0.0f);
                const std::string_view property = track.property;
                if (property == "opacity") sampled.opacity = value;
                else if (property == "position.x") position.x = value;
                else if (property == "position.y") position.y = value;
                else if (property == "position.z") position.z = value;
                else if (property == "scale.x") scale.x = value;
                else if (property == "scale.y") scale.y = value;
                else if (property == "scale.z") scale.z = value;
            }
        }

        Transform transform;
        transform.position = position;
        transform.scale = scale;
        transform.rotation = Quat(glm::radians(layer.rotation[2]), Vec3{0.0f, 0.0f, 1.0f});
        transform.opacity = sampled.opacity;
        sampled.matrix = transform.to_matrix();
        block.layers.push_back(sampled);
    }
    return block;
}

}  // namespace chronon3d::compiler
