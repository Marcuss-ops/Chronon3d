#include <chronon3d/render_plan/render_plan.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>

#include <spdlog/spdlog.h>

namespace chronon3d::render_plan {

std::optional<PlanDecodeError> validate_render_budget(
    const RenderPlan& plan, const RenderBudget& budget) {
    const auto fail = [](std::string path, std::string message) {
        return std::optional<PlanDecodeError>{
            PlanDecodeError{std::move(path), std::move(message)}};
    };
    const auto fail_if_non_finite = [&](double value, const char* path) {
        return !std::isfinite(value) ? fail(path, "value must be finite")
                                     : std::optional<PlanDecodeError>{};
    };
    if (plan.canvas.width <= 0 ||
        static_cast<std::uint64_t>(plan.canvas.width) > budget.max_width)
        return fail("canvas.width", "render budget resolution width exceeded or is non-positive");
    if (plan.canvas.height <= 0 ||
        static_cast<std::uint64_t>(plan.canvas.height) > budget.max_height)
        return fail("canvas.height", "render budget resolution height exceeded or is non-positive");
    if (plan.canvas.fps.num() <= 0 || plan.canvas.fps.den() <= 0)
        return fail("canvas.fps", "frame rate must be positive");
    if (plan.output.bitrate < 0)
        return fail("output.bitrate", "bitrate cannot be negative");
    if (plan.output.crf < 0 || plan.output.crf > 51)
        return fail("output.crf", "CRF must be between 0 and 51");
    if (plan.output.qp < -1 || plan.output.qp > 63)
        return fail("output.qp", "QP must be between 0 and 63");
    if (plan.output.rate_control == RateControlMode::Crf &&
        (plan.output.qp >= 0 || plan.output.bitrate > 0))
        return fail("output.rate_control", "CRF mode cannot combine QP or bitrate");
    if (plan.output.rate_control == RateControlMode::ConstantQp &&
        (plan.output.crf > 0 || plan.output.bitrate > 0 || plan.output.qp < 0))
        return fail("output.rate_control", "QP mode requires only a valid QP value");
    if (plan.output.rate_control == RateControlMode::Bitrate &&
        (plan.output.crf > 0 || plan.output.qp >= 0 || plan.output.bitrate <= 0))
        return fail("output.rate_control", "bitrate mode requires only a positive bitrate");
    if (plan.layers.size() > budget.max_layers)
        return fail("layers", "render budget max_layers exceeded");

    const auto duration_value = plan.canvas.duration.integral();
    if (duration_value <= 0)
        return fail("canvas.duration_frames", "duration must be positive");
    const auto frames = static_cast<std::uint64_t>(duration_value);
    if (frames > budget.max_frames)
        return fail("canvas.duration_frames", "render budget max_frames exceeded");

    const auto width = static_cast<std::uint64_t>(plan.canvas.width);
    const auto height = static_cast<std::uint64_t>(plan.canvas.height);
    if (width > budget.max_total_pixels / height)
        return fail("canvas", "render budget max_total_pixels exceeded");
    const auto total_pixels = width * height;

    std::uint64_t text_bytes = 0;
    std::uint64_t asset_reference_bytes = 0;
    std::uint64_t inline_mesh_bytes = 0;
    const auto add_bytes = [](std::uint64_t& total, std::size_t value,
                              std::uint64_t limit) {
        if (total > limit || static_cast<std::uint64_t>(value) > limit - total)
            return false;
        total += static_cast<std::uint64_t>(value);
        return true;
    };

    for (std::size_t index = 0; index < plan.layers.size(); ++index) {
        const auto& layer = plan.layers[index];
        const auto prefix = "layers[" + std::to_string(index) + "]";
        if (layer.type == LayerType::Adjustment) {
            const bool has_non_default_size = layer.size != std::array<float, 2>{0, 0};
            const bool has_non_default_color = layer.color != std::array<float, 4>{1, 1, 1, 1};
            const bool has_non_default_position = layer.position != std::array<float, 3>{0, 0, 0};
            const bool has_non_default_scale = layer.scale != std::array<float, 3>{1, 1, 1};
            const bool has_non_default_rotation = layer.rotation != std::array<float, 3>{0, 0, 0};
            if (layer.effects.empty() || !layer.asset.empty() || !layer.source.empty() ||
                !layer.text.empty() || !layer.text_spans.empty() || !layer.font.empty() ||
                layer.style || layer.motion_blur || layer.shape || layer.inline_mesh ||
                (layer.inline_mesh && layer.inline_mesh->material) ||
                plan.lighting_rig ||
                layer.particle_emitter || layer.light || layer.path_mask || !layer.masks.empty() || has_non_default_size ||
                has_non_default_color || has_non_default_position || has_non_default_scale ||
                has_non_default_rotation || layer.size_dimensions != 0 ||
                layer.position_dimensions != 0 || layer.scale_dimensions != 0 ||
                layer.rotation_dimensions != 0 ||
                layer.enable_3d || layer.screen_space || layer.safe_insets || layer.depth_role ||
                layer.depth_offset != 0.0f || layer.radius || layer.start_frame ||
                layer.duration_frames || layer.fit || layer.crop || layer.animation ||
                layer.fill_gradient_animation || !layer.text_animators.empty() ||
                layer.blend_mode || layer.opacity || layer.loop || layer.parent ||
                layer.track_matte || layer.time_remap || layer.transition_in || layer.transition_out) {
                return fail(prefix,
                    "adjustment layers require a non-empty effect stack and cannot carry drawable, transform, timing, compositing, or relationship properties");
            }
        }
        if (layer.start_frame && layer.start_frame->integral() < 0)
            return fail(prefix + ".start_frame", "layer start frame cannot be negative");
        if (layer.duration_frames && layer.duration_frames->integral() <= 0)
            return fail(prefix + ".duration_frames", "layer duration must be positive");
        if (layer.start_frame && layer.start_frame->integral() >= duration_value)
            return fail(prefix + ".start_frame", "layer starts outside the composition duration");
        if (layer.duration_frames &&
            layer.duration_frames->integral() >
                duration_value - (layer.start_frame ? layer.start_frame->integral() : 0))
            return fail(prefix + ".duration_frames", "layer duration exceeds composition duration");

        if (!add_bytes(text_bytes, layer.text.size(), budget.max_text_bytes))
            return fail("layers[].text", "render budget max_text_bytes exceeded");
        for (const auto* reference : {&layer.asset, &layer.source, &layer.font}) {
            if (!add_bytes(asset_reference_bytes, reference->size(),
                           budget.max_asset_reference_bytes))
                return fail("layers[]", "render budget max_asset_reference_bytes exceeded");
        }

        if (layer.inline_mesh) {
            const auto& mesh = *layer.inline_mesh;
            if (layer.type != LayerType::Mesh)
                return fail(prefix + ".inline_mesh", "inline_mesh is valid only for mesh layers");
            if (mesh.material) {
                const auto& material = *mesh.material;
                if (!std::isfinite(material.diffuse) || material.diffuse < 0.0f || material.diffuse > 1.0f ||
                    !std::isfinite(material.specular) || material.specular < 0.0f || material.specular > 1.0f ||
                    !std::isfinite(material.shininess) || material.shininess < 1.0f || material.shininess > 1024.0f ||
                    !std::isfinite(material.rim_intensity) || material.rim_intensity < 0.0f || material.rim_intensity > 16.0f)
                    return fail(prefix + ".inline_mesh.material", "material values are outside their finite supported ranges");
                if (material.base_color && !std::all_of(material.base_color->begin(), material.base_color->end(),
                        [](float value) { return std::isfinite(value) && value >= 0.0f && value <= 1.0f; }))
                    return fail(prefix + ".inline_mesh.material.base_color", "components must be finite and in [0, 1]");
            }
            if (mesh.vertices.size() < 3 || mesh.vertices.size() > kInlineMeshMaxVertices)
                return fail(prefix + ".inline_mesh.vertices", "vertex count must be in [3, 250000]");
            if (mesh.indices.size() < 3 || mesh.indices.size() > kInlineMeshMaxIndices ||
                mesh.indices.size() % 3 != 0)
                return fail(prefix + ".inline_mesh.indices", "triangle index count must be divisible by three and in [3, 750000]");
            if ((layer.type != LayerType::Mesh && layer.inline_mesh) ||
                (layer.type == LayerType::Mesh &&
                 (layer.shape || layer.particle_emitter || layer.light || !layer.text.empty() ||
                  layer.style || layer.path_mask || !layer.masks.empty() ||
                  (layer.animation && !layer.animation->tracks.empty()))))
                return fail(prefix + ".inline_mesh", "inline meshes are mesh-only payloads; mesh layers cannot carry another content payload or transform animation");
            if (mesh.animation.size() > kInlineMeshMaxAnimationSamples)
                return fail(prefix + ".inline_mesh.animation", "animation sample count exceeds 4096");
            std::uint64_t animation_bytes = 0;
            std::int64_t previous_sample_frame = -1;
            for (const auto& sample : mesh.animation) {
                if (sample.frame < 0 || sample.frame >= duration_value || sample.frame <= previous_sample_frame)
                    return fail(prefix + ".inline_mesh.animation", "sample frames must be strictly increasing and inside the composition duration");
                if (sample.positions.size() != mesh.vertices.size())
                    return fail(prefix + ".inline_mesh.animation.positions", "sample vertex count must match the base mesh");
                previous_sample_frame = sample.frame;
                constexpr std::uint64_t bytesPerPosition = sizeof(std::array<float, 3>);
                if (sample.positions.size() > (kInlineMeshMaxAggregateBytes / bytesPerPosition) ||
                    animation_bytes > kInlineMeshMaxAggregateBytes - sample.positions.size() * bytesPerPosition)
                    return fail(prefix + ".inline_mesh.animation", "baked animation exceeds the 64 MiB byte limit");
                animation_bytes += sample.positions.size() * bytesPerPosition;
                for (const auto& position : sample.positions)
                    for (const float component : position)
                        if (!std::isfinite(component) || std::abs(component) > 1'000'000.0f)
                            return fail(prefix + ".inline_mesh.animation.positions", "sample positions must be finite and within [-1000000, 1000000]");
            }
            constexpr std::uint64_t kBytesPerVertex = sizeof(InlineMeshVertex);
            constexpr std::uint64_t kBytesPerIndex = sizeof(std::uint32_t);
            const auto vertex_count = static_cast<std::uint64_t>(mesh.vertices.size());
            const auto index_count = static_cast<std::uint64_t>(mesh.indices.size());
            if (vertex_count > kInlineMeshMaxAggregateBytes / kBytesPerVertex ||
                index_count > (kInlineMeshMaxAggregateBytes - vertex_count * kBytesPerVertex) / kBytesPerIndex)
                return fail(prefix + ".inline_mesh", "inline mesh payload exceeds the 64 MiB aggregate byte limit");
            const std::uint64_t mesh_bytes = vertex_count * kBytesPerVertex + index_count * kBytesPerIndex;
            if (mesh_bytes > kInlineMeshMaxAggregateBytes || animation_bytes > kInlineMeshMaxAggregateBytes - mesh_bytes)
                return fail(prefix + ".inline_mesh", "inline mesh plus baked animation exceeds the 64 MiB payload limit");
            const auto complete_mesh_bytes = mesh_bytes + animation_bytes;
            if (inline_mesh_bytes > kInlineMeshMaxAggregateBytes - complete_mesh_bytes)
                return fail("layers[].inline_mesh", "render budget aggregate inline mesh and animation bytes exceed 64 MiB");
            inline_mesh_bytes += complete_mesh_bytes;
            for (std::size_t vertex_index = 0; vertex_index < mesh.vertices.size(); ++vertex_index) {
                const auto& vertex = mesh.vertices[vertex_index];
                for (const float component : vertex.position)
                    if (!std::isfinite(component) || std::abs(component) > 1'000'000.0f)
                        return fail(prefix + ".inline_mesh.vertices[].position", "position components must be finite and within [-1000000, 1000000]");
                for (const float component : vertex.normal)
                    if (!std::isfinite(component) || std::abs(component) > 1.0f)
                        return fail(prefix + ".inline_mesh.vertices[].normal", "normal components must be finite and within [-1, 1]");
                for (const float component : vertex.uv)
                    if (!std::isfinite(component) || std::abs(component) > 1'000'000.0f)
                        return fail(prefix + ".inline_mesh.vertices[].uv", "UV components must be finite and within [-1000000, 1000000]");
            }
            for (const std::uint32_t index_value : mesh.indices)
                if (index_value >= mesh.vertices.size())
                    return fail(prefix + ".inline_mesh.indices[]", "index is outside the vertex array");
        } else if (layer.type == LayerType::Mesh) {
            return fail(prefix + ".inline_mesh", "mesh layers require inline triangle geometry");
        }

        for (const auto value : layer.size)
            if (const auto invalid = fail_if_non_finite(value, "layers[].size")) return invalid;
        for (const auto value : layer.position)
            if (const auto invalid = fail_if_non_finite(value, "layers[].position")) return invalid;
        for (const auto value : layer.scale)
            if (const auto invalid = fail_if_non_finite(value, "layers[].scale")) return invalid;
        for (const auto value : layer.rotation)
            if (const auto invalid = fail_if_non_finite(value, "layers[].rotation")) return invalid;
        if (layer.opacity)
            if (const auto invalid = fail_if_non_finite(*layer.opacity, "layers[].opacity")) return invalid;
        if (layer.style && layer.style->font_size)
            if (const auto invalid = fail_if_non_finite(*layer.style->font_size,
                                                        "layers[].style.font_size")) return invalid;

        if (layer.animation) {
            for (const auto& track : layer.animation->tracks) {
                const auto valid_spring = [](const SpringPlan& spring) {
                    return is_valid_spring_config(spring) && spring.mass <= 10000.0f &&
                        spring.stiffness <= 100000.0f && spring.damping <= 100000.0f &&
                        std::abs(spring.initial_velocity) <= 1000000.0f;
                };
                if (track.spring && !valid_spring(*track.spring))
                    return fail(prefix + ".animation.tracks[].spring", "spring config is outside supported finite bounds");
                if (track.spring && track.easing != "spring")
                    return fail(prefix + ".animation.tracks[].spring", "track spring config requires spring easing");
                if (track.keyframes.empty())
                    return fail(prefix + ".animation.tracks", "tracks require at least one keyframe");
                if (track.easing == "spring" && track.keyframes.size() < 2)
                    return fail(prefix + ".animation.tracks", "spring segment requires positive frame span");
                Frame previous{-1};
                for (std::size_t key_index = 0; key_index < track.keyframes.size(); ++key_index) {
                    const auto& key = track.keyframes[key_index];
                    if (key.spring && !valid_spring(*key.spring))
                        return fail(prefix + ".animation.tracks[].keyframes[].spring", "spring config is outside supported finite bounds");
                    if (key.spring && key.easing != "spring" &&
                        !(track.easing == "spring" && !key.easing))
                        return fail(prefix + ".animation.tracks[].keyframes[].spring", "keyframe spring config requires spring easing");
                    if (key_index + 1 < track.keyframes.size() &&
                        (key.easing == "spring" || (!key.easing && track.easing == "spring")) &&
                        key.frame >= track.keyframes[key_index + 1].frame)
                        return fail(prefix + ".animation.tracks[].keyframes", "spring segment requires positive frame span");
                    if (key_index + 1 < track.keyframes.size() &&
                        (key.easing == "spring" || (!key.easing && track.easing == "spring")) &&
                        !key.spring && !track.spring)
                        return fail(prefix + ".animation.tracks[].keyframes[].spring", "spring segment requires a config on its keyframe or track");
                    if (key.frame.integral() < 0 || key.frame.integral() > duration_value)
                        return fail(prefix + ".animation.tracks[].keyframes[].frame",
                                    "keyframe frame " + std::to_string(key.frame.integral()) +
                                    " outside valid range [0, " + std::to_string(std::max<std::int64_t>(0, duration_value - 1)) +
                                    "] (duration_frames=" + std::to_string(duration_value) + ")");
                    if (key.frame.integral() == duration_value) {
                        spdlog::warn("render_budget: keyframe frame {} on {} matches composition duration {}; accepted as boundary clamped to last frame",
                                     key.frame.integral(), prefix + ".animation.tracks[].keyframes[].frame", duration_value);
                    }
                    if (previous.integral() >= 0 && key.frame < previous)
                        return fail(prefix + ".animation.tracks[].keyframes",
                                    "keyframes must be sorted by frame");
                    previous = key.frame;
                    if (key.value.empty() || key.value.size() > 4)
                        return fail(prefix + ".animation.tracks[].keyframes[].value",
                                    "keyframe value must contain 1 to 4 numeric components");
                    for (const auto component : key.value)
                        if (const auto invalid = fail_if_non_finite(
                                component, "layers[].animation.tracks[].keyframes[].value"))
                            return invalid;
                }
                if (!track.keyframes.empty() &&
                    (track.keyframes.back().easing == "spring" || track.keyframes.back().spring))
                    return fail(prefix + ".animation.tracks[].keyframes", "spring segment requires positive frame span");
            }
        }
    }

    if (total_pixels > budget.max_peak_memory_bytes / 16)
        return fail("canvas", "render budget max_peak_memory_bytes exceeded");
    if (total_pixels > std::numeric_limits<std::uint64_t>::max() / 4 / frames)
        return fail("canvas", "render budget output estimate overflow");
    const auto estimated_output = total_pixels * 4 * frames;
    if (estimated_output > budget.max_estimated_output_bytes)
        return fail("canvas", "render budget max_estimated_output_bytes exceeded");
    return std::nullopt;
}

std::optional<PlanDecodeError> validate_render_plan_budget(
    const RenderPlan& plan, const RenderBudget& budget) {
    return validate_render_budget(plan, budget);
}

}  // namespace chronon3d::render_plan
