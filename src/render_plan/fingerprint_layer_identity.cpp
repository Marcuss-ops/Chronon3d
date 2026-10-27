#include "fingerprint_detail.hpp"

namespace chronon3d::render_plan::fingerprint_detail {

void add_layer_identity_and_mesh(CanonicalPlanWriter& out, const LayerPlan& layer) {
    out.add_string(layer.id);
    out.add_enum(layer.type);
    // Preserve the canonical byte stream for plans with no Lottie isolation
    // metadata. This importer-only field is absent from legacy/public plans;
    // only emit its tagged domain when expansion actually produced groups.
    if (!layer.lottie_precomp_groups.empty()) {
        out.add_string("lottie-precomp-groups-v1");
        out.add_size(layer.lottie_precomp_groups.size());
        for (const auto& group : layer.lottie_precomp_groups) {
            out.add_string(group.id);
            out.add_float(group.opacity);
            out.add_bool(group.opacity_active);
            out.add_enum(group.blend_mode);
            add_optional(out, group.opacity_animation, [](auto& writer, const auto& track) {
                add_track(writer, track);
            });
        }
    }
    add_optional(out, layer.flow_layout, [](auto& w, const LayerPlan::FlowLayoutPlan& p) {
        w.add_string(p.group_id); w.add_string(p.axis); w.add_string(p.align); w.add_string(p.justify);
        w.add_float(p.gap); w.add_bool(p.wrap); w.add_float(p.cell_size[0]); w.add_float(p.cell_size[1]);
        if (p.intrinsic_size) w.add_string("intrinsic-layout-v1");
        add_optional(w, p.stagger, [](auto& writer, const auto& stagger) { add_stagger(writer, stagger); });
    });
    add_optional(out, layer.grid_layout, [](auto& w, const LayerPlan::GridLayoutPlan& p) {
        w.add_string(p.group_id); w.add_string(p.fit); w.add_uint64(static_cast<std::uint64_t>(p.columns)); w.add_uint64(static_cast<std::uint64_t>(p.rows));
        w.add_float(p.gap[0]); w.add_float(p.gap[1]); w.add_float(p.cell_size[0]); w.add_float(p.cell_size[1]);
        if (p.intrinsic_size) w.add_string("intrinsic-layout-v1");
        add_optional(w, p.stagger, [](auto& writer, const auto& stagger) { add_stagger(writer, stagger); });
    });
    add_optional(out, layer.stagger_group, [](auto& w, const LayerPlan::StaggerPlan& stagger) {
        w.add_string(stagger.group_id);
        add_stagger(w, stagger);
    });
    add_optional(out, layer.follow_path, [](auto& w, const LayerPlan::FollowPathDriverPlan& p) {
        w.add_string(p.path_id);
        w.add_size(p.path_commands.size());
        for (const auto& command : p.path_commands) {
            w.add_enum(command.type);
            for (const auto value : command.point) w.add_float(value);
            for (const auto value : command.control1) w.add_float(value);
            for (const auto value : command.control2) w.add_float(value);
        }
        add_track(w, p.progress);
        w.add_bool(p.auto_orient);
        w.add_float(p.offset_deg);
        w.add_bool(p.clamp_end);
    });
    add_optional(out, layer.text_counter, [](auto& writer, const TextCounterPlan& value) {
        writer.add_string(value.token);
        writer.add_float(value.counter.from);
        writer.add_float(value.counter.to);
        writer.add_frame(value.counter.start_frame);
        writer.add_frame(value.counter.duration_frames);
        writer.add_string(value.counter.easing);
        writer.add_string(value.counter.format);
        writer.add_string(value.counter.currency);
        writer.add_i64(value.counter.decimals);
        writer.add_string(value.counter.prefix);
        writer.add_string(value.counter.suffix);
    });
    add_optional(out, layer.parent, [](auto& writer, const std::string& value) {
        writer.add_string(value);
    });
    add_optional(out, layer.track_matte, [](auto& writer, const TrackMattePlan& value) {
        writer.add_string(value.source);
        writer.add_enum(value.type);
    });
    add_optional(out, layer.time_remap, [](auto& writer, const TimeRemapPlan& value) {
        add_optional(writer, value.speed, [](auto& nested, float speed) {
            nested.add_float(speed);
        });
        writer.add_bool(value.reverse);
        add_optional(writer, value.freeze_frame, [](auto& nested, Frame frame) {
            nested.add_frame(frame);
        });
        add_optional(writer, value.animation, [](auto& nested, const AnimationTrackPlan& track) {
            add_track(nested, track);
        });
    });
    out.add_string(layer.asset);
    out.add_string(layer.input_color_space);
    out.add_string(layer.ocio_input_space);
    out.add_string(layer.source);
    out.add_string(layer.text);
    out.add_bool(layer.inline_mesh.has_value());
    if (layer.inline_mesh) {
        out.add_size(layer.inline_mesh->vertices.size());
        for (const auto& vertex : layer.inline_mesh->vertices) {
            for (const float value : vertex.position) out.add_float(value);
            for (const float value : vertex.normal) out.add_float(value);
            for (const float value : vertex.uv) out.add_float(value);
        }
        out.add_size(layer.inline_mesh->indices.size());
        for (const std::uint32_t index : layer.inline_mesh->indices) out.add_uint64(index);
        add_optional(out, layer.inline_mesh->material, [](auto& writer, const InlineMeshMaterialPlan& material) {
            writer.add_float(material.diffuse);
            writer.add_float(material.specular);
            writer.add_float(material.shininess);
            writer.add_float(material.rim_intensity);
            add_optional(writer, material.base_color, [](auto& nested, const auto& color) {
                for (const float value : color) nested.add_float(value);
            });
        });
        out.add_size(layer.inline_mesh->animation.size());
        for (const auto& sample : layer.inline_mesh->animation) {
            out.add_i64(sample.frame);
            out.add_size(sample.positions.size());
            for (const auto& position : sample.positions)
                for (const float value : position) out.add_float(value);
        }
    }
}

} // namespace chronon3d::render_plan::fingerprint_detail
