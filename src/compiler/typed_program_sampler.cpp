#include <chronon3d/compiler/typed_program_sampler.hpp>
#include <chronon3d/compiler/scene_materializer.hpp>

#include <chronon3d/math/transform.hpp>
#include <chronon3d/render_plan/resolved_transform_samples.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace chronon3d::compiler {
namespace {

// ── O(1) animation-track cursor ─────────────────────────────────────────────
// Sequential video export walks frames in order, so the keyframe segment that
// bracketed the previous frame is almost always the one that brackets this
// frame too. A direct-mapped thread-local cache turns the per-track
// `std::upper_bound` into one validated compare pair. The cache only ever
// supplies an INDEX HINT: the bracket is re-validated against the keys passed
// in this call, so a stale/unrelated program address can never change the
// sampled value.
struct TrackSegmentCursor {
    const void* base{nullptr};
    std::size_t count{0};
    std::size_t right{0};
};

constexpr std::size_t kTrackSegmentCursorSlots = 128;

[[nodiscard]] TrackSegmentCursor* track_segment_cursors() noexcept {
    static thread_local TrackSegmentCursor slots[kTrackSegmentCursorSlots];
    return slots;
}

/// Returns the index of the first keyframe with `frame <= value` (value is
/// guaranteed to sit strictly inside `[base[0].frame, base[count-1].frame]`).
template <typename KeyframeT>
[[nodiscard]] std::size_t locate_track_segment(const KeyframeT* base,
                                               std::size_t count,
                                               double value) noexcept {
    auto* slot = track_segment_cursors() +
        ((reinterpret_cast<std::uintptr_t>(base) >> 5) % kTrackSegmentCursorSlots);
    if (slot->base == base && slot->count == count && slot->right < count &&
        slot->right > 0 &&
        static_cast<double>(base[slot->right - 1].frame) <= value &&
        value < static_cast<double>(base[slot->right].frame)) {
        return slot->right;
    }
    const auto* right_it = std::upper_bound(
        base, base + count, value,
        [](double lhs, const KeyframeT& key) {
            return lhs < static_cast<double>(key.frame);
        });
    const std::size_t right = static_cast<std::size_t>(right_it - base);
    slot->base = base;
    slot->count = count;
    slot->right = right;
    return right;
}

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
    if (frame_number <= static_cast<double>(keys[begin].frame)) return keys[begin].value;
    if (frame_number >= static_cast<double>(keys[end - 1].frame)) return keys[end - 1].value;
    const auto right_index = begin + locate_track_segment(
        keys.data() + begin, end - begin, frame_number);
    const auto& left = keys[right_index - 1];
    const auto& right = keys[right_index];
    const auto span = static_cast<float>(right.frame - left.frame);
    const auto t = span == 0.0f
        ? 0.0f
        : static_cast<float>(frame_number - static_cast<double>(left.frame)) / span;
    return left.value + (right.value - left.value) * t;
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
    // Reuse the per-layer slots in place: `clear()` + `push_back` would destroy
    // every `SampledLayerParameters` (freeing the `elements` buffer) and
    // reallocate it from scratch on every frame.
    block.layers.resize(program.layers().size());

    std::size_t layer_slot = 0;
    for (const auto& layer : program.layers()) {
        SampledLayerParameters& sampled = block.layers[layer_slot++];
        // Reset to exactly the value a freshly constructed slot would hold, so
        // nothing from the previous frame's payload/elements survives.
        sampled.id = layer.id;
        sampled.kind = layer.kind;
        sampled.matrix = Mat4{1.0f};
        sampled.opacity = 1.0f;
        sampled.active = false;
        sampled.dirty = true;
        sampled.time_dependent = false;
        sampled.local_time_frame = 0.0;
        sampled.text_index = 0;
        sampled.elements.clear();  // keeps the capacity grown on earlier frames
        sampled.dirty = program.temporal_schedule().dirty(
            layer.id, frame_number);

        const auto start = layer.start_frame;
        const auto end = start + layer.duration_frames;
        sampled.active = frame_number >= static_cast<double>(start) &&
                         frame_number < static_cast<double>(end);

        switch (layer.kind) {
            case NodeKind::Color:
                sampled.payload = SampledColorParameters{layer.color};
                break;
            case NodeKind::Image:
                if (auto* img = std::get_if<SampledImageParameters>(&sampled.payload)) {
                    img->asset = layer.asset;
                    img->size = layer.size;
                    img->radius = layer.radius;
                } else {
                    sampled.payload = SampledImageParameters{
                        layer.asset, layer.size, layer.radius};
                }
                break;
            case NodeKind::Video: {
                const auto& src = layer.source.empty() ? layer.asset : layer.source;
                if (auto* vid = std::get_if<SampledVideoParameters>(&sampled.payload)) {
                    vid->source = src;
                    vid->size = layer.size;
                    vid->loop = layer.loop;
                } else {
                    sampled.payload = SampledVideoParameters{
                        src, layer.size, layer.loop};
                }
                break;
            }
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
                // SVG paint/geometry/operators are baked by the frame-specific
                // materialization; the sampler must not let pixel reuse treat
                // this layer as static.
                if (layer_has_time_varying_path(layer)) {
                    sampled.time_dependent = true;
                    sampled.dirty = true;
                }
                break;
            case NodeKind::Mesh:
            {
                SampledMeshParameters mesh{.inline_mesh = layer.inline_mesh ? &*layer.inline_mesh : nullptr,
                                           .color = layer.color};
                if (mesh.inline_mesh && !mesh.inline_mesh->animation.empty()) {
                    const auto& samples = mesh.inline_mesh->animation;
                    if (frame_number < static_cast<double>(samples.front().frame)) {
                        mesh.animation_before = mesh.animation_after = &samples.front();
                    } else if (frame_number >= static_cast<double>(samples.back().frame)) {
                        mesh.animation_before = mesh.animation_after = &samples.back();
                    } else {
                        const auto right_idx = locate_track_segment(
                            samples.data(), samples.size(), frame_number);
                        mesh.animation_before = &samples[right_idx - 1];
                        mesh.animation_after = &samples[right_idx];
                        const auto span = mesh.animation_after->frame - mesh.animation_before->frame;
                        mesh.animation_mix = span > 0 ? std::clamp(
                            static_cast<float>(frame_number - mesh.animation_before->frame) /
                                static_cast<float>(span), 0.0f, 1.0f) : 0.0f;
                    }
                    sampled.dirty = true;
                    sampled.time_dependent = true;
                }
                sampled.payload = mesh;
                break;
            }
            case NodeKind::Particles: {
                if (!layer.particle_emitter)
                    throw std::invalid_argument(
                        "compiled particle layer has no emitter payload");
                const auto fps_numerator = program.metadata().fps_numerator;
                const auto fps_denominator = program.metadata().fps_denominator;
                if (fps_numerator <= 0 || fps_denominator <= 0)
                    throw std::invalid_argument(
                        "compiled particle layer has an invalid frame rate");
                const double seconds = frame_number *
                    static_cast<double>(fps_denominator) /
                    static_cast<double>(fps_numerator);
                const auto result = sample_particles(*layer.particle_emitter, seconds);
                if (!result.ok())
                    throw std::invalid_argument(
                        "compiled particle sampling failed: " + result.error);
                for (const auto& particle : result.particles) {
                    SampledElementParameters element;
                    element.id = particle.index;
                    element.position = Vec3{
                        particle.position.x, particle.position.y,
                        layer.particle_emitter->environment
                            ? (1.0f - particle.depth) * 100.0f : 0.0f};
                    element.rotation.z = particle.rotation;
                    element.opacity = particle.color.a;
                    element.color = particle.color;
                    element.size = particle.size;
                    element.sprite_scale = particle.sprite_scale;
                    element.velocity = particle.velocity;
                    sampled.elements.push_back(element);
                }
                sampled.time_dependent = true;
                sampled.dirty = true;
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
                // A usable track makes the layer's sampled transform/opacity
                // frame-dependent even though the materialized Scene is not.
                sampled.time_dependent = true;
                // Track values interpolate BETWEEN keyframes, so the compiler
                // schedule (which only lists authored keyframe frames) cannot
                // decide freshness: without this the graph keeps the matrix
                // sampled at the first frame and the layer renders frozen.
                sampled.dirty = true;
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

        if (!layer.transform_samples.empty()) {
            sampled.time_dependent = true;
            const auto resolved = render_plan::sample_resolved_transform(layer.transform_samples, frame_number);
            position = {resolved.position[0], resolved.position[1], resolved.position[2]};
            scale = {resolved.scale[0], resolved.scale[1], resolved.scale[2]};
            rotation = {resolved.rotation_deg[0], resolved.rotation_deg[1], resolved.rotation_deg[2]};
            sampled.opacity = resolved.opacity;
            sampled.dirty = true;
        }
        if (layer.has_transform_animation || !layer.transform_samples.empty()) {
            // A 3D text plane is compiled with a negative X base scale so its
            // glyphs face the camera in readable order. Scale animation
            // replaces the base value, so retain that authored orientation
            // when scalar/vector scale tracks are sampled.
            if (layer.kind == NodeKind::Text && layer.scale[0] < 0.0f)
                scale.x = -std::abs(scale.x);
            Transform transform;
            transform.position = position;
            transform.scale = scale;
            transform.rotation = glm::quat(glm::radians(rotation));
            transform.opacity = sampled.opacity;
            sampled.matrix = transform.to_matrix();
        }
    }
}

FrameParameterBlock TypedProgramSampler::sample(const CompiledProgram& program,
                                                Frame frame) const {
    FrameParameterBlock block;
    sample_into(program, frame, block);
    return block;
}

}  // namespace chronon3d::compiler
