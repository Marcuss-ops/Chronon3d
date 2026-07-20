// ═══════════════════════════════════════════════════════════════════════════
// tests/text_golden/matrix/test_golden_matrix_emphasis.cpp — Golden Matrix
// Sweep Harness for Emphasis-category presets (Batch 3)
//
// TICKET-GOLDEN-MATRIX-BATCH-3: applies the shared 5-dim × 3-ts matrix
// harness to the 4 registered baseline Emphasis-category presets:
//
//   * word_pop          (Stage 3, FASE 2b Word selector)
//   * scale_punch       (Stage 3)
//   * color_accent      (Stage 3)
//   * gradient_fill     (Stage 3)
//
// Matrix dimensions per preset: AR × text-length × scale × cache × ts
// = 2×2×2×2×3 = 48 cells/preset → 4 × 48 = 192 total cells.
// FAST_MODE (CHRONON3D_GOLDEN_MATRIX_FAST_MODE=1): 6 cells/preset → 24 total.
//
// ── FAT-BLOCKER NOTE ─────────────────────────────────────────────────────
// Batch 3 inherits the upstream missing `Poppins-Bold.ttf` FAT-BLOCKER
// (TICKET-TEST-FONT-ASSET-PATH).  This harness is structurally correct
// but `macchina-verify` will reproduce the Batch 1 silent-fake green
// (blank PNGs, empty_frame=true) until the host font path is fixed.
// `golden_missing_count` will stay >0 even after CHRONON3D_UPDATE_GOLDENS=1
// because HarfBuzz emits zero glyphs when the font asset is missing.
// Resolution: `mkdir -p assets/fonts && cp <ttf> assets/fonts/Poppins-Bold.ttf`.
// ═══════════════════════════════════════════════════════════════════════════

#include <doctest/doctest.h>
#include <tests/text_golden/matrix/golden_matrix_harness.hpp>

// ── FastMode contract test (TICKET-GOLDEN-MATRIX-BATCH-3 / C-4 / N-2) ───
//
// Pins the FAST_MODE reduction to 6 cells/preset (2 AR × 3 ts).  Locks the
// env-var contract so future refactors of `matrix_cells_for_preset()` cannot
// silently change the cell-count contract.
//
// Single source of truth: reuses `fast_mode()` from the harness to read
// `CHRONON3D_GOLDEN_MATRIX_FAST_MODE` — branch-divergent predicates (e.g..
// uppercase input handling in the harness but lowercase-only in the test)
// are impossible by construction (Cat-3 anti-dup).
TEST_CASE("GoldenMatrix/FastMode/CellCount") {
    using namespace chronon3d::test::matrix_harness;
    if (!fast_mode()) {
        MESSAGE("FAST_MODE not active (CHRONON3D_GOLDEN_MATRIX_FAST_MODE not set to "
                "1/true/on/yes) \u2014 contract skipped. Set the env-var to exercise.");
        return;
    }
    // FAST_MODE active: exactly 6 cells per preset (2 AR \u00d7 3 ts).
    auto cells = matrix_cells_for_preset("word_pop");
    REQUIRE(cells.size() == 6u);
    CHECK(cells[0].ar == AspectRatio::k16x9); CHECK(cells[0].t_frame == 0);
    CHECK(cells[1].ar == AspectRatio::k16x9); CHECK(cells[1].t_frame == 20);
    CHECK(cells[2].ar == AspectRatio::k16x9); CHECK(cells[2].t_frame == 40);
    CHECK(cells[3].ar == AspectRatio::k9x16); CHECK(cells[3].t_frame == 0);
    CHECK(cells[4].ar == AspectRatio::k9x16); CHECK(cells[4].t_frame == 20);
    CHECK(cells[5].ar == AspectRatio::k9x16); CHECK(cells[5].t_frame == 40);
}

TEST_CASE("GoldenMatrix/Emphasis/WordPop") {
    chronon3d::test::matrix_harness::sweep_preset_matrix("word_pop");
}

TEST_CASE("GoldenMatrix/Emphasis/ScalePunch") {
    chronon3d::test::matrix_harness::sweep_preset_matrix("scale_punch");
}

TEST_CASE("GoldenMatrix/Emphasis/ColorAccent") {
    chronon3d::test::matrix_harness::sweep_preset_matrix("color_accent");
}

TEST_CASE("GoldenMatrix/Emphasis/GradientFill") {
    chronon3d::test::matrix_harness::sweep_preset_matrix("gradient_fill");
}
