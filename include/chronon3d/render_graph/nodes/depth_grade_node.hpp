#pragma once

#include <chronon3d/render_graph/render_graph_node.hpp>
#include <chronon3d/render_graph/render_graph_hashing.hpp>
#include <chronon3d/rendering/depth_grade.hpp>
#include <chronon3d/scene/model/camera_2_5d.hpp>
#include <chronon3d/render_graph/render_backend.hpp>

#include <cmath>
#include <span>
#include <string>

namespace chronon3d::graph {

// Applies depth-based colour grading (fog, saturation, contrast) to a layer
// based on its world-space Z position.  This is a per-layer effect inserted
// after lighting and before compositing, analogous to DofEffectNode.
//
// The grading is done per-pixel using the layer's average world Z as input
// to DepthGrade::normalized_depth() and DepthGrade::apply().
class DepthGradeNode final : public RenderGraphNode {
public:
    DepthGradeNode(rendering::DepthGrade grade, float layer_world_z,
                   bool layer_accepts_lights)
        : m_grade(std::move(grade))
        , m_layer_world_z(layer_world_z)
        , m_accepts_lights(layer_accepts_lights) {}

    RenderGraphNodeKind kind() const override { return RenderGraphNodeKind::Effect; }
    std::string name() const override { return "DepthGrade"; }

    [[nodiscard]] bool cacheable() const override { return true; }

    cache::NodeCacheKey cache_key(const RenderGraphContext& ctx) const override {
        u64 h = hash_value(m_grade.enabled);
        h = hash_combine(h, hash_value(m_grade.near_z));
        h = hash_combine(h, hash_value(m_grade.far_z));
        h = hash_combine(h, hash_color(m_grade.fog_color));
        h = hash_combine(h, hash_value(m_grade.fog_opacity));
        h = hash_combine(h, hash_value(m_grade.far_saturation));
        h = hash_combine(h, hash_value(m_grade.far_contrast));
        h = hash_combine(h, hash_value(m_grade.far_blur_px));
        h = hash_combine(h, hash_value(m_layer_world_z));
        return cache::NodeCacheKey{
            .scope = "depth_grade",
            .frame = frame_dependent() ? ctx.frame : Frame{0},
            .width = ctx.width,
            .height = ctx.height,
            .params_hash = h
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

    OwnedFB execute(
        RenderGraphContext& ctx,
        std::span<const FramebufferRef> inputs,
        std::span<const std::optional<raster::BBox>>
    ) override {
        if (inputs.empty() || !inputs[0]) {
            return ctx.acquire_owned_fb(ctx.width, ctx.height);
        }

        if (!m_grade.enabled || !m_accepts_lights) {
            return ctx.acquire_owned_fb(*inputs[0]);
        }

        const float t = m_grade.normalized_depth(m_layer_world_z);
        if (t <= 0.001f) {
            return ctx.acquire_owned_fb(*inputs[0]); // near: no grading
        }

        auto result = ctx.acquire_owned_fb(*inputs[0]);

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
