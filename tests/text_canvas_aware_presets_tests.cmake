# tests/text_canvas_aware_presets_tests.cmake
# ─────────────────────────────────────────────────────────────────────
# TICKET-PRESET-CANVAS-AWARE-COORDS — ERT-parameterized canvas-aware
# verification of the 5 reusable `chronon3d::presets::text::` v1
# presets across 5 canvas configurations:
#
#   1. 1920×1080 (Landscape 16:9, canonical reference)
#   2. 1080×1920 (Portrait 9:16 — TikTok / Reels / Shorts native)
#   3. 1080×1080 (Square 1:1 — Instagram feed)
#   4. 3840×2160 (4K Landscape 16:9 — high-end preview / archive)
#   5. 1280×720  (Custom 16:9 — intermediate resolution)
#
# For each (preset, canvas) pair three invariants are locked:
#
#   I1. Box fits in canvas       (per-axis)
#   I2. Pin stays inside safe area (no out-of-canvas box)
#   I3. Placement is semantic     (NOT Absolute — no hard-coded coord)
#
# Total: 5 presets × 5 canvases × 3 invariants = 75 CHECK() assertions,
# surfaced via DOCTEST GENERATE() with one subcase per canvas variant.
#
# Test tier: UNIT (pure struct + math inspection; no Framebuffer /
# compositor / GPU / Blend2D / FontEngine / HarfBuzz).  Compiles
# UNCONDITIONALLY (mirrors `test_presets_stability.cpp` +
# `test_safe_area_placement.cpp` UNCONDITIONAL pattern).
#
# Anti-duplicazione honour:
#   - Reuses canonical CanvasInfo / SafeAreaPreset / TextBoxConstraints /
#     AspectRatioPolicy types — zero new symbols in include/chronon3d/.
#   - Reuses DOCTEST GENERATE() — no parallel parametrization framework.
# ─────────────────────────────────────────────────────────────────────

chronon3d_add_test_suite(
    NAME chronon3d_text_canvas_aware_presets_tests
    TIER UNIT
    LINK_TARGETS chronon3d_text_core chronon3d_backend_text
    SOURCES text/test_preset_canvas_aware_presets.cpp
)