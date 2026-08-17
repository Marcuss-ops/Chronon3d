// ==============================================================================
// include/chronon3d/layout/overlay_layout_resolver.hpp
//
// V1 overlay layout resolver.  Turns overlay LAYOUT INTENTS into concrete
// top-left positions on a canvas, honouring (in order, per the ADR-029 V1
// contract):
//
//   1. preferred anchor
//   2. calculate content bounds
//   3. check safe area
//   4. check collision against occupied regions
//   5. try fallback anchors
//   6. preflight warning if no candidate fits
//
// It is deliberately greedy and deterministic (priority desc, then original
// order) so the same plan resolves to the same layout on every worker.  It
// does NOT render; it only computes positions — the compiler consumes the
// resolved values.  A "region" is an axis-aligned rect in pixels (top-left +
// extent); the resolver records each accepted box so later layers avoid it.
// ==============================================================================

#pragma once

#include <chronon3d/layout/layout_rules.hpp>  // Anchor, Vec2

#include <string>
#include <string_view>
#include <vector>

namespace chronon3d::layout {

// Axis-aligned occupied region in canvas pixels (top-left + extent).
struct OverlayRegion {
    float x{0.0f};
    float y{0.0f};
    float width{0.0f};
    float height{0.0f};
};

// One overlay layer to place.
struct OverlayLayoutRequest {
    std::string id;
    std::string intent;                       // preferred layout intent.
    std::vector<std::string> fallback_intents; // preferred → fallback order.
    float width{0.0f};                        // content bounds (pixels).
    float height{0.0f};
    float safe_margin{0.06f};                 // fraction of canvas per side.
    float priority{0.0f};                     // higher resolves first.
};

// Resolved placement for one request.
struct ResolvedOverlayLayout {
    std::string id;
    std::string intent;     // the intent actually used (fallback if preferred failed).
    float x{0.0f};          // resolved top-left, canvas pixels.
    float y{0.0f};
    bool valid{true};       // false when no candidate fit (preflight failure).
    std::string warning;    // set when !valid.
};

// Maps a closed layout intent (the render-plan `anchor.type` enum spelling)
// onto the 9-point `Anchor` pin grid.  Unknown intents resolve to Center.
[[nodiscard]] Anchor intent_anchor(std::string_view intent) noexcept;

// Greedy V1 resolver.  Requests are placed in priority order (descending,
// stable); each accepted box is appended to the occupied set so subsequent
// layers avoid it.  Returns one ResolvedOverlayLayout per request, in the
// original request order.
class OverlayLayoutResolver {
public:
    [[nodiscard]] std::vector<ResolvedOverlayLayout> solve(
        float canvas_width,
        float canvas_height,
        std::vector<OverlayLayoutRequest> requests,
        std::vector<OverlayRegion> occupied = {}) const;
};

} // namespace chronon3d::layout
