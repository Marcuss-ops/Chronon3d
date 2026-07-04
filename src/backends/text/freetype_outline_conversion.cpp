// SPDX-License-Identifier: MIT
// ═══════════════════════════════════════════════════════════════════════════
// src/backends/text/freetype_outline_conversion.cpp
//
// M1.5#11 — implementation of the reusable FreeType outline → Blend2D
// BLPath converter.  Body lifted verbatim out of the legacy
// `text_rasterizer_render.cpp::FtGlyphPathBuilder::build_path`,
// with the FT_Face parameter promoted from a cached struct-member to
// the function argument (caller must own the face and its locking
// semantics — face I/O lives elsewhere per the anti-duplication
// invariant).
//
// What lives here:
//   * `convert_ft_outline_to_bl_path(FT_Face, placed, ox, oy)` —
//     per-glyph `FT_Load_Glyph(FT_LOAD_NO_BITMAP)` + `FT_Outline_Decompose`
//     → BLPath.  Y-axis inversion (font-up → BL-down) baked in.
//     Pixel→em scaling (1/64) baked in (FreeType 26.6 fixed-point).
//   * `build_outline_funcs()` static factory returning the FT_Outline_Funcs
//     table with `move_to` / `line_to` / `conic_to` / `cubic_to` lambdas
//     dispatching into a `DecomposeCtx` BLPath-build context.
//
// Anti-duplication invariant (M1.5#6 + M1.5#7 constraint):
//   * NO caching, NO face I/O — caller hands in a live FT_Face.
//   * Y-axis inversion is hardcoded — no caller-side knob (the legacy
//     caller used the same convention; preserving it preserves the
//     downstream `ctx.strokePath(...)` correctness that depends on
//     BL-space coordinate orientation).
//
// ═══════════════════════════════════════════════════════════════════════════

#include "freetype_outline_conversion.hpp"     // M1.5#11 — same-dir include

#include <blend2d.h>

#ifdef CHRONON3D_ENABLE_TEXT
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_OUTLINE_H
#endif

namespace chronon3d {

#ifdef CHRONON3D_ENABLE_TEXT

namespace {

// Pixel → float scale factor for FreeType's 26.6 fixed-point pixel space.
constexpr float  kScale        = 1.0f / 64.0f;
// Standard conic Bézier weight (true conic from a single control point).
constexpr double kConicWeight  = 1.0;

// Decompose callback context shared across move_to/line_to/conic_to/cubic_to.
struct DecomposeCtx {
    BLPath* path;
    double  off_x;
    double  off_y;
    bool    first_contour{true};
};

/// Build the FT_Outline_Funcs table bound to our BLPath-construction
/// `DecomposeCtx` consumer.  Constructed ONCE per `convert_ft_outline_to_bl_path`
/// call (held on the caller's stack for the duration of the dispatch loop).
FT_Outline_Funcs build_outline_funcs() {
    FT_Outline_Funcs funcs{};
    funcs.move_to = [](const FT_Vector* to, void* user) -> int {
        auto* ctx = static_cast<DecomposeCtx*>(user);
        if (!ctx->first_contour) ctx->path->close();
        ctx->first_contour = false;
        ctx->path->moveTo(
            ctx->off_x + static_cast<double>(to->x) * kScale,
            ctx->off_y - static_cast<double>(to->y) * kScale);
        return 0;
    };
    funcs.line_to = [](const FT_Vector* to, void* user) -> int {
        auto* ctx = static_cast<DecomposeCtx*>(user);
        ctx->path->lineTo(
            ctx->off_x + static_cast<double>(to->x) * kScale,
            ctx->off_y - static_cast<double>(to->y) * kScale);
        return 0;
    };
    funcs.conic_to = [](const FT_Vector* control, const FT_Vector* to, void* user) -> int {
        auto* ctx = static_cast<DecomposeCtx*>(user);
        ctx->path->conicTo(
            ctx->off_x + static_cast<double>(control->x) * kScale,
            ctx->off_y - static_cast<double>(control->y) * kScale,
            ctx->off_x + static_cast<double>(to->x) * kScale,
            ctx->off_y - static_cast<double>(to->y) * kScale,
            kConicWeight);
        return 0;
    };
    funcs.cubic_to = [](const FT_Vector* c1, const FT_Vector* c2, const FT_Vector* to, void* user) -> int {
        auto* ctx = static_cast<DecomposeCtx*>(user);
        ctx->path->cubicTo(
            ctx->off_x + static_cast<double>(c1->x) * kScale,
            ctx->off_y - static_cast<double>(c1->y) * kScale,
            ctx->off_x + static_cast<double>(c2->x) * kScale,
            ctx->off_y - static_cast<double>(c2->y) * kScale,
            ctx->off_x + static_cast<double>(to->x) * kScale,
            ctx->off_y - static_cast<double>(to->y) * kScale);
        return 0;
    };
    funcs.delta = 0;
    funcs.shift = 0;
    return funcs;
}

} // anonymous namespace

// ── Public surface ────────────────────────────────────────────────────────

BLPath convert_ft_outline_to_bl_path(
    FT_Face                   ft_face,
    const PlacedGlyphRun&     placed,
    float                     origin_x,
    float                     origin_y
) {
    BLPath path;
    if (!ft_face || placed.glyphs.empty()) return path;

    const FT_Outline_Funcs funcs = build_outline_funcs();

    for (const auto& pg : placed.glyphs) {
        if (FT_Load_Glyph(ft_face, pg.glyph_id, FT_LOAD_NO_BITMAP) != 0) continue;
        if (ft_face->glyph->format != FT_GLYPH_FORMAT_OUTLINE) continue;

        const float glyph_ox = origin_x + pg.x;
        const float glyph_oy = origin_y + pg.y;

        DecomposeCtx ctx{&path,
                          static_cast<double>(glyph_ox),
                          static_cast<double>(glyph_oy),
                          true};
        FT_Outline_Decompose(&ft_face->glyph->outline, &funcs, &ctx);
        if (!ctx.first_contour) path.close();
    }
    return path;
}

#endif // CHRONON3D_ENABLE_TEXT

} // namespace chronon3d
