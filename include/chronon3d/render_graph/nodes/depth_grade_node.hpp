#pragma once

#include <chronon3d/render_graph/nodes/render_graph_node.hpp>
#include <chronon3d/render_graph/core/render_graph_hashing.hpp>
#include <chronon3d/rendering/depth_grade.hpp>
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>
#include <chronon3d/render_graph/render_backend.hpp>

#include <cmath>
#include <span>
#include <string>

namespace chronon3d::graph {

// Applies depth-based colour grading (fog, saturation, contrast) to a layer
// based on its world-space Z position.  This is a per-layer effect inserted
// after lighting and before compositing.
//
// The grading is done per-pixel using the layer's average world Z as input
// to DepthGrade::normalized_depth() and DepthGrade::apply().
class DepthGradeNode final : public RenderGraphNode {
public:
    DepthGradeNode(rendering::DepthGrade grade, float layer_world_z,
                   bool layer_accepts_lights)
        : RenderGraphNode(static_memory_cache("depth_grade"))
        , m_grade(std::move(grade))
        , m_layer_world_z(layer_world_z)
        , m_accepts_lights(layer_accepts_lights) {}

    RenderGraphNodeKind kind() const noexcept override { return RenderGraphNodeKind::Effect; }
    std::string_view name() const noexcept override { return "DepthGrade"; }
    bool has_compiled_recorder() const noexcept override { return true; }


    cache::NodeCacheKey cache_key(const RenderGraphContext& ctx) const override {
        u64 h = hash_string("chronon.depth-grade-node.v2");
        h = hash_combine(h, hash_value(m_grade.enabled));
        h = hash_combine(h, hash_float_v2(m_grade.near_z));
        h = hash_combine(h, hash_float_v2(m_grade.far_z));
        h = hash_combine(h, hash_color_v2(m_grade.fog_color));
        h = hash_combine(h, hash_float_v2(m_grade.fog_opacity));
        h = hash_combine(h, hash_float_v2(m_grade.far_saturation));
        h = hash_combine(h, hash_float_v2(m_grade.far_contrast));
        h = hash_combine(h, hash_float_v2(m_grade.far_blur_px));
        h = hash_combine(h, hash_float_v2(m_layer_world_z));
        h = hash_combine(h, hash_value(m_accepts_lights));
        return cache::NodeCacheKey{
            .scope = "depth_grade",
            // Depth grade parameters and layer depth are immutable for this
            // node.  Its static cache policy must therefore use one stable
            // frame key; otherwise every rendered frame retains a duplicate
            // full-surface entry and defeats semantic cache reuse.
            .frame = Frame{0},
            .width = ctx.frame_input.width,
            .height = ctx.frame_input.height,
            .params_hash = h,
            .params_version = 2
        };
    }

    std::optional<raster::BBox> predicted_bbox(
        const RenderGraphContext& ctx,
        std::span<const std::optional<raster::BBox>> input_bboxes = {}
    ) const override {
        if (input_bboxes.empty() || !input_bboxes[0]) {
            return std::nullopt;
        }
        return input_bboxes[0]; // depth grade doesn't change extent
    }

    NodeExecResult execute(
        RenderGraphContext& ctx,
        std::span<const FramebufferRef> inputs,
        std::span<const std::optional<raster::BBox>>
    ) override {
        if (inputs.empty() || !inputs[0]) {
            return ctx.acquire_owned_fb(ctx.frame_input.width, ctx.frame_input.height);
        }

        if (!m_grade.enabled || !m_accepts_lights) {
            auto passthrough = ctx.acquire_owned_fb(*inputs[0]);
            if (inputs[0]->surface_handle() != runtime::kInvalidRenderSurfaceHandle) {
                passthrough->set_origin(inputs[0]->origin_x(), inputs[0]->origin_y());
            }
            return passthrough;
        }

        const float t = m_grade.normalized_depth(m_layer_world_z);
        if (t <= 0.001f) {
            auto passthrough = ctx.acquire_owned_fb(*inputs[0]);
            if (inputs[0]->surface_handle() != runtime::kInvalidRenderSurfaceHandle) {
                passthrough->set_origin(inputs[0]->origin_x(), inputs[0]->origin_y());
            }
            return passthrough; // near: no grading
        }

        const auto source_handle = inputs[0]->surface_handle();
        const bool native_input = ctx.policy.require_native_gpu && ctx.backend &&
            ctx.surface_registry &&
            ctx.backend_stats_api()->supports_native_surfaces() &&
            source_handle != runtime::kInvalidRenderSurfaceHandle &&
            ctx.backend_surface_api()->is_native_surface_valid(source_handle);
        if (native_input) {
            // DepthGrade is affine in RGB for a constant layer depth. Lower
            // fog, desaturation, and contrast into the existing Vulkan color
            // matrix kernel so strict-native never downloads the layer.
            auto result = ctx.acquire_owned_fb(
                inputs[0]->width(), inputs[0]->height(), false);
            result->set_origin(inputs[0]->origin_x(), inputs[0]->origin_y());
            const auto source_record = ctx.surface_registry->lookup(source_handle);
            if (!source_record) {
                return NodeExecResult(NodeExecutionError{
                    RenderBackendErrorCode::ExecutionFailure,
                    std::string(name()), "native depth-grade source is not registered"});
            }
            result->clear_surface_handle();
            auto output_desc = source_record->desc;
            output_desc.lifetime = runtime::LifetimeClass::FrameTransient;
            const auto output_handle = ctx.surface_registry->create(output_desc);
            if (output_handle == runtime::kInvalidRenderSurfaceHandle) {
                return NodeExecResult(NodeExecutionError{
                    RenderBackendErrorCode::ExecutionFailure,
                    std::string(name()), "native depth-grade output allocation failed"});
            }
            const auto created = ctx.backend_surface_api()->create_surface(
                output_handle, output_desc);
            if (!created.ok()) {
                ctx.surface_registry->release(output_handle);
                return NodeExecResult(NodeExecutionError{
                    created.error().code, std::string(name()), created.error().message});
            }
            result->set_surface_handle(output_handle);

            const float fog_mix = t * m_grade.fog_opacity;
            const float fog_scale = 1.0f - fog_mix;
            const float saturation = std::min(
                1.0f, 1.0f + t * (m_grade.far_saturation - 1.0f));
            const float contrast = std::min(
                1.0f, 1.0f + t * (m_grade.far_contrast - 1.0f));
            constexpr float luma[3]{0.2126f, 0.7152f, 0.0722f};
            NativeColorTransform transform;
            transform.mode = NativeColorTransform::Mode::Affine;
            for (std::size_t row = 0; row < 3; ++row) {
                float fogged[3]{};
                for (std::size_t column = 0; column < 3; ++column) {
                    const float desat = saturation * (row == column ? 1.0f : 0.0f) +
                        (1.0f - saturation) * luma[column];
                    transform.p[row][column] = contrast * fog_scale * desat;
                    fogged[row] += desat *
                        (row == 0 ? m_grade.fog_color.r :
                         row == 1 ? m_grade.fog_color.g : m_grade.fog_color.b) * fog_mix;
                }
                transform.p[row][3] = contrast * fogged[row] +
                    0.5f * (1.0f - contrast);
            }
            const auto graded = ctx.backend_surface_api()->color_transform_surface(
                result->surface_handle(), source_handle, transform);
            if (!graded.ok()) {
                const auto message = graded.error().message;
                (void)ctx.backend_surface_api()->release_surface(output_handle);
                (void)ctx.surface_registry->release(output_handle);
                result->clear_surface_handle();
                return NodeExecResult(NodeExecutionError{
                    graded.error().code, std::string(name()), message});
            }
            result->mark_gpu_authoritative();
            result->set_opaque(inputs[0]->is_opaque());
            result->set_content_cleared(inputs[0]->is_content_cleared());
            return NodeExecResult{std::move(result)};
        }

        // DepthGrade is still the CPU reference implementation.  Never call
        // pixels_row() on a strict native surface: fail with a diagnostic
        // capability error until a matching Vulkan color-grade kernel exists.
        if (ctx.policy.require_native_gpu && !inputs[0]->data()) {
            return NodeExecResult(NodeExecutionError{
                RenderBackendErrorCode::UnsupportedCapability,
                std::string(name()),
                "depth grade requires a native GPU pass or CPU pixel backing"});
        }

        auto result = ctx.acquire_owned_fb(*inputs[0]);
        if (inputs[0]->surface_handle() != runtime::kInvalidRenderSurfaceHandle) {
            result->set_origin(inputs[0]->origin_x(), inputs[0]->origin_y());
        }

        for (i32 y = 0; y < result->height(); ++y) {
            Color* row = result->pixels_row(y);
            for (i32 x = 0; x < result->width(); ++x) {
                if (row[x].a <= 0.0f) continue;
                row[x] = m_grade.apply(row[x], t);
            }
        }

        return result;
    }

    static std::unique_ptr<DepthGradeNode> create(
        const rendering::DepthGrade& grade,
        float layer_world_z,
        bool layer_accepts_lights = true)
    {
        return std::make_unique<DepthGradeNode>(grade, layer_world_z, layer_accepts_lights);
    }

private:
    rendering::DepthGrade m_grade;
    float m_layer_world_z;
    bool m_accepts_lights;
};

} // namespace chronon3d::graph
