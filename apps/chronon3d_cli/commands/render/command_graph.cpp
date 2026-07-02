#include "../../commands.hpp"
#include "../../utils/job/cli_render_utils.hpp"
#include <chronon3d/cache/cache_diagnostics.hpp>
#include <chronon3d/render_graph/pipeline/render_pipeline.hpp>
#include <chronon3d/render_graph/builder/graph_builder.hpp>
#include <chronon3d/scene/model/render/resolved_types.hpp>
#include <chronon3d/math/projector_2_5d.hpp>
#include <fmt/format.h>
#include <spdlog/spdlog.h>
#include <filesystem>
#include <fstream>
#include "src/render_graph/pipeline/helpers.hpp"
#include "src/render_graph/builder/graph_builder_internal.hpp"
#include "src/render_graph/builder/graph_builder_coordinates.hpp"
#include "src/render_graph/builder/graph_builder_bbox.hpp"
#include "src/render_graph/builder/graph_builder_pipeline.hpp"

namespace chronon3d {
namespace cli {

namespace {

std::string format_summary(const std::string& comp_id, Frame frame,
                            int width, int height,
                            const graph::SceneGraphStats& s) {
    std::string out;
    out += fmt::format("Graph summary: {} frame {}\n", comp_id, frame);
    out += fmt::format("Scene:\n");
    out += fmt::format("  layers:        {}\n", s.scene_layers);
    out += fmt::format("  width:         {}\n", width);
    out += fmt::format("  height:        {}\n", height);
    out += "\nGraph:\n";
    out += fmt::format("  nodes:         {}\n", s.total_nodes);
    if (s.output_id != graph::k_invalid_node)
        out += fmt::format("  output node:   {}\n", s.output_id);
    out += fmt::format("  sources:       {}\n", s.source_nodes);
    out += fmt::format("  transforms:    {}\n", s.transform_nodes);
    out += fmt::format("  effects:       {}\n", s.effect_nodes);
    out += fmt::format("  masks:         {}\n", s.mask_nodes);
    out += fmt::format("  composites:    {}\n", s.composite_nodes);
    out += fmt::format("  adjustments:   {}\n", s.adjustment_nodes);
    out += fmt::format("  precomps:      {}\n", s.precomp_nodes);
    out += fmt::format("  videos:        {}\n", s.video_nodes);
    out += fmt::format("  motion blur:   {}\n", s.motion_blur_nodes);
    out += fmt::format("  other:         {}\n", s.other_nodes);
    out += fmt::format("  cacheable:     {}\n", s.cacheable_nodes);

    if (s.execute_ms > 0.0) {
        out += "\nCache (all domains):\n";
        out += chronon3d::cache::format_cache_snapshot();
    }

    out += "\nTiming:\n";
    out += fmt::format("  graph_build:   {:.2f} ms\n", s.build_ms);
    if (s.execute_ms > 0.0)
        out += fmt::format("  graph_exec:    {:.2f} ms\n", s.execute_ms);
    out += fmt::format("  total:         {:.2f} ms\n", s.total_ms);
    return out;
}

bool write_dot(const std::string& dot, const std::string& path) {
    std::filesystem::path p(path);
    if (p.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(p.parent_path(), ec);
        if (ec) {
            spdlog::error("[graph] Cannot create directory {}: {}", p.parent_path().string(), ec.message());
            return false;
        }
    }
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f || !(f << dot)) {
        spdlog::error("[graph] Cannot write DOT to {}", path);
        return false;
    }
    spdlog::info("[graph] DOT saved to {}", path);
    return true;
}

} // namespace

namespace {

std::string format_bbox(const raster::BBox& bbox) {
    return fmt::format("[{}, {} -> {}, {}]", bbox.x0, bbox.y0, bbox.x1, bbox.y1);
}

[[nodiscard]] raster::BBox translate_bbox(const raster::BBox& bbox, i32 dx, i32 dy) {
    return raster::BBox{
        bbox.x0 + dx,
        bbox.y0 + dy,
        bbox.x1 + dx,
        bbox.y1 + dy
    };
}

std::string format_vec3(const Vec3& v) {
    return fmt::format("({:.1f}, {:.1f}, {:.1f})", v.x, v.y, v.z);
}

void print_plan(
    const std::string& comp_id,
    Frame frame,
    int width,
    int height,
    const chronon3d::graph::detail::LayerResolutionResult& resolved,
    const chronon3d::graph::RenderGraphContext& ctx,
    const std::shared_ptr<SoftwareRenderer>& renderer
) {
    fmt::print("Graph plan: {} frame {}\n", comp_id, frame);
    fmt::print("Scene:\n");
    fmt::print("  width:  {}\n", width);
    fmt::print("  height: {}\n", height);
    fmt::print("  layers: {}\n", resolved.layers.size());
    fmt::print("  modular_coordinates: {}\n", ctx.policy.modular_coordinates ? "true" : "false");
    fmt::print("  ssaa_factor: {:.3f}\n", ctx.policy.ssaa_factor);
    fmt::print("\nLayer placement:\n");

    for (std::size_t i = 0; i < resolved.layers.size(); ++i) {
        const auto& rl = resolved.layers[i];
        if (!rl.layer) continue;

        chronon3d::graph::LayerGraphItem item{
            .layer = rl.layer,
            .transform = rl.world_transform,
            .world_matrix = rl.world_matrix,
            .projected = false,
            .native_3d = rl.layer->uses_2_5d_projection,
            .insertion_index = rl.insertion_index,
        };

        const bool item_transform_any = item.transform.any();
        const bool implicit_center_only = chronon3d::graph::detail::is_implicit_2d_centering_only(item, ctx);
        const bool custom_transform = chronon3d::graph::detail::has_custom_render_transform(item, ctx);
        const bool needs_transform = chronon3d::graph::detail::layer_needs_render_transform(item, ctx);
        const bool use_local = ctx.policy.modular_coordinates && needs_transform && !item.native_3d;
        const bool centered = chronon3d::graph::detail::should_use_centered_rendering(item, ctx);
        const auto source_world = chronon3d::graph::detail::source_space_world_matrix(item, ctx);
        const auto canvas_center = chronon3d::graph::detail::implicit_canvas_center_matrix(ctx);

        fmt::print(
            "  [{}] layer='{}' kind={} any={} implicit_center_only={} custom_transform={} needs_transform={} use_local={} centered={} projected={} native_3d={} pos={}\n",
            i,
            rl.layer->name,
            static_cast<int>(rl.layer->kind),
            item_transform_any ? "true" : "false",
            implicit_center_only ? "true" : "false",
            custom_transform ? "true" : "false",
            needs_transform ? "true" : "false",
            use_local ? "true" : "false",
            centered ? "true" : "false",
            item.projected ? "true" : "false",
            item.native_3d ? "true" : "false",
            format_vec3(item.transform.position)
        );
        fmt::print(
            "       source_world_t=({:.1f}, {:.1f}, {:.1f}) canvas_center_t=({:.1f}, {:.1f}, {:.1f})\n",
            source_world[3][0], source_world[3][1], source_world[3][2],
            canvas_center[3][0], canvas_center[3][1], canvas_center[3][2]
        );

        if (renderer) {
            try {
                const auto bbox = chronon3d::graph::detail::compute_layer_bbox(item, ctx, renderer.get());
                const auto final_bbox = (!use_local && centered)
                    ? translate_bbox(bbox, width / 2, height / 2)
                    : bbox;
                auto visible_bbox = final_bbox;
                visible_bbox.clip_to(width, height);
                fmt::print("       bbox_work={}\n", format_bbox(bbox));
                fmt::print("       bbox_final={}\n", format_bbox(final_bbox));
                fmt::print("       bbox_visible={}\n", format_bbox(visible_bbox));
            } catch (const std::exception& e) {
                fmt::print("       bbox=<error: {}>\n", e.what());
            }
        }
    }

    fmt::print("\nLegend:\n");
    fmt::print("  any = item.transform.any()\n");
    fmt::print("  implicit_center_only = automatic fullscreen centering only\n");
    fmt::print("  custom_transform = real user/hierarchy transform\n");
    fmt::print("  needs_transform = layer enters the TransformNode/local path\n");
    fmt::print("  use_local = layer will get a local framebuffer / TransformNode path\n");
    fmt::print("  centered = renderer expects centered coordinates\n");
    fmt::print("\n");
}

} // namespace

int command_graph(const CompositionRegistry& registry, const GraphArgs& args) {
    auto resolved = resolve_composition(registry, args.comp_id);
    if (!resolved) return 1;

    const auto& comp = *resolved.comp;
    auto scene = comp.evaluate(args.frame);

    RenderSettings settings;
    settings.use_modular_graph = true;
    settings.diagnostics.enabled = args.plan;
    auto renderer = create_renderer(registry, settings);

    if (args.plan) {
        auto ctx = chronon3d::graph::make_graph_context(
            renderer->backend(), renderer->node_cache(), comp.camera,
            comp.width(), comp.height(),
            args.frame, 0.0f,
            settings, &registry, nullptr,
            static_cast<float>(comp.frame_rate().fps())
        );
        ctx.frame_input.light_context = scene.light_context();
        if (scene.camera_2_5d().enabled) {
            ctx.frame_input.camera_2_5d = scene.camera_2_5d();
            ctx.frame_input.has_camera_2_5d = true;
            ctx.frame_input.projection_ctx = chronon3d::renderer::make_projection_context(ctx.frame_input.camera_2_5d, ctx.frame_input.width, ctx.frame_input.height);
            ctx.frame_input.projection_ctx.ready = true;
        }

        auto resolved_layers = chronon3d::graph::detail::resolve_layers(scene, ctx);
        print_plan(args.comp_id, args.frame, comp.width(), comp.height(), resolved_layers, ctx, renderer);
        return 0;
    }

    if (args.summary) {
        const bool need_dot = !args.output.empty();
        const auto stats = graph::analyze_scene_graph(
            renderer->backend(), renderer->node_cache(), scene,
            comp.camera, comp.width(), comp.height(),
            args.frame, 0.0f, settings, &registry, nullptr,
            static_cast<float>(comp.frame_rate().fps()),
            /*execute=*/true, /*include_dot=*/need_dot
        );
        fmt::print("{}", format_summary(args.comp_id, args.frame, comp.width(), comp.height(), stats));
        if (need_dot && !write_dot(stats.dot, args.output)) return 1;
        return 0;
    }

    // Legacy: export DOT only
    const std::string dot_path = args.output.empty() ? "output/graph.dot" : args.output;
    const auto stats = graph::analyze_scene_graph(
        renderer->backend(), renderer->node_cache(), scene,
        comp.camera, comp.width(), comp.height(),
        args.frame, 0.0f, settings, &registry, nullptr,
        static_cast<float>(comp.frame_rate().fps()),
        /*execute=*/false, /*include_dot=*/true
    );
    return write_dot(stats.dot, dot_path) ? 0 : 1;
}

} // namespace cli
} // namespace chronon3d
