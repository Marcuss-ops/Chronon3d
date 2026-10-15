#include <chronon3d/compiler/typed_program_sampler.hpp>

#include <chronon3d/animation/easing/easing.hpp>
#include <chronon3d/math/transform.hpp>
#include <chronon3d/scene/model/core/transition.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace chronon3d::compiler {
namespace {

float sample_track(const CompiledProgram& program,
                   const AnimationTrackProgram& track,
                   double frame_number,
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
    if (frame_number <= keys[begin].frame) return keys[begin].value;
    if (frame_number >= keys[end - 1].frame) return keys[end - 1].value;
    const auto right_it = std::upper_bound(
        keys.begin() + static_cast<std::ptrdiff_t>(begin),
        keys.begin() + static_cast<std::ptrdiff_t>(end), frame_number,
        [](double value, const CurveKeyframeProgram& key) {
            return value < key.frame;
        });
    const auto right_index = static_cast<std::size_t>(right_it - keys.begin());
    const auto& left = keys[right_index - 1];
    const auto& right = keys[right_index];
    const auto span = static_cast<float>(right.frame - left.frame);
    const auto t = span == 0.0f
        ? 0.0f
        : static_cast<float>(frame_number - static_cast<double>(left.frame)) / span;
    // Authored easing runs here so program sampling matches the legacy
    // render-plan compiler (keyframe_easing/track_easing). "bezier" always
    // carries its curve (the decoder rejects a bare bezier); unknown names
    // fall back to linear via string_to_easing.
    EasingCurve curve{Easing::Linear};
    if (track.has_bezier) {
        curve = EasingCurve::cubic_bezier(track.bezier[0], track.bezier[1],
                                          track.bezier[2], track.bezier[3]);
    } else if (!track.easing.empty() && track.easing != "linear") {
        curve = EasingCurve{string_to_easing(track.easing)};
    }
    return left.value + (right.value - left.value) * curve.apply(t);
}

}  // namespace

void TypedProgramSampler::sample_into(const CompiledProgram& program,
                                      Frame frame,
                                      FrameParameterBlock& block) const {
    sample_into(program, static_cast<double>(frame.integral()), block);
}

void TypedProgramSampler::sample_into(const CompiledProgram& program,
                                      double frame_number,
                                      FrameParameterBlock& block) const {
    if (!std::isfinite(frame_number))
        throw std::invalid_argument("typed program sample frame must be finite");
    if (frame_number < static_cast<double>(std::numeric_limits<std::int64_t>::min()) ||
        frame_number >= static_cast<double>(std::numeric_limits<std::int64_t>::max()))
        throw std::out_of_range("typed program sample frame exceeds the integral frame range");
    block.frame = Frame{static_cast<std::int64_t>(std::floor(frame_number))};
    block.layers.clear();
    if (block.layers.capacity() < program.layers().size()) {
        block.layers.reserve(program.layers().size());
    }

    for (const auto& layer : program.layers()) {
        SampledLayerParameters sampled;
        sampled.id = layer.id;
        sampled.kind = layer.kind;
        sampled.text_index = 0;
        sampled.dirty = layer.has_transform_animation ||
            program.temporal_schedule().dirty(layer.id, frame_number);

        const auto start = layer.start_frame;
        const auto end = start + layer.duration_frames;
        sampled.active = frame_number >= static_cast<double>(start) &&
                         frame_number < static_cast<double>(end);

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
            case NodeKind::Shape:
                sampled.payload = SampledShapeParameters{
                    layer.size, layer.color, layer.opacity};
                break;
            case NodeKind::Mesh: {
                SampledMeshParameters mesh;
                mesh.inline_mesh = layer.inline_mesh ? &*layer.inline_mesh : nullptr;
                mesh.color = layer.color;
                if (mesh.inline_mesh) {
                    const auto& source = *mesh.inline_mesh;
                    const auto& samples = source.animation;
                    if (!samples.empty()) {
                        mesh.positions.reserve(source.vertices.size());
                        const double sample_frame = std::max(0.0, frame_number);
                        const render_plan::InlineMeshFrameSamplePlan* left = nullptr;
                        const render_plan::InlineMeshFrameSamplePlan* right = nullptr;
                        for (const auto& sample : samples) {
                            if (static_cast<double>(sample.frame) <= sample_frame) left = &sample;
                            if (static_cast<double>(sample.frame) >= sample_frame) { right = &sample; break; }
                        }
                        if (!left) left = right;
                        if (!right) right = left;
                        const double amount = !left || left == right ? 0.0 :
                            std::clamp((sample_frame - static_cast<double>(left->frame)) /
                                static_cast<double>(right->frame - left->frame), 0.0, 1.0);
                        for (std::size_t vertex = 0; vertex < source.vertices.size(); ++vertex) {
                            std::array<float, 3> position = source.vertices[vertex].position;
                            if (left && right) {
                                for (std::size_t component = 0; component < position.size(); ++component)
                                    position[component] = static_cast<float>(left->positions[vertex][component] +
                                        (right->positions[vertex][component] - left->positions[vertex][component]) * amount);
                            }
                            mesh.positions.push_back(position);
                        }
                        std::vector<Vec3> accumulated(mesh.positions.size(), Vec3{0.0f});
                        for (std::size_t triangle = 0; triangle < source.indices.size(); triangle += 3) {
                            const auto a = source.indices[triangle];
                            const auto b = source.indices[triangle + 1];
                            const auto c = source.indices[triangle + 2];
                            const auto point = [&](std::uint32_t index) {
                                const auto& p = mesh.positions[index];
                                return Vec3{p[0], p[1], p[2]};
                            };
                            const Vec3 face = glm::cross(point(b) - point(a), point(c) - point(a));
                            accumulated[a] += face; accumulated[b] += face; accumulated[c] += face;
                        }
                        mesh.normals.reserve(accumulated.size());
                        for (const auto& normal : accumulated) {
                            const float length2 = glm::dot(normal, normal);
                            const Vec3 unit = length2 > 1.0e-20f && std::isfinite(length2)
                                ? normal / std::sqrt(length2) : Vec3{0.0f, 0.0f, 1.0f};
                            mesh.normals.push_back({unit.x, unit.y, unit.z});
                        }
                    }
                }
                sampled.payload = std::move(mesh);
                break;
            }
            case NodeKind::Unknown:
                break;
        }

        Vec3 position{layer.position[0], layer.position[1], layer.position[2]};
        Vec3 scale{layer.scale[0], layer.scale[1], layer.scale[2]};
        Vec3 rotation{layer.rotation[0], layer.rotation[1], layer.rotation[2]};
        sampled.matrix = layer.static_matrix;
        sampled.opacity = layer.opacity;

        if (layer.animation_program_index < program.animations().size()) {
            const auto& animation = program.animations()[layer.animation_program_index];
            const auto first = animation.first_track;
            const auto last = std::min<std::size_t>(
                first + animation.track_count, program.animation_tracks().size());
            // Layer animation keyframes are local to the layer window. This
            // is how the render-plan builder evaluates them and is required
            // for overlays that enter after frame zero.
            const double animation_frame = std::max(
                0.0, frame_number - static_cast<double>(start));
            for (std::size_t i = first; i < last; ++i) {
                const auto& track = program.animation_tracks()[i];
                // An empty/invalid track carries no authored value. In
                // particular, don't replace a layer's static opacity with
                // sample_track's generic zero fallback.
                if (track.keyframe_count == 0 ||
                    track.first_keyframe >= program.curve_keyframes().size()) {
                    continue;
                }
                const auto value = sample_track(program, track, animation_frame, 0.0f);
                switch (track.property_kind) {
                    case AnimationProperty::Opacity: sampled.opacity = value; break;
                    case AnimationProperty::PositionX: position.x = value; break;
                    case AnimationProperty::PositionY: position.y = value; break;
                    case AnimationProperty::PositionZ: position.z = value; break;
                    case AnimationProperty::PositionXOffset: position.x += value; break;
                    case AnimationProperty::PositionYOffset: position.y += value; break;
                    case AnimationProperty::PositionZOffset: position.z += value; break;
                    case AnimationProperty::ScaleX: scale.x = value; break;
                    case AnimationProperty::ScaleY: scale.y = value; break;
                    case AnimationProperty::ScaleZ: scale.z = value; break;
                    case AnimationProperty::UniformScale: scale = {value, value, value}; break;
                    case AnimationProperty::RotationX: rotation.x = value; break;
                    case AnimationProperty::RotationY: rotation.y = value; break;
                    case AnimationProperty::RotationZ: rotation.z = value; break;
                    case AnimationProperty::PositionVector:
                    case AnimationProperty::RotationVector:
                    case AnimationProperty::Blur:
                    case AnimationProperty::Tracking:
                    case AnimationProperty::Skew:
                    case AnimationProperty::SkewAxis:
                    case AnimationProperty::FillColor:
                    case AnimationProperty::Anchor:
                    case AnimationProperty::BaselineShift:
                    case AnimationProperty::BackgroundColor:
                    case AnimationProperty::StrokeColor:
                    case AnimationProperty::StrokeWidth:
                    case AnimationProperty::CharacterOffset:
                    case AnimationProperty::Unknown: break;
                }
            }
        }

        if (layer.has_transform_animation) {
            Transform transform;
            transform.position = position;
            transform.scale = scale;
            transform.rotation = glm::quat(glm::radians(rotation));
            transform.opacity = sampled.opacity;
            sampled.matrix = transform.to_matrix();
        }
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
