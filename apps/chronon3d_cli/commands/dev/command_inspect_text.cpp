// SPDX-License-Identifier: MIT

// ═══════════════════════════════════════════════════════════════════════════
// command_inspect_text.cpp — §12 FU09 / TICKET-SIMPLICITY-INSPECT-TEXT
//
// Per-node TextRun audit with structured JSON output.  After the render
// graph executes, the graph builder has populated
// `renderer.text_audit_snapshots()` (see `src/render_graph/pipeline/
// scene.cpp` §8a) with one snapshot per TextRun node — each carrying
// the real `TextRunShape`, the real per-node world matrix, and the
// producer-supplied predicted/clip bboxes that match what the render
// graph actually rasterised.  This command consumes those snapshots
// DIRECTLY (no scene re-evaluation, no local reconstruction) and
// audits each via `audit_text_visibility()`.
//
// Exit code mapping (per §12 spec):
//   0 = PASS    — all nodes pass critical invariants
//   1 = FAIL    — composition not found, no text nodes, non-diagnostic
//                 build, or any node has font_resolved=false /
//                 finite=false / shaping_succeeded=false
//   2 = VIOLATION — any node has predicted_contains_world=false
//                  (FU04 trigger)
//
// JSON output (per-node entry):
//   {
//     "node": "<text_run_node_name>",
//     "font": "<font_path_or_unknown>",
//     "glyph_count": <int>,
//     "frame": <int>,
//     "local_bbox":   { ... },  // audit.local_ink_bbox  (audit-computed)
//     "world_bbox":   { ... },  // audit.world_ink_bbox  (audit-computed)
//     "predicted_bbox": { ... },// audit.predicted_bbox
//     "alpha_bbox":   { ... },  // audit.rendered_alpha_bbox
//     "status": "PASS" | "FAIL" | "VIOLATION"
//   }
//
// §8/§9 cleanup lineage: replaces the prior local-reconstruction path
// (FrameContext build + scene.layers() walk + per-node
// compute_text_run_world_bbox() + compute_text_run_visual_bounds() +
// post-audit `audit.local_ink_bbox = *snap.local_bbox` override) with
// direct consumption of the graph builder's `TextRunAuditSnapshot`
// (the same data path used by `command_text_def_inspect.cpp`, the
// reference clean pattern).
//
// AGENTS.md v0.1 freeze compliance: zero new public SDK API.  The
// function lives in the chronon3d_cli_dev sub-target (gated by
// CHRONON3D_BUILD_CLI_DEV, default OFF).  The audit call is gated by
// CHRONON3D_BUILD_DIAGNOSTICS; in non-diagnostic builds the command
// emits an error JSON and exits 1.
// ═══════════════════════════════════════════════════════════════════════════

#include "../../commands.hpp"
#include "command_inspect_text.hpp"

#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/text/text_visibility_audit.hpp>
#include <chronon3d/text/text_run_shape.hpp>

#include <spdlog/spdlog.h>

#include <cstdio>
#include <sstream>
#include <iomanip>
#include <string>

namespace chronon3d {
namespace cli {

// ── JSON helpers (TU-local) ────────────────────────────────────────────────

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
/// Gated by `CHRONON3D_BUILD_DIAGNOSTICS` because the
/// `TextVisibilityStatus` enum itself is gated by that macro in
/// `<chronon3d/text/text_visibility_audit.hpp>`.
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

/// Render the composition at the requested frame, consume the graph
/// builder's `TextRunAuditSnapshot` vector directly, audit each, emit
/// per-snapshot JSON.  Returns the worst-case exit code across all
/// snapshots (VIOLATION(2) > FAIL(1) > PASS(0)).
///
/// Step 3 lineage (§8/§9 cleanup): replaced the prior local reconstruction
/// (FrameContext build + scene.layers() walk + per-node
/// `compute_text_run_world_bbox()` + `compute_text_run_visual_bounds()`
/// + post-audit `audit.local_ink_bbox = *snap.local_bbox` override)
/// with direct consumption of the graph builder's snapshot vector.  The
/// audit itself computes `local_ink_bbox` and `world_ink_bbox` via the
/// canonical `compute_text_run_visual_bounds()` (Step 2 closure); the
/// prior override collapsed `world_bbox == predicted_bbox` always,
/// blocking the §9 FU04 violation-response invariant that requires
/// `world_bbox != predicted_bbox` to fire.
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

    // Render the composition.  The render call populates
    // `renderer.text_audit_snapshots()` via the graph builder's source
    // pass (scene.cpp §8a) — one snapshot per TextRun node carrying
    // the real `TextRunShape`, real per-node world matrix, and
    // producer-supplied predicted/clip bboxes (the 4 inputs to
    // `audit_text_visibility()`).
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

    // Consume the graph builder's snapshots (populated by scene.cpp
    // §8a during the render above).  Each entry is the producer-side
    // view: real TextRunShape + real per-node world matrix +
    // producer-supplied predicted/clip bboxes.
    const auto& snapshots = renderer.text_audit_snapshots();

    // Early-fail (§8 FU): the graph builder found zero TextRun nodes.
    // Emit an explicit error JSON and return 1.  Without this guard
    // the command would silently emit `[]\n` with exit 0 (PASS
    // bugiardo) when the composition has no text — `worst_exit_code`
    // starts at 0 and the loop body never executes, so an empty
    // scene would erroneously report PASS.
    if (snapshots.empty()) {
        std::ostringstream os;
        os << "{\n"
           << "  \"error\": \"no_text_nodes\",\n"
           << "  \"composition_id\": \"" << json_escape(args.comp_id) << "\",\n"
           << "  \"frame\": " << args.frame.integral() << ",\n"
           << "  \"status\": \"FAIL\",\n"
           << "  \"message\": \"graph builder found 0 TextRun nodes for the requested frame\"\n"
           << "}\n";
        std::fputs(os.str().c_str(), stdout);
        return 1;
    }

    // Emit one JSON entry per snapshot with the audit-computed
    // `local_ink_bbox` / `world_ink_bbox` (the audit computes these
    // via `compute_text_run_visual_bounds()` internally; no
    // post-audit override is applied).
    std::ostringstream os;
    os << "[\n";

    int worst_exit_code = 0;
    for (size_t i = 0; i < snapshots.size(); ++i) {
        const auto& snap = snapshots[i];

        // The audit takes a `const TextRunShape&`; for null handles
        // (graph builder captured a node whose text pipeline hadn't
        // wired the TextRunShape yet) we pass a default-constructed
        // empty shape so `font_resolved` evaluates to false → FAIL
        // (exit 1).  This preserves the §12 spec's per-node FAIL
        // semantic for unresolvable fonts.
        const TextRunShape* shape_ptr = snap.shape.get();
        TextRunShape        empty_shape{};
        const TextRunShape& shape = shape_ptr ? *shape_ptr : empty_shape;

        // Audit call.  `audit.local_ink_bbox` and `audit.world_ink_bbox`
        // are computed inside `audit_text_visibility()` via the
        // canonical `compute_text_run_visual_bounds()` (the audit
        // header is the single source of truth for these — see
        // `src/text/text_visibility_audit.cpp` §FU02).  The prior
        // post-audit override of these fields is REMOVED (Step 3):
        // it made `world_ink_bbox == predicted_bbox` always,
        // blocking the `world_bbox != predicted_bbox` invariant the
        // §9 FU04 violation response depends on.
        const TextVisibilityAudit audit = audit_text_visibility(
            shape, snap.world_matrix,
            snap.predicted_bbox, snap.clip_rect,
            fb.get(), /*effect_padding=*/0.0f);

        const auto mapping = map_status_for_node(
            audit.status, audit.predicted_contains_world);
        if (mapping.exit_code > worst_exit_code) {
            worst_exit_code = mapping.exit_code;
        }

        // Real font + glyph_count come from the materialised layout,
        // not from a hard-coded placeholder.  Falls back to
        // "<unknown>" / 0 when the shape has no resolved layout
        // (i.e. the font resolution failed upstream).
        // `audit.glyph_count` is computed the same way inside the
        // audit; we mirror the same source for JSON consistency.
        std::string  font_str = "<unknown>";
        if (shape.layout && !shape.layout->font.font_path.empty()) {
            font_str = shape.layout->font.font_path;
        }
        const std::size_t glyph_count = audit.glyph_count;

        if (i > 0) os << ",\n";
        os << "  {\n";
        os << "    \"node\": \"" << json_escape(snap.name) << "\",\n";
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
