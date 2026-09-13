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
    const auto frame_number = frame.integral();
    if (frame_number <= keys[begin].frame) return keys[begin].value;
    if (frame_number >= keys[end - 1].frame) return keys[end - 1].value;
    const auto right_it = std::upper_bound(
        keys.begin() + static_cast<std::ptrdiff_t>(begin),
        keys.begin() + static_cast<std::ptrdiff_t>(end), frame_number,
        [](std::int64_t value, const CurveKeyframeProgram& key) {
            return value < key.frame;
        });
    const auto right_index = static_cast<std::size_t>(right_it - keys.begin());
    const auto& left = keys[right_index - 1];
    const auto& right = keys[right_index];
    const auto span = static_cast<float>(right.frame - left.frame);
    const auto t = span == 0.0f
        ? 0.0f
        : static_cast<float>(frame_number - left.frame) / span;
    return left.value + (right.value - left.value) * t;
}

}  // namespace

void TypedProgramSampler::sample_into(const CompiledProgram& program,
                                      Frame frame,
                                      FrameParameterBlock& block) const {
    block.frame = frame;
    block.layers.clear();
    if (block.layers.capacity() < program.layers().size()) {
        block.layers.reserve(program.layers().size());
    }

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
                if (layer.text_program_index < program.texts().size()) {
                    sampled.text_index = program.texts()[layer.text_program_index].text_index;
                }
                sampled.payload = SampledTextParameters{sampled.text_index};
                break;
            }
            case NodeKind::Unknown:
                break;
        }

        Vec3 position{layer.position[0], layer.position[1], layer.position[2]};
        Vec3 scale{layer.scale[0], layer.scale[1], layer.scale[2]};
        sampled.opacity = layer.opacity;

        if (layer.animation_program_index < program.animations().size()) {
            const auto& animation = program.animations()[layer.animation_program_index];
            const auto first = animation.first_track;
            const auto last = std::min<std::size_t>(
                first + animation.track_count, program.animation_tracks().size());
            for (std::size_t i = first; i < last; ++i) {
                const auto& track = program.animation_tracks()[i];
                const auto value = sample_track(program, track, frame, 0.0f);
                switch (track.property_kind) {
                    case AnimationProperty::Opacity: sampled.opacity = value; break;
                    case AnimationProperty::PositionX: position.x = value; break;
                    case AnimationProperty::PositionY: position.y = value; break;
                    case AnimationProperty::PositionZ: position.z = value; break;
                    case AnimationProperty::ScaleX: scale.x = value; break;
                    case AnimationProperty::ScaleY: scale.y = value; break;
                    case AnimationProperty::ScaleZ: scale.z = value; break;
                    case AnimationProperty::Unknown: break;
                }
            }
        }

        Transform transform;
        transform.position = position;
        transform.scale = scale;
        transform.rotation = Quat(glm::radians(layer.rotation[2]), Vec3{0.0f, 0.0f, 1.0f});
        transform.opacity = sampled.opacity;
        sampled.matrix = transform.to_matrix();
        block.layers.push_back(std::move(sampled));
    }
}

FrameParameterBlock TypedProgramSampler::sample(const CompiledProgram& program,
                                                Frame frame) const {
    FrameParameterBlock block;
    sample_into(program, frame, block);
    return block;
}

}  // namespace chronon3d::compiler
