// SPDX-License-Identifier: MIT

// ═══════════════════════════════════════════════════════════════════════════
// command_inspect_text.cpp — §12 FU09 / Step 9 §B thin orchestrator
//
// Consumes the graph builder's `TextRunAuditSnapshot` vector (one entry
// per TextRun node, populated by `src/render_graph/pipeline/scene.cpp`
// §8a) and audits each via the canonical collector
// `text_inspection_collector.{hpp,cpp}`.  Step 9 §B slim removed the
// inline audit + status-mapping logic; the command now does ONLY arg
// parsing, render, per-snapshot JSON emission, and exit dispatch.
//
// Exit codes (per §12 spec): 0=PASS, 1=FAIL, 2=VIOLATION (FU04).
// Gated by CHRONON3D_BUILD_CLI_DEV (chronon3d_cli_dev sub-target,
// default OFF) + CHRONON3D_BUILD_DIAGNOSTICS (audit call).  Zero new
// public SDK API per AGENTS.md v0.1 freeze.
// ═══════════════════════════════════════════════════════════════════════════

#include "command_inspect_text.hpp"
#include "text_inspection_collector.hpp"   // Step 9 §B — audit + status mapping
#include "text_audit_helpers.hpp"          // canonical json_escape (no TU-local duplicate)

#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>

#include <cstdio>
#include <sstream>
#include <iomanip>
#include <string>

namespace chronon3d {
namespace cli {

namespace {

// TU-local `json_bbox(const Rect&)` — STAYS in the command because the
// signature differs from `json_bbox(const TextAuditBBox&)` (Rect has
// origin+size, TextAuditBBox has x0+y0+x1+y1) and the JSON output format
// differs (object `{x0, y0, x1, y1}` vs array `[x0, y0, x1, y1]`).
// Consolidating them would be a behavioural change, not a refactor.
std::string json_bbox(const Rect& r) {
    std::ostringstream os;
    os << std::fixed << std::setprecision(2);
    os << "{ \"x0\": " << r.origin.x
       << ", \"y0\": " << r.origin.y
       << ", \"x1\": " << (r.origin.x + r.size.x)
       << ", \"y1\": " << (r.origin.y + r.size.y) << " }";
    return os.str();
}

/// Emit the canonical error JSON for the (composition_not_found |
/// render_failed | no_text_nodes | diagnostics_disabled) cases.  The
/// `frame` field is omitted when `frame < 0`; the `message` field is
/// omitted when `message == nullptr`.  Reduces 3 inline error blocks
/// (~30 LoC) to 1 helper + 3 call sites.
void emit_error_json(const char* error_code,
                     const std::string& comp_id,
                     int frame,
                     const char* message = nullptr) {
    std::ostringstream os;
    os << "{\n"
       << "  \"error\": \"" << error_code << "\",\n"
       << "  \"composition_id\": \"" << json_escape(comp_id) << "\",\n";
    if (frame >= 0) {
        os << "  \"frame\": " << frame << ",\n";
    }
    if (message) {
        os << "  \"message\": \"" << json_escape(message) << "\",\n";
    }
    os << "  \"status\": \"FAIL\"\n"
       << "}\n";
    std::fputs(os.str().c_str(), stdout);
}

#ifdef CHRONON3D_BUILD_DIAGNOSTICS

/// Render the composition at the requested frame, consume the graph
/// builder's `TextRunAuditSnapshot` vector directly, audit each via the
/// collector, emit per-snapshot JSON.  Returns the worst-case exit code
/// across all snapshots (VIOLATION(2) > FAIL(1) > PASS(0)).
int run_inspect_text_impl(const CompositionRegistry& registry,
                          const InspectTextArgs& args) {
    if (!registry.contains(args.comp_id)) {
        emit_error_json("composition_not_found", args.comp_id, -1);
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
        emit_error_json("render_failed", args.comp_id, args.frame.integral());
        return 1;
    }

    const auto& snapshots = renderer.text_audit_snapshots();

    // Early-fail (§8 FU): the graph builder found zero TextRun nodes.
    // Emit an explicit error JSON and return 1.  Without this guard
    // the command would silently emit `[]\n` with exit 0 (PASS
    // bugiardo) when the composition has no text — `worst_exit_code`
    // starts at 0 and the loop body never executes, so an empty
    // scene would erroneously report PASS.
    if (snapshots.empty()) {
        emit_error_json("no_text_nodes", args.comp_id, args.frame.integral(),
                        "graph builder found 0 TextRun nodes for the requested frame");
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
        // Step 9 §B — delegate the audit + status mapping to the
        // canonical collector.  Returns the `(mapping, record)` pair
        // so this loop can stay focused on JSON emission + worst-exit
        // -code tracking.  The collector handles the null-shape
        // fallback (empty shape → font_resolved=false → FAIL) +
        // materialised font_path extraction + audit invocation.
        const auto sr = map_to_status(snapshots[i], fb.get(), args.frame.integral());
        if (sr.mapping.exit_code > worst_exit_code) {
            worst_exit_code = sr.mapping.exit_code;
        }
        const auto& audit       = sr.record.audit;
        const std::string& font_str = sr.record.font_path;
        const std::size_t  glyph_count = sr.record.glyph_count;

        if (i > 0) os << ",\n";
        os << "  {\n    \"node\": \"" << json_escape(snapshots[i].name)
           << "\",\n    \"font\": \"" << json_escape(font_str)
           << "\",\n    \"glyph_count\": " << glyph_count
           << ",\n    \"frame\": " << args.frame.integral()
           << ",\n    \"local_bbox\": "  << json_bbox(audit.local_ink_bbox)
           << ",\n    \"world_bbox\": "  << json_bbox(audit.world_ink_bbox)
           << ",\n    \"predicted_bbox\": " << json_bbox(audit.predicted_bbox)
           << ",\n    \"alpha_bbox\": "  << json_bbox(audit.rendered_alpha_bbox)
           << ",\n    \"status\": \"" << sr.mapping.json_status << "\"\n  }";
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
int run_inspect_text_impl(const CompositionRegistry& /*registry*/,
                          const InspectTextArgs& /*args*/) {
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
