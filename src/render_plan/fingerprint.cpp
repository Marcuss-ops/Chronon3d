#include "fingerprint_detail.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory_resource>

namespace chronon3d::render_plan {
using namespace fingerprint_detail;

std::uint64_t compute_render_plan_content_fingerprint(const RenderPlan& plan) {
    std::array<std::byte, 64U * 1024U> scratch_storage{};
    std::pmr::monotonic_buffer_resource scratch{
        scratch_storage.data(), scratch_storage.size()};
    CanonicalPlanWriter out{&scratch};

    out.add_string("chronon3d.render-plan.content-fingerprint.v38");
    out.add_string(plan.schema);
    add_optional(out, plan.color_management, [](auto& writer, const auto& cm) {
        writer.add_string(cm.ocio_config);
        writer.add_string(cm.working_space);
        writer.add_string(cm.video_input_space);
        writer.add_string(cm.output_display);
        writer.add_string(cm.output_view);
    });
    out.add_i64(plan.canvas.width);
    out.add_i64(plan.canvas.height);
    out.add_i64(plan.canvas.fps.num());
    out.add_i64(plan.canvas.fps.den());
    out.add_frame(plan.canvas.duration);
    if (plan.temporal_motion_blur) {
        // Preserve the historical byte stream for plans without this v3-only
        // extension; present values still contribute fully to identity.
        out.add_bool(true);
        out.add_float(plan.temporal_motion_blur->shutter_angle);
        out.add_uint64(plan.temporal_motion_blur->samples);
    }
    add_optional(out, plan.camera,
                 [](auto& writer, const CameraPlan& camera) { add_camera(writer, camera); });
    add_optional(out, plan.camera_samples,
                 [](auto& writer, const std::vector<CameraSamplePlan>& samples) {
                     writer.add_size(samples.size());
                     for (const auto& sample : samples) {
                         writer.add_double(sample.frame);
                         add_camera(writer, sample.camera);
                     }
                 });
    add_optional(out, plan.lighting_rig, [](auto& writer, const LightingRigPlan& rig) {
        for (const float value : rig.key_direction) writer.add_float(value);
        for (const float value : rig.key_color) writer.add_float(value);
        writer.add_float(rig.key_intensity);
        for (const float value : rig.ambient_color) writer.add_float(value);
        writer.add_float(rig.ambient_intensity);
        writer.add_bool(rig.rim_enabled);
        for (const float value : rig.rim_color) writer.add_float(value);
        writer.add_float(rig.rim_intensity);
        writer.add_float(rig.rim_power);
    });
    add_optional(out, plan.audio, [](auto& writer, const AudioMixPlan& mix) {
        add_audio_mix(writer, mix);
    });
    out.add_uint64(plan.budget.max_temporal_pixels);
    out.add_size(plan.layers.size());
    for (const auto& layer : plan.layers) add_layer(out, layer);

    return out.finish();
}


} // namespace chronon3d::render_plan
