// =============================================================================
// CLI Command: inspect text-visibility
// Export a JSON diagnostic that combines real TextRunShape data
// (glyph_count) with a rendered alpha-bbox scan and an overall PASS/FAIL
// status.  Used by test_inspect_text to verify that text inspection no
// longer relies on placeholder data.
// =============================================================================
#include "../../commands.hpp"
#include "../../utils/job/cli_render_utils.hpp"

#include <chronon3d/scene/model/core/scene.hpp>
#include <chronon3d/scene/model/layer/layer.hpp>
#include <chronon3d/scene/model/shape/shape.hpp>
#include <chronon3d/scene/model/render/render_node.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/software/render_settings.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/api/composition.hpp>
#include <chronon3d/api/scene.hpp>

#ifdef CHRONON3D_BUILD_DIAGNOSTICS
#include <chronon3d/text/text_visibility_audit.hpp>
#include <chronon3d/text/text_run_geometry.hpp>
#endif

#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <fmt/format.h>

#include <fstream>
#include <sstream>

namespace chronon3d {
namespace cli {

namespace {

nlohmann::json rect_to_json(const Rect& r) {
    return nlohmann::json{
        {"x", r.origin.x},
        {"y", r.origin.y},
        {"w", r.size.x},
        {"h", r.size.y}
    };
}

} // anonymous namespace

int command_text_visibility(const CompositionRegistry& registry,
                            const TextVisibilityArgs& args) {
#ifndef CHRONON3D_BUILD_DIAGNOSTICS
    spdlog::error("text-visibility: requires CHRONON3D_BUILD_DIAGNOSTICS=ON");
    return 1;
#else
    auto resolved = resolve_composition(registry, args.comp_id);
    if (!resolved) {
        spdlog::error("text-visibility: unknown composition '{}'", args.comp_id);
        return 1;
    }

    const auto& comp = *resolved.comp;

    // Render the composition at the requested frame so we can scan real ink pixels.
    RenderSettings settings;
    settings.use_modular_graph = true;
    settings.dirty.enabled = false;
    auto renderer = create_renderer(registry, settings);
    auto fb = renderer->render(comp, Frame{args.frame});
    if (!fb) {
        spdlog::error("text-visibility: render produced no output");
        return 1;
    }

    // Evaluate the scene to walk TextRun nodes.
    Scene scene = comp.evaluate(Frame{args.frame});

    nlohmann::json js;
    js["composition"] = args.comp_id;
    js["frame"] = args.frame;
    js["canvas"] = {{"width", fb->width()}, {"height", fb->height()}};

    auto& nodes_json = js["text_nodes"] = nlohmann::json::array();
    bool any_fail = false;

    for (const auto& layer : scene.layers()) {
        for (const auto& node : layer.nodes) {
            if (node.shape.type() != ShapeType::TextRun) continue;

            const auto& handle = node.shape.text_run_shape_handle();
            if (!handle.value) continue;

            const auto& shape = *handle.value;
            if (!shape.layout) continue;

            // Compute the real local/world ink bbox from the shaped glyphs
            // and the node's world transform.  Use that bbox both as the
            // predicted/clip region and as the alpha-scan region so each
            // text node reports its own rendered ink extent.
            Rect predicted_bbox{{0.0f, 0.0f},
                                  {static_cast<float>(fb->width()),
                                   static_cast<float>(fb->height())}};
            Rect clip_rect = predicted_bbox;
            const Rect* alpha_region = nullptr;

            auto local_bounds = renderer::compute_text_run_visual_bounds(shape);
            if (local_bounds) {
                Rect local_ink{
                    {local_bounds->min_x, local_bounds->min_y},
                    {local_bounds->max_x - local_bounds->min_x,
                     local_bounds->max_y - local_bounds->min_y}};
                predicted_bbox = transform_aabb(local_ink,
                                                node.world_transform.to_mat4());
                clip_rect = predicted_bbox;
                alpha_region = &predicted_bbox;
            }

            auto audit = audit_text_visibility(
                shape,
                node.world_transform.to_mat4(),
                predicted_bbox,
                clip_rect,
                fb.get(),
                alpha_region
            );

            nlohmann::json nj;
            nj["node_name"] = std::string(node.name);
            nj["glyph_count"] = audit.glyph_count;
            nj["font_resolved"] = audit.font_resolved;
            nj["shaping_succeeded"] = audit.shaping_succeeded;
            nj["alpha_bbox"] = rect_to_json(audit.rendered_alpha_bbox);
            nj["world_ink_bbox"] = rect_to_json(audit.world_ink_bbox);
            nj["predicted_contains_world"] = audit.predicted_contains_world;
            nj["clip_contains_visible_ink"] = audit.clip_contains_visible_ink;

            // Overall status: PASS only if all invariants hold and we have
            // real glyphs + visible ink.
            bool pass = audit.font_resolved &&
                        audit.shaping_succeeded &&
                        audit.glyph_count > 0 &&
                        audit.finite &&
                        audit.predicted_contains_world &&
                        audit.clip_contains_visible_ink &&
                        (audit.rendered_alpha_bbox.size.x > 0.0f) &&
                        (audit.rendered_alpha_bbox.size.y > 0.0f);
            if (!pass) any_fail = true;

            nj["status"] = pass ? "PASS" : "FAIL";
            nodes_json.push_back(std::move(nj));
        }
    }

    if (nodes_json.empty()) {
        any_fail = true;
        js["error"] = "no text nodes found in composition";
    }

    js["overall_status"] = any_fail ? "FAIL" : "PASS";
    js["exit_code"] = any_fail ? 1 : 0;

    if (!args.json_output.empty()) {
        std::ofstream out(args.json_output);
        if (!out) {
            spdlog::error("text-visibility: cannot write to '{}'", args.json_output);
            return 1;
        }
        out << js.dump(2) << "\n";
        spdlog::info("text-visibility: JSON written to '{}'", args.json_output);
    } else {
        fmt::print("{}\n", js.dump(2));
    }

    return any_fail ? 1 : 0;
#endif
}

} // namespace cli
} // namespace chronon3d
