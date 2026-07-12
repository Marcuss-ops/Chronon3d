// SPDX-License-Identifier: MIT
#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// text_visibility_reporting.hpp — Step 9 §A extracted reporting layer
//
// Hosts the SIDE-EFFECTING half of the text-visibility contract:
//   - `verify_text_visibility()` — post-audit `spdlog::warn` emitters
//     for the 6 F1.E invariants (with per-scope warn-once dedup).
//   - `WarnOnceDeduper`           — session-scoped dedup state.
//   - `TextWarningKind` + `TextWarningKey` + `std::hash<>` — the
//     diagnostic taxonomy + dedup key.
//
// Extracted from `text_visibility_audit.{hpp,cpp}` so the audit file is
// now a pure-data module (no spdlog dependency, no warn-once state).
// This split is the canonical decomposition for the §A "diagnostica" /
// "log, JSON, telemetry" axis per the user spec.
//
// GATING: entire content is wrapped in `#ifdef CHRONON3D_BUILD_DIAGNOSTICS`.
// When diagnostics=OFF, this header is a no-op (zero symbols, zero
// textual ABI surface) — same Cat-3 zero-surface invariant as the audit
// header. Consumers that need the pure `audit_text_visibility()` + the
// reusable `alpha_bbox_scan()` do NOT need this header.
//
// Anti-duplication: the entire reporting layer lives in a single TU
// (`text_visibility_reporting.cpp`). Cross-TU consumers (CLI inspect-text,
// CLI command_text_def_inspect, node runners) include this header and
// get the canonical deduper + verify function.
// ═══════════════════════════════════════════════════════════════════════════

#ifdef CHRONON3D_BUILD_DIAGNOSTICS

#include <cstddef>
#include <cstdint>
#include <functional>                           // std::hash for TextWarningKey
#include <string_view>                          // stable node-name hash
#include <unordered_set>                        // WarnOnceDeduper backing store

#include <chronon3d/media/media_placement.hpp>   // chronon3d::Rect
#include <chronon3d/math/glm_types.hpp>          // chronon3d::Mat4

namespace chronon3d {

class TextRunShape;     // forward decl — full definition lives in
                        // `<chronon3d/text/text_run_shape.hpp>`. The reporting
                        // function only reads the immutable struct + the
                        // populated `TextVisibilityAudit`, so a forward
                        // declaration is sufficient at compile time. Callers
                        // that trigger the actual function include the full
                        // header.
class Framebuffer;      // forward decl — `<chronon3d/core/memory/framebuffer.hpp>`.

/// §12 FU09 / TICKET-SIMPLICITY-VISIBILITY-CONTRACT dedup taxonomy.
/// One value per diagnostic invariant emitted by `verify_text_visibility()`.
/// Used together with `TextWarningKey::node_id` to dedup warnings per
/// session — replacing the prior 6 `static bool` one-shot pattern (which
/// had process-wide state, hidden globals, and data races in parallel
/// render paths).
///
/// All values are stable; new entries may be appended at the end of the
/// range without breaking the F1.E regression lock (existing callers
/// never construct TextWarningKey directly — they pass through the
/// `make_warning_key()` canonical helper in text_visibility_reporting.cpp).
enum class TextWarningKind : u8 {
    FontUnresolved    = 1,  // §1.E asset-1: shape.engine == nullptr
    ShapingFailed     = 2,  // §1.E asset-2: layout.placed.glyphs.empty()
    BboxNonFinite     = 3,  // §1.E math-3:  any of 5 bboxes has NaN/Inf
    PredictedTooSmall = 4,  // §1.E math-4:  predicted_bbox ⊉ world_ink_bbox
    ClipDropsInk      = 5,  // §1.E math-5:  clip_rect ⊉ rendered_alpha_bbox
    NoInkRendered     = 6,  // §1.E assset-6: rendered alpha_bbox empty
};

/// `TextWarningKey` — composite key for the warn-once deduper. The
/// `(node_id, kind)` pair uniquely identifies one diagnostic stream per
/// node per invariant. Stable hash via `std::hash<TextWarningKey>`
/// specialization below (Cat-3 anti-duplication: we do NOT introduce a
/// generic string-key deduper; the enum+int pair is the minimal surface
/// for the audit contract).
struct TextWarningKey {
    int             node_id;  // -1 for unnamed nodes; otherwise stable hash
    TextWarningKind kind;
    bool operator==(const TextWarningKey& other) const noexcept {
        return node_id == other.node_id && kind == other.kind;
    }
};

} // namespace chronon3d

namespace std {
template<>
struct hash<chronon3d::TextWarningKey> {
    std::size_t operator()(const chronon3d::TextWarningKey& k) const noexcept {
        // XOR-shift combine: node_id dominates because each node has its own
        // diagnostic stream; kind disambiguates the 6 invariants within a node.
        return static_cast<std::size_t>(k.node_id)
             ^ (static_cast<std::size_t>(k.kind) << 1);
    }
};
} // namespace std

namespace chronon3d {

/// `WarnOnceDeduper` — session-scoped warn-once deduplicator. Caller
/// instantiates one per auditable scope (per-render-graph, per-composition
/// evaluation, per-CLI-invocation) and passes it to `verify_text_visibility()`.
/// Each `(node_id, kind)` key is emitted at most once per deduper lifetime.
///
/// AGENTS.md Cat-5 compliance: NOT a singleton. NOT a global. Class-level
/// state is per-instance, plain `std::unordered_set` backing store, zero
/// hidden state outside the explicit instance.
///
/// Replace site: the prior 6 `static bool wN = false;` one-shots which
/// had 3 known §honesty defects (process-wide hidden state, data race
/// on parallel render, first-error masks later nodes). All three are
/// closed by the per-instance dedup model: each render call gets its
/// own deduper OR shares one via a contract-documented owner (e.g.
/// `static WarnOnceDeduper s_deduper;` at TU scope, or a class member).
class WarnOnceDeduper {
public:
    /// Returns `true` iff this is the FIRST time `key` is observed by this
    /// deduper instance. After `true` is returned once for a given key,
    /// ALL subsequent calls with the same key return `false`. Caller
    /// pattern: `if (deduper.mark_warned(key)) spdlog::warn(...);`
    bool mark_warned(const TextWarningKey& key) {
        return warned_.insert(key).second;
    }
    void  clear()                       noexcept { warned_.clear(); }
    std::size_t size()            const noexcept { return warned_.size(); }
    bool  empty()                const noexcept { return warned_.empty(); }

private:
    std::unordered_set<TextWarningKey> warned_;
};

// Forward-declare the audit struct (full definition in text_visibility_audit.hpp).
// `verify_text_visibility()` returns this by value.
struct TextVisibilityAudit;

/// `verify_text_visibility()` — F1.E post-render visibility contract.
///
/// Calls `audit_text_visibility()` and emits structured `spdlog::warn`
/// diagnostics for each of the 6 invariants that fail:
///
///   1. font_resolved        — `shape.engine != nullptr`
///   2. shaping_succeeded    — `shape.layout.placed.glyphs.size() > 0`
///   3. finite               — all 5 bboxes have finite coordinates
///   4. predicted_contains_world — predicted_bbox ⊇ world_ink_bbox
///   5. clip_contains_visible_ink — expand(clip_rect, kTextAuditBBoxTolerance) ⊇ rendered_alpha_bbox
///   6. rendered_alpha_bbox non-empty — actual ink pixels detected
///
/// Returns the populated `TextVisibilityAudit` so callers can also
/// inspect the result programmatically.
///
/// DIAGNOSTIC DEDUP: warnings are emitted at most once per
/// `(node_id, TextWarningKind)` pair per `deduper` lifetime. The deduper
/// is owned by the caller — pass an instance shared across the auditable
/// scope (e.g. per-render-graph or per-composition evaluation). The prior
/// `static bool` pattern (process-wide state, data-race on parallel
/// render, first-error masking) is removed; the deduper instance model
/// closes all three defects.
///
/// @param shape             The TextRunShape with layout, engine, glyphs
/// @param world_matrix      Layer → canvas transform matrix
/// @param predicted_bbox    Producer-supplied predicted bbox (canvas-level)
/// @param clip_rect         Compositor clip rect (canvas-level)
/// @param rendered_output   Optional framebuffer for alpha-bbox scan
/// @param node_name         Diagnostic label for log messages
/// @param deduper           Per-scope warn-once deduper (replaces static bool)
/// @return Populated TextVisibilityAudit struct
[[nodiscard]] TextVisibilityAudit verify_text_visibility(
    const TextRunShape& shape,
    const Mat4&         world_matrix,
    const Rect&         predicted_bbox,
    const Rect&         clip_rect,
    const Framebuffer*  rendered_output,
    const char*         node_name,
    WarnOnceDeduper&    deduper,
    float               effect_padding = 0.0f
);

} // namespace chronon3d

#endif // CHRONON3D_BUILD_DIAGNOSTICS
