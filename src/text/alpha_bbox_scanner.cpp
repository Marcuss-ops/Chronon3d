// SPDX-License-Identifier: MIT

// ═══════════════════════════════════════════════════════════════════════════
// alpha_bbox_scanner.cpp — Step 9 §A implementation
//
// Implementation of the reusable alpha-bbox scanner. UNGATED: the file
// always compiles, no `#ifdef CHRONON3D_BUILD_DIAGNOSTICS` guard. See
// the header docstring for the rationale (pure pixel walk, no side
// effects, no logging, no allocations).
//
// Anti-duplication: this is the canonical implementation. The legacy
// TU-local `scan_alpha_bbox()` in `text_visibility_audit.cpp` has been
// removed; that file now calls `alpha_bbox_scan()` from this TU.
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/text/alpha_bbox_scanner.hpp>

namespace chronon3d {

Rect alpha_bbox_scan(const Framebuffer& fb) noexcept {
    const int fb_w = static_cast<int>(fb.width());
    const int fb_h = static_cast<int>(fb.height());
    if (fb_w <= 0 || fb_h <= 0) return Rect{};

    int alpha_x0 = fb_w, alpha_y0 = fb_h;
    int alpha_x1 = -1, alpha_y1 = -1;

    for (int y = 0; y < fb_h; ++y) {
        const Color* row = fb.pixels_row(y);
        bool row_has_ink = false;
        for (int x = 0; x < fb_w; ++x) {
            if (row[x].a > kAlphaBBoxScanThreshold) {
                row_has_ink = true;
                if (x < alpha_x0) alpha_x0 = x;
                if (x > alpha_x1) alpha_x1 = x;
            }
        }
        if (row_has_ink) {
            if (y < alpha_y0) alpha_y0 = y;
            alpha_y1 = y;
        }
    }

    if (alpha_x0 > alpha_x1 || alpha_y0 > alpha_y1) {
        return Rect{};  // no visible ink found
    }
    return Rect{
        {static_cast<float>(alpha_x0), static_cast<float>(alpha_y0)},
        {static_cast<float>(alpha_x1 - alpha_x0 + 1),
         static_cast<float>(alpha_y1 - alpha_y0 + 1)}
    };
}

} // namespace chronon3d
