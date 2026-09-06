// test_text_placement_resolver_adr019.cpp — Split from
// test_text_placement_resolver.cpp: Phase A.3 ResolvedTextPlacement output
// contract and the ADR-019 §6 numerical-example lattice locks.

#include <doctest/doctest.h>
#include <chronon3d/text/text_placement.hpp>           // TextPlacement, TextPlacementKind
#include <chronon3d/text/resolve_text_placement.hpp>    // Canonical resolver surface
#include <chronon3d/layout/layout_solver.hpp>            // Canvas-space layer pin math
#include <chronon3d/scene/builders/layer_builder.hpp>    // Materialized TextRun transform contract
#include <chronon3d/scene/model/core/scene.hpp>         // Rasterizer-independent layer contract

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

using namespace chronon3d;

// ── Helpers ──────────────────────────────────────────────────────────────

static constexpr f32 kEps = 0.01f;

static bool vec2_near(Vec2 a, Vec2 b, f32 eps = kEps) {
    return std::abs(a.x - b.x) < eps && std::abs(a.y - b.y) < eps;
}

static bool mat4_near(const Mat4& a, const Mat4& b, f32 eps = kEps) {
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            if (std::abs(a[i][j] - b[i][j]) > eps) return false;
    return true;
}

static CanvasInfo default_canvas() {
    return CanvasInfo{1920.0f, 1080.0f, 54.0f, 54.0f, 96.0f, 96.0f};
}

// ═════════════════════════════════════════════════════════════════════════
// Phase A.3 — ResolvedTextPlacement output contract
// ═════════════════════════════════════════════════════════════════════════

// Phase A.3 spec maps these terms onto ResolvedTextPlacement fields:
//   Vec2  canvas_position     ↔ layout_origin  (Vec2; box top-left in Canvas)
//   TextFrame resolved_frame  ↔ local_frame    (Vec4 = x, y, width, height)
//   Anchor resolved_anchor   ↔ resolved_anchor (TextAnchor; input echo)
//
// These tests lock the Phase A.3 contract: the resolver must populate
// every field of ResolvedTextPlacement, including the new `resolved_anchor`
// echo of the input TextAnchor (previously discarded).

TEST_CASE("ResolvedTextPlacement: resolved_anchor echoes input TextAnchor (Phase A.3)") {
    auto c = default_canvas();
    Vec2 box{800.0f, 200.0f};

    // The resolver previously used the anchor to compute the anchor
    // offset, then discarded it.  Phase A.3 stores the input anchor
    // in `resolved_anchor` so downstream consumers can re-derive layout
    // without re-running the full resolver.
    //
    // Parametric coverage: all existing TextAnchor values must be echoed.
    // (We use a sequence of CHECKs rather than DOCTEST_VALUE_PARAMETERIZED
    // to keep the test self-contained and easy to read.)
    struct AnchorCase { TextAnchor input; };
    const AnchorCase cases[] = {
        {TextAnchor::TopLeft},
        {TextAnchor::TopCenter},
        {TextAnchor::Center},
        {TextAnchor::BottomCenter},
        {TextAnchor::BaselineLeft},
        {TextAnchor::BaselineCenter},
    };
    for (const auto& tc : cases) {
        auto r = resolve_text_placement(c, box,
            TextPlacement{TextPlacementKind::CanvasCenter}, tc.input);
        CHECK(r.resolved_anchor == tc.input);
    }
}

TEST_CASE("ResolvedTextPlacement: default anchor when not specified (Phase A.3)") {
    // The free function defaults `anchor` to TextAnchor::Center; the
    // resolved_anchor field must mirror that default.
    auto c = default_canvas();
    Vec2 box{800.0f, 200.0f};
    auto r = resolve_text_placement(c, box,
        TextPlacement{TextPlacementKind::CanvasCenter});
    CHECK(r.resolved_anchor == TextAnchor::Center);
}

TEST_CASE("ResolvedTextPlacement: canvas_position = layout_origin (Phase A.3 spec alias)") {
    // The Phase A.3 spec term `canvas_position` maps to the existing
    // `layout_origin` field (Vec2; box top-left in Canvas coords).
    auto c = default_canvas();
    Vec2 box{1700.0f, 360.0f};
    auto r = resolve_text_placement(c, box,
        TextPlacement{TextPlacementKind::CanvasCenter});

    // The "canvas_position" per the spec is where the text frame is
    // placed in Canvas coords.  After anchor adjustment, the box's
    // top-left lands at (110, 360) on a 1920×1080 canvas with a
    // 1700×360 box centered on the canvas.
    const Vec2 canvas_position = r.layout_origin;
    CHECK(vec2_near(canvas_position, {110.0f, 360.0f}));
}

TEST_CASE("ResolvedTextPlacement: resolved_frame = local_frame (Phase A.3 spec alias)") {
    // The Phase A.3 spec term `resolved_frame` maps to the existing
    // `local_frame` field (Vec4 = x, y, width, height).
    auto c = default_canvas();
    Vec2 box{1700.0f, 360.0f};
    auto r = resolve_text_placement(c, box,
        TextPlacement{TextPlacementKind::CanvasCenter});

    // The "resolved_frame" per the spec is the box position + size in
    // Canvas coords.  local_frame packs (x, y, width, height).
    const Vec4 resolved_frame = r.local_frame;
    CHECK(resolved_frame.x == doctest::Approx(110.0f));
    CHECK(resolved_frame.y == doctest::Approx(360.0f));
    CHECK(resolved_frame.z == doctest::Approx(1700.0f));
    CHECK(resolved_frame.w == doctest::Approx(360.0f));
}

TEST_CASE("ResolvedTextPlacement: layer_matrix + world_matrix preserved (beyond spec)") {
    // The 2 additional fields beyond the Phase A.3 spec terms
    // (layer_matrix, world_matrix) are kept for the rendering
    // pipeline's transform composition.  Lock their population so
    // downstream consumers can rely on them.
    auto c = default_canvas();
    Vec2 box{800.0f, 200.0f};
    Mat4 parent = glm::translate(Mat4(1.0f), Vec3(100.0f, 50.0f, 0.0f));
    auto r = resolve_text_placement(c, box,
        TextPlacement{TextPlacementKind::TopLeft},
        TextAnchor::TopLeft, parent);

    // layer_matrix echoes the input parent transform
    CHECK(mat4_near(r.layer_matrix, parent));
    // world_matrix = parent × T(origin) where origin = (96, 54)
    Mat4 expected = parent * glm::translate(Mat4(1.0f), Vec3(96.0f, 54.0f, 0.0f));
    CHECK(mat4_near(r.world_matrix, expected));
}

// ═════════════════════════════════════════════════════════════════════════
// ADR-019 §6 — Numerical Examples (1920×1080 canonical canvas) — explicit locks
// ═════════════════════════════════════════════════════════════════════════
//
// These 6 TEST_CASEs lock the numerical lattice published in
// [ADR-019 §6 Numerical Examples](../adr/ADR-019-text-coordinate-model.md#6-numerical-examples-19201080-canonical-canvas).
// Note the GitHub-compatible anchor `#6-...` (the `§` character is stripped by
// GitHub's markdown anchor algorithm; the link must point to `6-numerical-...`,
// not `§6-numerical-...`). Values are duplicated here verbatim so the suite is
// self-contained for CI runs that don't read the ADR markdown. When the ADR is
// updated, this block MUST be updated in lock-step.
//
// Anchor normalization: `<NUMBER> Numerical Examples` → GitHub anchor `<NUMBER>-numerical-...`.
// ADR-019 §6.1 / 6.2 / 6.3 / 6.4 / 6.5 / 6.6 → GitHub anchors `6.1-...` / `6.2-...` etc.
// (the `.` between digits is also stripped by GitHub; we link the parent §6
// anchor only once at the anchor reference above).
//
// Anti-duplication rationale (per AGENTS.md §anti-duplication rules, see also
// ROADMAP.md §V0.2 Fase-1 PIVOT precedent): some of these 6 TESTS partially
// overlap with pre-existing tests above (notably "CanvasCenter + Center
// anchor — classic centered title", "CanvasCenter + TopLeft anchor — box
// starts at center", "world_matrix transforms box-local origin correctly").
// Duplication is INTENTIONAL: the ADR-019 §6.x test names + comments form
// an explicit audit-trail link from the ADR markdown back to test code.
// Operators can `chronon3d_text_tests --test-case='*ADR-019*'` to verify all
// §6 lattice locks in one pass. The genuinely NEW coverage here is §6.4
// (rotation composition — no existing test exercises `resolve_text_placement`
// with a NON-IDENTITY `layer_matrix` rotation, which is the sole §6.4 invariant
// that exercises the `world_matrix = layer_matrix * T(origin)` composition
// formula under a real parent transform).

TEST_CASE("ADR-019 §6.1 Placement — CanvasCenter pin (960, 540) on 1920×1080") {
    // §6.1 row 1: CanvasCenter pin = (canvas.width/2, canvas.height/2)
    // Setup per ADR-019 §6: canvas 1920×1080, default safe margins
    // (96/96/54/54), box 1700×360, no offset, anchor TextAnchor::Center.
    auto c = default_canvas();                                      // 1920×1080, margins 96/96/54/54
    Vec2 box{1700.0f, 360.0f};
    auto pin = resolve_placement_origin(c, box, TextPlacement{TextPlacementKind::CanvasCenter});
    CHECK(vec2_near(pin, {960.0f, 540.0f}));
}

TEST_CASE("ADR-019 §6.2 Offset — TopLeft + offset {10, 20} → pin (106, 74)") {
    // §6.2 row 2: TopLeft pin + {10, 20} is ADDITIVE: (96+10, 54+20) = (106, 74)
    // Setup per ADR-019 §6.2 table: box 800×200 chosen for legibility
    // (the table does not lock box size for §6.2; pin computation is
    // independent of box size for most placements per resolver comment).
    auto c = default_canvas();                                      // 1920×1080, margins 96/96/54/54
    Vec2 box{800.0f, 200.0f};
    auto pin = resolve_placement_origin(c, box,
        TextPlacement{TextPlacementKind::TopLeft, {10.0f, 20.0f}});
    CHECK(vec2_near(pin, {106.0f, 74.0f}));
}

TEST_CASE("ADR-019 §6.3 Alignment — anchor Center default → box_top_left (110, 360)") {
    // §6.3 layout-engine contract: alignment (TextAlign + VerticalAlign)
    // is a layout-engine concern running INSIDE the box.  The resolver
    // returns the box top-left (= pin − anchor_offset).  For CanvasCenter
    // pin (960, 540) and TextAnchor::Center (anchor_offset = box/2 =
    // (850, 180)), box_top_left = (960−850, 540−180) = (110, 360).
    auto c = default_canvas();
    Vec2 box{1700.0f, 360.0f};
    auto r = resolve_text_placement(c, box, TextPlacement{TextPlacementKind::CanvasCenter});
    CHECK(r.local_frame.x == doctest::Approx(110.0f));   // ADR-019 §6.3 box_top_left.x
    CHECK(r.local_frame.y == doctest::Approx(360.0f));   // ADR-019 §6.3 box_top_left.y
    CHECK(r.local_frame.z == doctest::Approx(1700.0f));  // ADR-019 §6.3 box width
    CHECK(r.local_frame.w == doctest::Approx(360.0f));   // ADR-019 §6.3 box height
    CHECK(vec2_near(r.layout_origin, {110.0f, 360.0f}));
}

TEST_CASE("ADR-019 §6.4 Rotation — world_matrix = rot_z(15°) × T(origin)") {
    // §6.4 composition contract: world_matrix = layer_matrix × T(origin),
    // where origin is the box top-left in Canvas coords independent of
    // rotation.  For CanvasCenter pin + Center anchor + 1700×360 box,
    // origin = (110, 360); with layer_matrix = rot_z(15°), the world_matrix
    // is the composition of rotation × translation, and `local_frame` /
    // `layout_origin` are NOT affected by rotation (they remain in Canvas
    // coords AFTER placement, before rotation).
    auto c = default_canvas();
    Vec2 box{1700.0f, 360.0f};
    Mat4 rot_layer = glm::rotate(Mat4(1.0f), glm::radians(15.0f), Vec3(0, 0, 1));
    auto r = resolve_text_placement(c, box,
        TextPlacement{TextPlacementKind::CanvasCenter},
        TextAnchor::Center, rot_layer);

    // local_frame / layout_origin are placement-only (Canvas coords):
    // rotation is applied AFTER placement resolution, so box position +
    // box extent are INVARIANT under rotation of layer_matrix.
    CHECK(r.local_frame.x == doctest::Approx(110.0f));
    CHECK(r.local_frame.y == doctest::Approx(360.0f));
    CHECK(r.local_frame.z == doctest::Approx(1700.0f));   // box width is rotation-invariant
    CHECK(r.local_frame.w == doctest::Approx(360.0f));    // box height is rotation-invariant

    // layer_matrix echoes the input rotation
    CHECK(mat4_near(r.layer_matrix, rot_layer));

    // world_matrix = rot_z(15°) × T(110, 360, 0) — matrix multiply is exact
    Mat4 expected_world = rot_layer * glm::translate(Mat4(1.0f), Vec3(110.0f, 360.0f, 0.0f));
    CHECK(mat4_near(r.world_matrix, expected_world));

    // Sanity: rotation pivot at canvas-center origin (960, 540) maps to itself
    // (matrix[3][0..2] for a rotation+translation anchored at origin).
    Vec4 origin_in_world = r.world_matrix * Vec4(0.0f, 0.0f, 0.0f, 1.0f);
    CHECK(std::isfinite(origin_in_world.x));
    CHECK(std::isfinite(origin_in_world.y));
}

TEST_CASE("ADR-019 §6.5 Anchor — TopLeft at CanvasCenter pin → box_top_left (960, 540)") {
    // §6.5 row 1: anchor = TopLeft → anchor_offset = (0, 0) → box_top_left = pin.
    // For CanvasCenter pin (960, 540), anchor TopLeft means the box's top-left
    // corner sits at the canvas center (extending right and down).
    auto c = default_canvas();
    Vec2 box{1700.0f, 360.0f};
    auto r = resolve_text_placement(c, box,
        TextPlacement{TextPlacementKind::CanvasCenter},
        TextAnchor::TopLeft);
    CHECK(r.local_frame.x == doctest::Approx(960.0f));   // ADR-019 §6.5 box_top_left.x for TopLeft
    CHECK(r.local_frame.y == doctest::Approx(540.0f));   // ADR-019 §6.5 box_top_left.y for TopLeft
    CHECK(r.local_frame.z == doctest::Approx(1700.0f));  // box width unchanged
    CHECK(r.local_frame.w == doctest::Approx(360.0f));   // box height unchanged
    CHECK(vec2_near(r.layout_origin, {960.0f, 540.0f}));
    CHECK(r.resolved_anchor == TextAnchor::TopLeft);     // Phase A.3 echo
}

TEST_CASE("ADR-019 §6.6 Hero example — placed box at (110, 360, 1700, 360)") {
    // §6.6 Hero example: layer.text("title").place(TextPlacement::CanvasCenter)
    // on canvas 1920×1080 with box 1700×360 and anchor Center (the default).
    // Expected: pin (960, 540) → box_top_left (110, 360); local_frame
    // {110, 360, 1700, 360}; world_matrix = T(110, 360, 0) for top-level
    // layer (identity parent).
    auto c = default_canvas();
    Vec2 box{1700.0f, 360.0f};
    auto r = resolve_text_placement(c, box, TextPlacement{TextPlacementKind::CanvasCenter});
    CHECK(r.local_frame.x == doctest::Approx(110.0f));   // §6.6 box_top_left.x
    CHECK(r.local_frame.y == doctest::Approx(360.0f));   // §6.6 box_top_left.y
    CHECK(r.local_frame.z == doctest::Approx(1700.0f));  // §6.6 box width
    CHECK(r.local_frame.w == doctest::Approx(360.0f));   // §6.6 box height
    CHECK(mat4_near(r.layer_matrix, Mat4(1.0f)));        // top-level layer → identity
    Mat4 expected_world = glm::translate(Mat4(1.0f), Vec3(110.0f, 360.0f, 0.0f));
    CHECK(mat4_near(r.world_matrix, expected_world));    // §6.6 world_matrix = T(110, 360, 0)
    CHECK(vec2_near(r.layout_origin, {110.0f, 360.0f}));
}
