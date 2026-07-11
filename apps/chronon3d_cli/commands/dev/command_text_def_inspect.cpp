// ==============================================================================
// CLI Command: inspect text-def (TICKET-CHRONON-GLOW-FINAL Fase 5)
//
// After the render, consume `renderer.text_audit_snapshots()` and call
// `audit_text_visibility()` per snapshot.  Output: JSON array of
// per-snapshot objects with the canonical 6-field shape:
//
//   { "node": "glow_pulse",
//     "font_resolved": true,
//     "glyph_count": 10,
//     "predicted_contains_world": true,
//     "alpha_bbox_empty": false,
//     "status": "PASS" }
//
// Gated by `CHRONON3D_BUILD_DIAGNOSTICS` — in non-diagnostic builds
// the command emits an error JSON and returns exit 1 (matches the
// inspect-text #5 test case pattern for diagnostic-gated commands).
// ==============================================================================
#include "../../commands.hpp"
#include "../../utils/job/cli_render_utils.hpp"
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/software/software_render_session.hpp>
#include <chronon3d/backends/software/render_settings.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/text/text_run_shape.hpp>
#include <nlohmann/json.hpp>
#include <fmt/format.h>
#include <spdlog/spdlog.h>
#include <fstream>

#ifdef CHRONON3D_BUILD_DIAGNOSTICS
#include <chronon3d/text/text_visibility_audit.hpp>
#endif

namespace chronon3d {
namespace cli {

#ifndef CHRONON3D_BUILD_DIAGNOSTICS

// ── Non-diagnostic build: emit error JSON, return 1 ────────────────────────
int command_text_def_inspect(const CompositionRegistry& /*registry*/,
                              const TextDefInspectArgs& args) {
    nlohmann::json js;
    js["error"] = "diagnostics_disabled";
    js["composition"] = args.comp_id;
    js["status"] = "FAIL";
    if (!args.json_output.empty()) {
        std::ofstream out(args.json_output);
        if (out) out << js.dump(2) << "\n";
    } else {
        fmt::print("{}\n", js.dump(2));
    }
    spdlog::error("text-def-inspect: requires CHRONON3D_BUILD_DIAGNOSTICS=ON");
    return 1;
}

#else  // CHRONON3D_BUILD_DIAGNOSTICS

// ── Diagnostic build: real render + snapshot consumption + audit ───────────
int command_text_def_inspect(const CompositionRegistry& registry,
                              const TextDefInspectArgs& args) {
    // 1. Resolve composition
    auto resolved = resolve_composition(registry, args.comp_id);
    if (!resolved) {
        nlohmann::json js;
        js["error"] = "composition_not_found";
        js["composition"] = args.comp_id;
        js["status"] = "FAIL";
        if (!args.json_output.empty()) {
            std::ofstream out(args.json_output);
            if (out) out << js.dump(2) << "\n";
        } else {
            fmt::print("{}\n", js.dump(2));
        }
        spdlog::error("text-def-inspect: unknown composition '{}'", args.comp_id);
        return 1;
    }

    const auto& comp = *resolved.comp;

    // 2. Render the composition at the requested frame.  This populates
    //    `renderer.text_audit_snapshots()` via the graph builder's source
    //    pass.  RenderSettings are hardcoded (dev command, no user-exposed
    //    knobs needed) — the same pattern as `command_text_visibility.cpp`.
    RenderSettings settings;
    settings.use_modular_graph = true;
    settings.dirty.enabled = false;
    auto renderer = create_renderer(registry, settings);
    auto fb = renderer->render(comp, args.frame);
    if (!fb) {
        nlohmann::json js;
        js["error"] = "render_failed";
        js["composition"] = args.comp_id;
        js["frame"] = static_cast<int>(args.frame);
        js["status"] = "FAIL";
        if (!args.json_output.empty()) {
            std::ofstream out(args.json_output);
            if (out) out << js.dump(2) << "\n";
        } else {
            fmt::print("{}\n", js.dump(2));
        }
        spdlog::error("text-def-inspect: render produced no output");
        return 1;
    }

    // 3. Consume the post-render snapshots.  The graph builder populates
    //    this vector during the source pass for every TextRun node.  Each
    //    snapshot carries the resolved shape, world matrix, and the
    //    producer-supplied predicted/clip bboxes — the 4 inputs to
    //    `audit_text_visibility()` (the 5th, `effect_padding`, defaults
    //    to 0.0f per the spec).
    const auto& snapshots = renderer->text_audit_snapshots();

    // 4. Walk snapshots, audit each, emit per-snapshot JSON object.
    nlohmann::json js = nlohmann::json::array();
    for (const auto& snapshot : snapshots) {
        // Defensive: a snapshot may carry a null shape if the graph
        // builder captured a node whose text pipeline hadn't wired the
        // TextRunShape yet.  Skip those — they're not auditable.
        if (!snapshot.shape) continue;

        // 5-arg audit call (per spec — no alpha_region, no effect_padding).
        // The framebuffer is passed so the alpha-bbox invariant is
        // measured (the deferred `clip_contains_visible_ink` invariant).
        const auto audit = audit_text_visibility(
            *snapshot.shape,
            snapshot.world_matrix,
            snapshot.predicted_bbox,
            snapshot.clip_rect,
            fb.get()
        );

        // Map the status enum to the spec's string vocabulary.  INDETERMINATE
        // is the default-initialized sentinel and is not produced by
        // `audit_text_visibility()` (it always returns PASS or FAIL), but
        // we map it for forward-compat in case future enum values are added.
        const char* status_str = "INDETERMINATE";
        if (audit.status == TextVisibilityStatus::PASS) {
            status_str = "PASS";
        } else if (audit.status == TextVisibilityStatus::FAIL) {
            status_str = "FAIL";
        }

        // `alpha_bbox_empty` is a derived field: the alpha-bbox is the
        // measured ink region after rasterization.  An empty bbox (zero
        // width or zero height) means the compositor scanned no ink —
        // either the node is offscreen, fully clipped, or the text pipeline
        // produced no glyphs.  The 4-px minimum matches the alpha-bbox
        // scan tolerance used elsewhere in the visibility contract.
        const bool alpha_bbox_empty =
            (audit.rendered_alpha_bbox.size.x <= 0.0f) ||
            (audit.rendered_alpha_bbox.size.y <= 0.0f);

        nlohmann::json nj;
        nj["node"]                     = snapshot.name;
        nj["font_resolved"]            = audit.font_resolved;
        nj["glyph_count"]              = audit.glyph_count;
        nj["predicted_contains_world"] = audit.predicted_contains_world;
        nj["alpha_bbox_empty"]         = alpha_bbox_empty;
        nj["status"]                   = status_str;
        js.push_back(std::move(nj));
    }

    // 5. Output
    if (!args.json_output.empty()) {
        std::ofstream out(args.json_output);
        if (!out) {
            spdlog::error("text-def-inspect: cannot write to '{}'", args.json_output);
            return 1;
        }
        out << js.dump(2) << "\n";
        spdlog::info("text-def-inspect: JSON written to '{}' ({} nodes)",
                     args.json_output, js.size());
    } else {
        fmt::print("{}\n", js.dump(2));
    }

    spdlog::info("text-def-inspect: {} text nodes audited", js.size());
    return 0;
}

#endif  // CHRONON3D_BUILD_DIAGNOSTICS

} // namespace cli
} // namespace chronon3d
