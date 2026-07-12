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
#include <chronon3d/text/text_run_layout.hpp>
#include <chronon3d/text/text_run_geometry.hpp>
#include <chronon3d/scene/model/core/scene.hpp>
#include <chronon3d/scene/model/layer/layer.hpp>
#include <chronon3d/scene/model/shape/shape.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <chronon3d/media/media_placement.hpp>

#include <spdlog/spdlog.h>

#include <cstdio>
#include <cmath>
#include <optional>
#include <sstream>
#include <iomanip>
#include <string>
#include <vector>

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
///
/// Gated by `CHRONON3D_BUILD_DIAGNOSTICS` because the `TextVisibilityStatus`
/// enum itself is gated by that macro in `<chronon3d/text/text_visibility_
/// audit.hpp>` (the audit's entire header is `#ifdef`-gated per the FU02
/// freeze contract).  Moving the mapping inside the guard keeps the
/// non-diagnostic build from referencing an undeclared enum.
#ifdef CHRONON3D_BUILD_DIAGNOSTICS
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
#endif // CHRONON3D_BUILD_DIAGNOSTICS

#ifdef CHRONON3D_BUILD_DIAGNOSTICS

/// Render the composition at the requested frame, then walk the render
/// graph to find TextRunNodes. For each TextRunNode, call
/// `audit_text_visibility` and emit a JSON entry. Returns the aggregate
/// exit code (worst-case across all nodes).
///
/// Implementation: renders the composition in process (no CLI subprocess),
/// then evaluates the scene at the same frame to find every
/// `ShapeType::TextRun` node (the `text_run_shape_handle().value` may be
/// null if font resolution failed upstream).  For each node, builds a
/// `TextRunSnapshot` with the real `TextRunShape`, the per-node
/// `world_transform.to_mat4()`, and the canonical
/// `renderer::compute_text_run_world_bbox()` / `compute_text_run_visual_
/// bounds()` outputs.  Calls `audit_text_visibility()` with the real
/// data and overrides the audit's `local_ink_bbox` + `world_ink_bbox`
/// with the canonical per-glyph accumulator values (the audit itself
/// now also computes these correctly via `compute_text_run_visual_bounds`
/// — the override below remains as belt-and-braces for any caller that
/// supplies a non-canonical audit path; see TICKET-VISIBILITY-OVERRIDE-
/// DEDUP forward-point for the Step 8 cleanup).  Emits one JSON object
/// per node.
///
/// The per-node `font` field reads `shape.layout->font.font_path` (the
/// materialised layout's authoring-level font spec); the per-node
/// `glyph_count` reads `shape.layout->placed.glyphs.size()`.  When the
/// shape has no resolved layout (font resolution failed upstream), the
/// `font` field falls back to "<unknown>" and `glyph_count` to 0.
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
    SoftwareRenderer renderer{Config{}};
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

    // Bind the renderer's FontEngine into the FrameContext so the
    // M1.5#9 materializer (invoked via `comp.evaluate(ctx)` →
    // `SceneBuilder(ctx).m_font_engine` → `LayerBuilder::text_run` →
    // `materialize_text_run_shape`) populates `text_run_shape_handle
    // ().value` with a non-null TextRunShape.  Without this binding
    // every TextRun node would carry a null handle and the scene-walk
    // would collapse to the "font missing" branch (full-canvas + FAIL
    // for every entry).  The renderer's engine is the same one the
    // render graph consumed, so the scene-walk snapshot data matches
    // what was actually rendered.
    ctx.font_engine = renderer.font_engine().get();

    // Walk the scene at the requested frame to find every TextRun node.
    // Each node provides the REAL TextRunShape + per-node world matrix
    // that the render graph consumed; the audit operates on those real
    // values instead of the §12 placeholder (empty shape, identity
    // matrix, full-canvas predicted bbox).  Per FU10 (TICKET-SIMPLICITY-
    // INSPECT-TEXT extension), the JSON array now has ONE entry per
    // actual TextRun node found, populated with the real snapshot data
    // (node name, font path, glyph count, local/world/predicted/alpha
    // bboxes, status).
    const Scene scene = comp.evaluate(ctx);

    // Collect per-node snapshots: each entry pairs the real
    // TextRunShape with the render-graph world matrix + bboxes.
    struct TextRunSnapshot {
        std::string                  node_name;
        std::shared_ptr<TextRunShape> shape;          // may be null (font missing)
        Mat4                         world_matrix;
        raster::BBox                 world_bbox_raster;  // cached world bbox (i32)
        Rect                         predicted_bbox; // canvas-space (float Rect)
        Rect                         clip_rect;      // canvas-space (= predicted_bbox)
        std::optional<Rect>          local_bbox;     // text-frame local-space
    };
    std::vector<TextRunSnapshot> snapshots;
    snapshots.reserve(4);
    for (const auto& layer : scene.layers()) {
        for (const auto& node : layer.nodes) {
            if (node.shape.type() != ShapeType::TextRun) continue;

            const auto& handle = node.shape.text_run_shape_handle();

            TextRunSnapshot snap;
            snap.node_name    = std::string(node.name);
            snap.shape        = handle.value;
            // The render graph's per-node world matrix is the resolved
            // layer→canvas transform baked onto the RenderNode (see
            // `src/render_graph/layer_resolver.cpp::ResolvedNode::world_
            // matrix`).  Using it here mirrors the audit invocation
            // performed by TextRunNode at execute time.
            snap.world_matrix = node.world_transform.to_mat4();
            // Full-canvas fallback for nodes with no resolved shape —
            // the audit's `font_resolved` flag will flag the missing
            // font, but the predicted/clip rects still need *some*
            // bounded value to evaluate the containment invariants.
            const Rect canvas_rect{
                {0.0f, 0.0f},
                {static_cast<float>(ctx.width),
                 static_cast<float>(ctx.height)}
            };
            snap.clip_rect = canvas_rect;

            if (handle.value) {
                const TextRunShape& s = *handle.value;
                // Real predicted bbox: the canonical
                // `compute_text_run_world_bbox(*shape, world_matrix, 0.0f)`
                // mirrors `TextRunNode::predicted_bbox()` exactly (no
                // spread: inspect-text reports the tight producer bbox;
                // the FU04 violation response expansion is internal to
                // the audit and doesn't change what we serialise).
                // Cached in the snapshot to avoid a redundant compute
                // when overriding `audit.world_ink_bbox` below.
                snap.world_bbox_raster = renderer::compute_text_run_world_bbox(
                    s, snap.world_matrix, /*spread=*/0.0f);
                const float bw = static_cast<float>(snap.world_bbox_raster.x1
                                                  - snap.world_bbox_raster.x0);
                const float bh = static_cast<float>(snap.world_bbox_raster.y1
                                                  - snap.world_bbox_raster.y0);
                snap.predicted_bbox = Rect{
                    {static_cast<float>(snap.world_bbox_raster.x0),
                     static_cast<float>(snap.world_bbox_raster.y0)},
                    {bw, bh}
                };
                // TextRunNode's compositor uses `predicted_bbox` as the
                // clip rect (see `compute_dirty_clip`); match that here
                // so the JSON clip_rect is what the GPU actually rasterised.
                snap.clip_rect = snap.predicted_bbox;

                // Real local bbox: the canonical per-glyph accumulator.
                // The audit (post Step 2) calls the same helper internally
                // and would compute an identical value; the post-audit
                // override below remains as forward-compatibility
                // (TICKET-VISIBILITY-OVERRIDE-DEDUP for Step 8 dedup).
                if (auto lb = renderer::compute_text_run_visual_bounds(s)) {
                    snap.local_bbox = Rect{
                        {lb->min_x, lb->min_y},
                        {lb->max_x - lb->min_x,
                         lb->max_y - lb->min_y}
                    };
                }
            } else {
                // No materialised TextRunShape ⇒ font resolution failed.
                // Predicted bbox collapses to the full canvas; the audit
                // reports FAIL (font_resolved=false) for this entry.
                snap.predicted_bbox = canvas_rect;
            }
            snapshots.push_back(std::move(snap));
        }
    }

    // Emit one JSON entry per snapshot with REAL snapshot data; the
    // worst-case exit code across all nodes drives the process return
    // value (VIOLATION(2) > FAIL(1) > PASS(0); default 0 if the scene
    // had no TextRun nodes).
    std::ostringstream os;
    os << "[\n";

    int worst_exit_code = 0;
    for (size_t i = 0; i < snapshots.size(); ++i) {
        const auto& snap = snapshots[i];

        // Audit call.  The audit takes a `const TextRunShape&`; for
        // null handles we pass a default-constructed (empty) shape
        // so `font_resolved` evaluates to false (FAIL → exit 1).
        const TextRunShape* shape_ptr = snap.shape.get();
        TextRunShape        empty_shape{};
        const TextRunShape& shape = shape_ptr ? *shape_ptr : empty_shape;

        TextVisibilityAudit audit = audit_text_visibility(
            shape, snap.world_matrix,
            snap.predicted_bbox, snap.clip_rect,
            fb.get(), /*effect_padding=*/0.0f);
        // Belt-and-braces override (Step 2 fix (d) docs note): the
        // audit now computes `local_ink_bbox` via the canonical
        // `renderer::compute_text_run_visual_bounds` internally, so
        // the override is redundant for the canonical path.  Kept
        // here to defend against future audit simplification that
        // could regress the public contract.  Forward-point:
        // TICKET-VISIBILITY-OVERRIDE-DEDUP consolidates both paths
        // in a Step 8 refactor.
        if (snap.local_bbox) {
            audit.local_ink_bbox = *snap.local_bbox;
            audit.world_ink_bbox = snap.predicted_bbox;
        }

        const auto mapping = map_status_for_node(
            audit.status, audit.predicted_contains_world);
        if (mapping.exit_code > worst_exit_code) {
            worst_exit_code = mapping.exit_code;
        }

        // Real font + glyph_count come from the materialised layout,
        // not from a hard-coded placeholder.  Falls back to
        // "<unknown>" / 0 when the shape has no resolved layout (i.e.
        // the font resolution failed upstream).  `audit.glyph_count`
        // is computed the same way inside the audit; we use the
        // same source here so the JSON's `glyph_count` matches.
        std::string  font_str = "<unknown>";
        if (shape.layout && !shape.layout->font.font_path.empty()) {
            font_str = shape.layout->font.font_path;
        }
        const std::size_t glyph_count = audit.glyph_count;

        if (i > 0) os << ",\n";
        os << "  {\n";
        os << "    \"node\": \"" << json_escape(snap.node_name) << "\",\n";
        os << "    \"font\": \"" << json_escape(font_str) << "\",\n";
        os << "    \"glyph_count\": " << glyph_count << ",\n";
        os << "    \"frame\": " << args.frame.integral() << ",\n";
        os << "    \"local_bbox\": "  << json_bbox(audit.local_ink_bbox)  << ",\n";
        os << "    \"world_bbox\": "  << json_bbox(audit.world_ink_bbox)  << ",\n";
        os << "    \"predicted_bbox\": " << json_bbox(audit.predicted_bbox) << ",\n";
        os << "    \"alpha_bbox\": "  << json_bbox(audit.rendered_alpha_bbox) << ",\n";
        os << "    \"status\": \"" << mapping.json_status << "\"\n";
        os << "  }";
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
