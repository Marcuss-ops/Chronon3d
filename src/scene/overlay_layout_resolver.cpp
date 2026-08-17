// ─── overlay_layout_resolver.cpp — V1 overlay layout resolver ──────────────
//
// Implements the greedy, deterministic layout algorithm declared in
// `include/chronon3d/layout/overlay_layout_resolver.hpp`.  Each request is
// placed by trying its preferred intent and then its fallback intents; a
// candidate is accepted only when the resulting box fits inside the safe
// area and does not collide with any already-occupied region.

#include <chronon3d/layout/overlay_layout_resolver.hpp>

#include <algorithm>
#include <numeric>

namespace chronon3d::layout {

namespace {

// Pin point for a 9-point anchor, inset from the canvas edges by the safe
// margins (per-axis, so aspect-ratio-aware safe areas work).  Mirrors
// `anchor_position` in layout_solver.hpp but with independent x/y insets.
Vec2 pin_for(Anchor anchor, float width, float height,
             float inset_x, float inset_y) {
    switch (anchor) {
        case Anchor::TopLeft:      return {inset_x, inset_y};
        case Anchor::TopCenter:    return {width * 0.5f, inset_y};
        case Anchor::TopRight:     return {width - inset_x, inset_y};
        case Anchor::MiddleLeft:   return {inset_x, height * 0.5f};
        case Anchor::Center:       return {width * 0.5f, height * 0.5f};
        case Anchor::MiddleRight:  return {width - inset_x, height * 0.5f};
        case Anchor::BottomLeft:   return {inset_x, height - inset_y};
        case Anchor::BottomCenter: return {width * 0.5f, height - inset_y};
        case Anchor::BottomRight:  return {width - inset_x, height - inset_y};
    }
    return {width * 0.5f, height * 0.5f};
}

// Box top-left given the anchor pin point and the content bounds.
Vec2 top_left_for(Anchor anchor, Vec2 pin, float box_w, float box_h) {
    switch (anchor) {
        case Anchor::TopLeft:      return pin;
        case Anchor::TopCenter:    return {pin.x - box_w * 0.5f, pin.y};
        case Anchor::TopRight:     return {pin.x - box_w, pin.y};
        case Anchor::MiddleLeft:   return {pin.x, pin.y - box_h * 0.5f};
        case Anchor::Center:       return {pin.x - box_w * 0.5f, pin.y - box_h * 0.5f};
        case Anchor::MiddleRight:  return {pin.x - box_w, pin.y - box_h * 0.5f};
        case Anchor::BottomLeft:   return {pin.x, pin.y - box_h};
        case Anchor::BottomCenter: return {pin.x - box_w * 0.5f, pin.y - box_h};
        case Anchor::BottomRight:  return {pin.x - box_w, pin.y - box_h};
    }
    return {pin.x - box_w * 0.5f, pin.y - box_h * 0.5f};
}

bool overlaps(const OverlayRegion& a, const OverlayRegion& b) {
    const bool temporal_known = a.start_frame >= 0 && a.end_frame > a.start_frame &&
                                b.start_frame >= 0 && b.end_frame > b.start_frame;
    if (temporal_known && (a.end_frame <= b.start_frame || b.end_frame <= a.start_frame))
        return false;
    const bool separated_x = a.x + a.width <= b.x || b.x + b.width <= a.x;
    const bool separated_y = a.y + a.height <= b.y || b.y + b.height <= a.y;
    return !(separated_x || separated_y);
}

bool inside_safe_area(const OverlayRegion& box, float canvas_w, float canvas_h,
                      float inset_x, float inset_y) {
    return box.x >= inset_x && box.y >= inset_y &&
           box.x + box.width <= canvas_w - inset_x &&
           box.y + box.height <= canvas_h - inset_y;
}

} // namespace

Anchor intent_anchor(std::string_view intent) noexcept {
    if (intent == "center")       return Anchor::Center;
    if (intent == "safe_area")    return Anchor::Center;
    if (intent == "lower_third")  return Anchor::BottomCenter;
    if (intent == "lower_left")   return Anchor::BottomLeft;
    if (intent == "lower_right")  return Anchor::BottomRight;
    if (intent == "top_left")     return Anchor::TopLeft;
    if (intent == "top_right")    return Anchor::TopRight;
    if (intent == "bottom_left")  return Anchor::BottomLeft;
    if (intent == "bottom_right") return Anchor::BottomRight;
    if (intent == "image_left")   return Anchor::MiddleLeft;
    if (intent == "image_right")  return Anchor::MiddleRight;
    return Anchor::Center;
}

std::vector<ResolvedOverlayLayout> OverlayLayoutResolver::solve(
    float canvas_width,
    float canvas_height,
    std::vector<OverlayLayoutRequest> requests,
    std::vector<OverlayRegion> occupied) const {
    // Deterministic placement order: priority desc, then original order.
    std::vector<std::size_t> order(requests.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        return requests[a].priority > requests[b].priority;
    });

    std::vector<ResolvedOverlayLayout> results(requests.size());
    for (const std::size_t index : order) {
        const auto& request = requests[index];
        const float inset_x = request.safe_margin * canvas_width;
        const float inset_y = request.safe_margin * canvas_height;

        std::vector<std::string> candidates{request.intent};
        candidates.insert(candidates.end(), request.fallback_intents.begin(),
                          request.fallback_intents.end());

        bool placed = false;
        ResolvedOverlayLayout out;
        out.id = request.id;

        for (const auto& candidate : candidates) {
            const Anchor anchor = intent_anchor(candidate);
            const Vec2 pin = pin_for(anchor, canvas_width, canvas_height,
                                     inset_x, inset_y);
            const Vec2 tl = top_left_for(anchor, pin, request.width, request.height);
            const OverlayRegion box{tl.x, tl.y, request.width, request.height,
                                    request.start_frame, request.end_frame};

            if (inside_safe_area(box, canvas_width, canvas_height, inset_x, inset_y) &&
                std::none_of(occupied.begin(), occupied.end(),
                             [&](const OverlayRegion& region) {
                                 return overlaps(box, region);
                             })) {
                out.intent = candidate;
                out.x = tl.x;
                out.y = tl.y;
                out.valid = true;
                occupied.push_back(box);
                placed = true;
                break;
            }
        }

        if (!placed) {
            // Preflight failure: keep the preferred-intent position (so the
            // caller has a renderable fallback) but flag it loudly.
            const Anchor anchor = intent_anchor(request.intent);
            const Vec2 pin = pin_for(anchor, canvas_width, canvas_height,
                                     inset_x, inset_y);
            const Vec2 tl = top_left_for(anchor, pin, request.width, request.height);
            out.intent = request.intent;
            out.x = tl.x;
            out.y = tl.y;
            out.valid = false;
            out.warning = "no collision-free, safe-area anchor for overlay '" +
                          request.id + "'";
        }
        results[index] = std::move(out);
    }
    return results;
}

} // namespace chronon3d::layout
