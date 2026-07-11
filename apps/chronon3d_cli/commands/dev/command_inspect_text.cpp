// SPDX-License-Identifier: MIT

// ═══════════════════════════════════════════════════════════════════════════
// command_inspect_text.cpp — §12 FU09 / TICKET-SIMPLICITY-INSPECT-TEXT
//
// Per-node TextRun audit with structured JSON output. Walks the rendered
// scene's render graph to find TextRunNodes, calls audit_text_visibility()
// for each, and emits a JSON array (one entry per TextRun node) to stdout.
//
// Exit code mapping (per §12 spec):
//   0 = PASS    — all nodes pass critical invariants
//   1 = FAIL    — composition not found, non-diagnostic build, or any node
//                 has font_resolved=false / finite=false / shaping_succeeded=false
//   2 = VIOLATION — any node has predicted_contains_world=false (FU04 trigger)
//
// JSON output (per-node entry):
//   {
//     "node": "<text_run_node_name>",
//     "font": "<font_path_or_placeholder>",
//     "glyph_count": <int>,
//     "frame": <int>,
//     "local_bbox":  { "x0": f, "y0": f, "x1": f, "y1": f },
//     "world_bbox":  { ... },
//     "predicted_bbox": { ... },
//     "alpha_bbox": { ... },
//     "status": "PASS" | "FAIL" | "VIOLATION"
//   }
//
// AGENTS.md v0.1 freeze compliance: zero new public SDK API. The function
// lives in the chronon3d_cli_dev sub-target (gated by
// CHRONON3D_BUILD_CLI_DEV, default OFF). The audit call is gated by
// CHRONON3D_BUILD_DIAGNOSTICS; in non-diagnostic builds the command emits
// an error JSON and exits 1.
// ═══════════════════════════════════════════════════════════════════════════

#include "../../commands.hpp"
#include "command_inspect_text.hpp"

#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/text/text_visibility_audit.hpp>
#include <chronon3d/text/text_run_shape.hpp>
#include <chronon3d/text/text_run_geometry.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/media/media_placement.hpp>
#include <chronon3d/render_graph/builder/graph_builder.hpp>
#include <chronon3d/render_graph/nodes/text_run_node.hpp>
#include "../../../../../src/render_graph/pipeline/helpers.hpp"
#include <chronon3d/scene/model/core/scene.hpp>
#include <chronon3d/scene/model/camera/camera.hpp>
#include <chronon3d/core/config.hpp>

#include <spdlog/spdlog.h>

#include <cstdio>
#include <cmath>
#include <sstream>
#include <iomanip>
#include <string>
#include <vector>
#include <memory>

namespace chronon3d {
namespace cli {

// ── JSON helpers (TU-local, mirrors text_audit_json.cpp pattern) ──────────

namespace {

std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x",
                                  static_cast<unsigned char>(c));
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

std::string json_bbox(const Rect& r) {
    std::ostringstream os;
    os << std::fixed << std::setprecision(2);
    os << "{ \"x0\": " << r.origin.x
       << ", \"y0\": " << r.origin.y
       << ", \"x1\": " << (r.origin.x + r.size.x)
       << ", \"y1\": " << (r.origin.y + r.size.y) << " }";
    return os.str();
}

/// Map `TextVisibilityStatus` enum to the JSON status string + exit code.
/// §12 spec exit codes:
///   PASS    → "PASS"     → 0
///   FAIL    → "FAIL"     → 1
///   VIOLATION (predicted_contains_world=false) → "VIOLATION" → 2
struct StatusMapping {
    const char* json_status;
    int         exit_code;
};

StatusMapping map_status_for_node(TextVisibilityStatus s,
                                  bool predicted_contains_world) {
    if (predicted_contains_world == false) {
        // FU04 violation: predicted bbox does not contain world ink.
        // This is the trigger for the §9 FU04 violation response.
        return {"VIOLATION", 2};
    }
    switch (s) {
        case TextVisibilityStatus::PASS:
            return {"PASS", 0};
        case TextVisibilityStatus::FAIL:
            return {"FAIL", 1};
        case TextVisibilityStatus::INDETERMINATE:
            // Default-initialized struct (audit not yet evaluated). Treat
            // as FAIL to fail-closed: callers that see this know the
            // audit did not run.
            return {"FAIL", 1};
    }
    return {"FAIL", 1};
}

#ifdef CHRONON3D_BUILD_DIAGNOSTICS

/// Render the composition at the requested frame, then walk the render
/// graph to find TextRunNodes. For each TextRunNode, call
/// `audit_text_visibility` and emit a JSON entry. Returns the aggregate
/// exit code (worst-case across all nodes).
///
/// Implementation note: this is a minimal version that demonstrates the
/// §12 JSON schema + exit code mapping. It renders the composition in
/// process (no CLI subprocess), walks the scene's render graph for
/// TextRunNode instances, and emits one JSON object per node. The
/// per-node `font` field reads `shape.engine->font_path()` if available
/// (placeholder "<font>" otherwise); the per-node `glyph_count` reads
/// `shape.layout.placed.glyphs.size()`.
int run_inspect_text_impl(const CompositionRegistry& registry,
                          const InspectTextArgs& args) {
    if (!registry.contains(args.comp_id)) {
        // Composition not found: error JSON, exit 1.
        std::ostringstream os;
        os << "{\n"
           << "  \"error\": \"composition_not_found\",\n"
           << "  \"composition_id\": \"" << json_escape(args.comp_id) << "\",\n"
           << "  \"status\": \"FAIL\"\n"
           << "}\n";
        std::fputs(os.str().c_str(), stdout);
        return 1;
    }

    auto comp = registry.create(args.comp_id);

    // Build a minimal FrameContext for the requested frame.
    // Field order matches `include/chronon3d/core/types/frame_context.hpp`
    // declaration order (frame, local_frame, frame_time, duration,
    // frame_rate, width, height, assets_root, assets, resource,
    // font_engine) — designated-initializer syntax.
    FrameContext ctx{
        .frame = args.frame,
        .local_frame = args.frame,
        .frame_time = 0.0f,
        .duration = comp.duration(),
        .frame_rate = comp.frame_rate(),
        .width = comp.width(),
        .height = comp.height(),
        .assets_root = comp.assets_root(),
        .assets = nullptr,   // PR 2 migration path; §12 minimal leaves empty
        .resource = std::pmr::get_default_resource(),
        .font_engine = nullptr,
    };

    // Render the composition to a framebuffer.
    Config config;
    SoftwareRenderer renderer{config};
    auto fb = renderer.render(comp, args.frame);
    if (!fb) {
        std::ostringstream os;
        os << "{\n"
           << "  \"error\": \"render_failed\",\n"
           << "  \"composition_id\": \"" << json_escape(args.comp_id) << "\",\n"
           << "  \"frame\": " << args.frame.integral() << ",\n"
           << "  \"status\": \"FAIL\"\n"
           << "}\n";
        std::fputs(os.str().c_str(), stdout);
        return 1;
    }

    // Walk the render graph to find TextRunNodes.
    //
    // The framebuffer is passed to the audit to enable the alpha-bbox
    // scan (full 6-invariant cascade).
    std::ostringstream os;
    os << "[\n";

    // Evaluate the scene and build the render graph so we can inspect
    // the real TextRunNode instances.
    Scene scene = comp.evaluate(args.frame, 0.0f, &renderer.font_engine());
    Camera default_camera;
    chronon3d::graph::RenderGraphContext graph_ctx = graph::make_graph_context(
        renderer.backend(), renderer.node_cache(), default_camera,
        comp.width(), comp.height(), args.frame, 0.0f,
        renderer.render_settings(), &registry, nullptr,
        static_cast<float>(comp.frame_rate().fps()));
    graph_ctx.services.sw_renderer_sidecar = &renderer;
    if (!scene.assets_root().empty()) {
        graph_ctx.frame_input.assets_root = scene.assets_root().string();
    }

    graph::RenderGraph graph = graph::GraphBuilder::build(scene, graph_ctx);
    graph.freeze();

    int worst_exit_code = 0;
    bool first = true;
    for (chronon3d::graph::GraphNodeId i = 0; i < graph.size(); ++i) {
        if (!graph.has_node(i)) continue;
        auto* node = dynamic_cast<graph::TextRunNode*>(&graph.node(i));
        if (!node) continue;

        const auto& shape_ptr = node->shape();
        if (!shape_ptr) continue;

        const Mat4 world_matrix = node->world_matrix(graph_ctx);
        auto pred = node->predicted_bbox(graph_ctx);
        Rect predicted_rect;
        if (pred) {
            predicted_rect = Rect{
                Vec2{static_cast<float>(pred->x0), static_cast<float>(pred->y0)},
                Vec2{static_cast<float>(pred->x1 - pred->x0),
                     static_cast<float>(pred->y1 - pred->y0)}};
        } else {
            predicted_rect = Rect{
                Vec2{0.0f, 0.0f},
                Vec2{static_cast<float>(comp.width()),
                     static_cast<float>(comp.height())}};
        }
        const Rect clip_rect = predicted_rect;

        // Mirror the effect-padding computation in TextRunNode::predicted_bbox()
        // so the audit uses the same spread the renderer uses.
        const auto& render_ref = node->render_node();
        float effect_padding = 0.0f;
        if (render_ref.shadow.enabled) {
            effect_padding = std::max(
                effect_padding,
                render_ref.shadow.radius
                    + std::max(std::abs(render_ref.shadow.offset.x),
                               std::abs(render_ref.shadow.offset.y)));
        }
        if (render_ref.glow.enabled) {
            effect_padding = std::max(effect_padding, render_ref.glow.radius);
        }
        effect_padding += 8.0f;

        Rect local_ink_bbox{};
        if (auto bounds = renderer::compute_text_run_visual_bounds(*shape_ptr)) {
            local_ink_bbox = renderer::to_rect(*bounds);
        }
        const auto audit = audit_text_visibility(
            *shape_ptr, local_ink_bbox, world_matrix, predicted_rect,
            clip_rect, fb.get(), effect_padding);

        const auto mapping = map_status_for_node(audit.status,
                                                audit.predicted_contains_world);

        if (!first) os << ",\n";
        first = false;

        std::string font_path = "<font>";
        if (shape_ptr->layout && !shape_ptr->layout->font.font_path.empty()) {
            font_path = shape_ptr->layout->font.font_path;
        }

        os << "  {\n";
        os << "    \"node\": \"" << json_escape(std::string(node->name())) << "\",\n";
        os << "    \"font\": \"" << json_escape(font_path) << "\",\n";
        os << "    \"glyph_count\": " << audit.glyph_count << ",\n";
        os << "    \"frame\": " << args.frame.integral() << ",\n";
        os << "    \"local_bbox\": " << json_bbox(audit.local_ink_bbox)
           << ",\n";
        os << "    \"world_bbox\": " << json_bbox(audit.world_ink_bbox)
           << ",\n";
        os << "    \"predicted_bbox\": " << json_bbox(audit.predicted_bbox)
           << ",\n";
        os << "    \"alpha_bbox\": " << json_bbox(audit.rendered_alpha_bbox)
           << ",\n";
        os << "    \"status\": \"" << mapping.json_status << "\"\n";
        os << "  }";

        worst_exit_code = std::max(worst_exit_code, mapping.exit_code);
    }

    os << "\n]\n";
    std::fputs(os.str().c_str(), stdout);
    return worst_exit_code;
}

#else  // !CHRONON3D_BUILD_DIAGNOSTICS

/// Non-diagnostic build: the audit function is gated. Emit an error JSON
/// and exit 1. The §12 spec does not require a non-diagnostic fallback;
/// callers must build with CHRONON3D_BUILD_DIAGNOSTICS=ON to use this
/// subcommand.
int run_inspect_text_impl(const CompositionRegistry& registry,
                          const InspectTextArgs& args) {
    (void)registry;
    (void)args;
    std::ostringstream os;
    os << "{\n"
       << "  \"error\": \"diagnostics_disabled\",\n"
       << "  \"status\": \"FAIL\",\n"
       << "  \"message\": \"inspect-text requires CHRONON3D_BUILD_DIAGNOSTICS=ON; \"\n"
       << "             \"rebuild with diagnostics enabled to use this subcommand\"\n"
       << "}\n";
    std::fputs(os.str().c_str(), stdout);
    return 1;
}

#endif // CHRONON3D_BUILD_DIAGNOSTICS

}  // anonymous namespace

int command_inspect_text(const CompositionRegistry& registry,
                         const InspectTextArgs& args) {
    return run_inspect_text_impl(registry, args);
}

} // namespace cli
} // namespace chronon3d
