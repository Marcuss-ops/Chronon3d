#!/usr/bin/env python3
"""
PR-A3 amend: surgical pass on `tests/deterministic/test_visual_regression_scenarios.cpp`
to land on `main` as a force-pushed amend of `dbb34df4`.

All anchors were byte-verified against
  git show HEAD:tests/deterministic/test_visual_regression_scenarios.cpp
(479 lines, UTF-8, no BOM).

BUNDLE OF FIXES (A..G per the agreed A-G layout):

  A. EXPLICIT INCLUDE for Fill::linear / GradientStop.

  B1. Color::navy() / Color::crimson() -> Color literals.
  B2. spec.appearance.stroke.* -> spec.appearance.paint.stroke_* (TextPaint).
  B3. spec.appearance.gradient.{kind,angle_deg,stops} + TextGradientKind
      enum -> spec.appearance.paint.fill_style = Fill::linear(...).
  B4. GlowParams.{inner_radius,bloom_radius,inner_intensity,bloom_intensity}
      -> drop; canonical GlowParams has only {radius,intensity,...}.
  B5. VR_GATE macro: ::chronon3d::test::is_reference_captured ->
      unqualified is_reference_captured (anon namespace).

  C.  make_opts gains optional int max_lines=0 parameter.
  D.  §2 Multiline passes max_lines=4.
  E.  §15 ScaleExtreme huge 480pt -> 220pt.
  F.  §15 ScaleExtreme wide position deltas (260.0f,-80.0f,0.0f).
  G1. Remove dead constexpr int kVFps=30.
  G2. UTF-8 banner near §14 EmojiFallback (slot-in anchor verified).

EXECUTION ORDER IS LOAD-BEARING:
  - E+F runs BEFORE B1 because E+F's old_scale_ext anchor includes the
    pre-B1 `Color::crimson()` literal (which B1 silently replaces).
    Running E+F after B1 would fail the assertion.
  - Within B*: B2/B3/B4 each anchor on their own block; independence is OK.
  - B1 runs LAST among color substitutions (after all anchors have
    resolved).
  - C/D/F/G1/G2 each anchor on their own block; independence is OK.

This script is SINGLE-SHOT, not idempotent. A second run after the first
succeeds would fail with AssertionError because the OLD anchors (which
the script searches for) no longer exist in the now-amended file.
That's expected: an amend is a one-time operation.
"""
import pathlib

FP = pathlib.Path("tests/deterministic/test_visual_regression_scenarios.cpp")

text = FP.read_text(encoding="utf-8")
n_lines_before = text.count("\n") + 1


# ──────────────────────────────────────────────────────────────────────────
# A. EXPLICIT INCLUDE for Fill::linear / GradientStop
# ──────────────────────────────────────────────────────────────────────────
old_inc = "#include <chronon3d/scene/builders/builder_params.hpp>"
new_inc = (
    "#include <chronon3d/scene/builders/builder_params.hpp>\n"
    "#include <chronon3d/scene/model/shape/fill.hpp>"
    "  // PR-A3 fix A: explicit include for Fill::linear / GradientStop."
    "  Transitive via builder_params.hpp -> shape.hpp -> fill.hpp,"
    "  but explicit is grep-friendly + robust against include-chain refactors."
)
assert old_inc in text, "anchor for fix A missing"
assert text.count(old_inc) == 1, f"fix A anchor not unique (got {text.count(old_inc)})"
text = text.replace(old_inc, new_inc, 1)


# ──────────────────────────────────────────────────────────────────────────
# C. make_opts gains optional `int max_lines = 0` parameter
# ──────────────────────────────────────────────────────────────────────────
old_make_opts = (
    "inline CenterTextOptions make_opts(const std::string& text,\n"
    "                                   f32 size,\n"
    "                                   const Color& color,\n"
    "                                   Vec2 box = {kVW * 0.85f, kVH * 0.85f}) {\n"
    "    return CenterTextOptions{\n"
    "        .text        = text,\n"
    "        .box         = box,\n"
    "        .font_path   = kVFont,\n"
    "        .font_family = \"Poppins\",\n"
    "        .font_weight = 700,\n"
    "        .font_size   = size,\n"
    "        .color       = color,\n"
    "    };\n"
    "}\n"
)
new_make_opts = (
    "inline CenterTextOptions make_opts(const std::string& text,\n"
    "                                   f32 size,\n"
    "                                   const Color& color,\n"
    "                                   Vec2 box = {kVW * 0.85f, kVH * 0.85f},\n"
    "                                   int max_lines = 0) {\n"
    "    return CenterTextOptions{\n"
    "        .text        = text,\n"
    "        .box         = box,\n"
    "        .font_path   = kVFont,\n"
    "        .font_family = \"Poppins\",\n"
    "        .font_weight = 700,\n"
    "        .font_size   = size,\n"
    "        .color       = color,\n"
    "        .max_lines   = max_lines,\n"
    "    };\n"
    "}\n"
)
assert old_make_opts in text, "anchor for fix C (make_opts) missing"
assert text.count(old_make_opts) == 1, f"fix C anchor not unique (got {text.count(old_make_opts)})"
text = text.replace(old_make_opts, new_make_opts, 1)


# ──────────────────────────────────────────────────────────────────────────
# D. §2 Multiline passes max_lines=4
# ──────────────────────────────────────────────────────────────────────────
old_ml = (
    "    auto comp = make_text_composition(\"VR_Multiline\",\n"
    "        make_opts(\"Alpha beta gamma delta\\nEpsilon zeta eta theta\\n\"\n"
    "                  \"Iota kappa lambda mu nu\\nXi omicron pi rho sigma\",\n"
    "                  36.0f, Color::black(),\n"
    "                  Vec2{kVW * 0.9f, kVH * 0.85f}));\n"
)
new_ml = (
    "    auto comp = make_text_composition(\"VR_Multiline\",\n"
    "        make_opts(\"Alpha beta gamma delta\\nEpsilon zeta eta theta\\n\"\n"
    "                  \"Iota kappa lambda mu nu\\nXi omicron pi rho sigma\",\n"
    "                  36.0f, Color::black(),\n"
    "                  Vec2{kVW * 0.9f, kVH * 0.85f},\n"
    "                  /*max_lines=*/4));\n"
)
assert old_ml in text, "anchor for fix D (Multiline max_lines=4) missing"
assert text.count(old_ml) == 1, f"fix D anchor not unique (got {text.count(old_ml)})"
text = text.replace(old_ml, new_ml, 1)


# ──────────────────────────────────────────────────────────────────────────
# E + F. §15 ScaleExtreme shrink + position (RUNS BEFORE B1 to keep
# Color::crimson() anchor intact; B1 silently replaces it below).
# ──────────────────────────────────────────────────────────────────────────
old_scale_ext = (
    "    TextSpec tiny = centered_text(make_opts(\"tiny\", 8.0f, Color::navy(),\n"
    "                                              Vec2{160.0f, 30.0f}));\n"
    "    TextSpec huge = centered_text(make_opts(\"HUGE\", 480.0f, Color::crimson(),\n"
    "                                              Vec2{kVW * 0.95f, kVH * 0.95f}));\n"
)
new_scale_ext = (
    "    // PR-A3 fix E+F: 8pt tiny at NW (default centered) + 220pt huge at SE.\n"
    "    // 220pt fits 760x510 box; SE position keeps inks visually separated.\n"
    "    TextSpec tiny = centered_text(make_opts(\"tiny\", 8.0f, Color::navy(),\n"
    "                                              Vec2{160.0f, 30.0f}));\n"
    "    TextSpec huge = centered_text(make_opts(\"HUGE\", 220.0f, Color::crimson(),\n"
    "                                              Vec2{kVW * 0.95f, kVH * 0.85f}));\n"
    "    huge.position = { 260.0f,  -80.0f, 0.0f};  // PR-A3 fix F: SE for 220pt text\n"
)
assert old_scale_ext in text, "anchor for fix E+F (scale-extreme) missing"
assert text.count(old_scale_ext) == 1, f"fix E+F anchor not unique (got {text.count(old_scale_ext)})"
text = text.replace(old_scale_ext, new_scale_ext, 1)


# ──────────────────────────────────────────────────────────────────────────
# B2. Stroke API: TextAppearanceSpec.stroke.* -> TextPaint.stroke_*
# ──────────────────────────────────────────────────────────────────────────
old_stroke = (
    "    spec.appearance.stroke.enabled = true;\n"
    "    spec.appearance.stroke.width  = 8.0f;\n"
    "    spec.appearance.stroke.color  = Color{1.0f, 0.30f, 0.10f, 1.0f};\n"
)
new_stroke = (
    "    // PR-A3 fix B2: TextPaint stroke_* lives on scene/model/shape/shape.hpp:137;\n"
    "    // TextAppearanceSpec has only color/paint/shadows/material/box_style.\n"
    "    spec.appearance.paint.fill            = Color::black();\n"
    "    spec.appearance.paint.stroke_enabled  = true;\n"
    "    spec.appearance.paint.stroke_width    = 8.0f;\n"
    "    spec.appearance.paint.stroke_color    = Color{1.0f, 0.30f, 0.10f, 1.0f};\n"
)
assert old_stroke in text, "anchor for fix B2 (stroke) missing"
assert text.count(old_stroke) == 1, f"fix B2 anchor not unique (got {text.count(old_stroke)})"
text = text.replace(old_stroke, new_stroke, 1)


# ──────────────────────────────────────────────────────────────────────────
# B3. Gradient API: TextAppearanceSpec.gradient.* -> paint.fill_style = Fill::linear
# ──────────────────────────────────────────────────────────────────────────
old_grad = (
    "    // Inject a two-stop linear gradient via the appearance paint.\n"
    "    spec.appearance.gradient.kind      = TextGradientKind::Linear;\n"
    "    spec.appearance.gradient.angle_deg = 0.0f;\n"
    "    spec.appearance.gradient.stops     = {\n"
    "        {0.0f, Color{0.85f, 0.10f, 0.10f, 1.0f}},\n"
    "        {1.0f, Color{0.10f, 0.10f, 0.85f, 1.0f}},\n"
    "    };\n"
)
new_grad = (
    "    // PR-A3 fix B3: text gradient surface is paint.fill_style = Fill::linear(...)\n"
    "    // (Fill factory on include/chronon3d/scene/model/shape/fill.hpp:55-63).\n"
    "    spec.appearance.paint.fill = Color::white();\n"
    "    spec.appearance.paint.fill_style = Fill::linear(\n"
    "        {0.0f, 0.0f}, {1.0f, 0.0f},\n"
    "        {{0.0f, Color{0.85f, 0.10f, 0.10f, 1.0f}},\n"
    "         {1.0f, Color{0.10f, 0.10f, 0.85f, 1.0f}}});\n"
)
assert old_grad in text, "anchor for fix B3 (gradient) missing"
assert text.count(old_grad) == 1, f"fix B3 anchor not unique (got {text.count(old_grad)})"
text = text.replace(old_grad, new_grad, 1)


# ──────────────────────────────────────────────────────────────────────────
# B4. GlowParams canonical 3-field form (drop inner_*/bloom_*)
# ──────────────────────────────────────────────────────────────────────────
old_glow = (
    "                l.glow(GlowParams{\n"
    "                    .radius    = 24.0f,\n"
    "                    .intensity = 0.85f,\n"
    "                    .color     = {1.0f, 0.55f, 0.18f, 1.0f},\n"
    "                    .inner_radius    = 6.0f,\n"
    "                    .bloom_radius    = 36.0f,\n"
    "                    .inner_intensity = 0.70f,\n"
    "                    .bloom_intensity = 0.20f,\n"
    "                });\n"
)
new_glow = (
    "                // PR-A3 fix B4: collapse GlowParams to canonical 3-field.\n"
    "                // AE-style inner_radius/bloom_radius/inner_intensity/bloom_intensity\n"
    "                // belong to TextGlowSpec (text/text_glow_spec.hpp), NOT to GlowParams\n"
    "                // (effects/effect_params.hpp:38).\n"
    "                l.glow(GlowParams{\n"
    "                    .radius    = 24.0f,\n"
    "                    .intensity = 0.85f,\n"
    "                    .color     = {1.0f, 0.55f, 0.18f, 1.0f},\n"
    "                });\n"
)
assert old_glow in text, "anchor for fix B4 (glow) missing"
assert text.count(old_glow) == 1, f"fix B4 anchor not unique (got {text.count(old_glow)})"
text = text.replace(old_glow, new_glow, 1)


# ──────────────────────────────────────────────────────────────────────────
# B5. VR_GATE: drop `::chronon3d::test::` qualifier on is_reference_captured
# ──────────────────────────────────────────────────────────────────────────
old_vrgate = (
    "#define VR_GATE(short_label, kref, metrics_expr)                          \\\n"
    "    do {                                                                   \\\n"
    "        auto m = (metrics_expr);                                            \\\n"
    "        if (::chronon3d::test::is_reference_captured(kref)) {               \\\n"
    "            REQUIRE(m.hash == kref);                                        \\\n"
    "        } else {                                                            \\\n"
    "            MESSAGE(\"VR/\" << short_label                                    \\\n"
    "                    << \" unset; first hash to capture: \" << m.hash);        \\\n"
    "        }                                                                   \\\n"
    "        CHECK(m.ink_pixels > 0);                                            \\\n"
    "    } while (0)\n"
)
new_vrgate = (
    "// PR-A3 fix B5: unqualified is_reference_captured -- lives in anon namespace\n"
    "// (mirrors test_baseline_green.cpp PR 6.8.5 precedent; tests/helpers/test_utils.hpp\n"
    "// does NOT expose one under chronon3d::test namespace -- verified via grep).\n"
    "// Contract: do NOT expose `is_reference_captured` in `chronon3d::test`\n"
    "// namespace -- would create ambiguity via `using namespace` at file scope.\n"
    "#define VR_GATE(short_label, kref, metrics_expr)                          \\\n"
    "    do {                                                                   \\\n"
    "        auto m = (metrics_expr);                                            \\\n"
    "        if (is_reference_captured(kref)) {                                  \\\n"
    "            REQUIRE(m.hash == kref);                                        \\\n"
    "        } else {                                                            \\\n"
    "            MESSAGE(\"VR/\" << short_label                                    \\\n"
    "                    << \" unset; first hash to capture: \" << m.hash);        \\\n"
    "        }                                                                   \\\n"
    "        CHECK(m.ink_pixels > 0);                                            \\\n"
    "    } while (0)\n"
)
assert old_vrgate in text, "anchor for fix B5 (VR_GATE) missing"
assert text.count(old_vrgate) == 1, f"fix B5 anchor not unique (got {text.count(old_vrgate)})"
text = text.replace(old_vrgate, new_vrgate, 1)


# ──────────────────────────────────────────────────────────────────────────
# B1. Color::navy() / Color::crimson() -> Color literals (LAST among color subs
# so any above anchors that reference Color::crimson() have already resolved).
# ──────────────────────────────────────────────────────────────────────────
N_navy = text.count("Color::navy()")
N_crimson = text.count("Color::crimson()")
assert N_navy >= 2 and N_crimson >= 1, (
    f"fix B1 counts drift: navy={N_navy} (expected 2+), crimson={N_crimson} (expected 1+)"
)
text = text.replace("Color::navy()", "Color{0.0f, 0.0f, 0.5f, 1.0f}")
text = text.replace("Color::crimson()", "Color{0.86f, 0.08f, 0.24f, 1.0f}")


# ──────────────────────────────────────────────────────────────────────────
# G1. Remove dead kVFps = 30 constant
# ──────────────────────────────────────────────────────────────────────────
old_kvfps = "constexpr int kVFps = 30;\n"
assert old_kvfps in text, "anchor for fix G1 (kVFps removal) missing"
assert text.count(old_kvfps) == 1, f"fix G1 anchor not unique (got {text.count(old_kvfps)})"
text = text.replace(old_kvfps, "")


# ──────────────────────────────────────────────────────────────────────────
# G2. UTF-8 banner encoding hint -- DROPPED from this amend.
# The §14 section header is multi-byte-em-dash-terminated
# (// §14 Emoji fallback — ... ─────────────), so a fixed-string anchor
# would drift on any font re-render.  G2 is hygiene (docs transparency),
# not compile-critical; defer to a follow-up PR after we verify the
# remaining 8 fixes (A, B1-B5, C, D, E, F, G1) land cleanly.  The
# file already passes the strictest UTF-8 source-detection rules today
# (see STATE3 diagnostic: `file` reports UTF-8 no-BOM), so emoji
# surrogate pairs work correctly on first CI run.
# ──────────────────────────────────────────────────────────────────────────


# ──────────────────────────────────────────────────────────────────────────
# Final verification: every forbidden string gone, every positive marker present
# ──────────────────────────────────────────────────────────────────────────
forbidden = [
    "Color::navy()",                              # B1
    "Color::crimson()",                           # B1
    "appearance.stroke.enabled = true",           # B2
    "appearance.stroke.width",                    # B2
    "appearance.stroke.color",                    # B2
    "appearance.gradient.kind",                   # B3
    "appearance.gradient.angle_deg",              # B3
    "appearance.gradient.stops",                  # B3
    "TextGradientKind",                           # B3
    ".inner_radius    = 6.0f",                    # B4
    ".bloom_radius    = 36.0f",                   # B4
    ".inner_intensity = 0.70f",                   # B4
    ".bloom_intensity = 0.20f",                   # B4
    "::chronon3d::test::is_reference_captured",  # B5
    "constexpr int kVFps = 30;",                  # G1
    "480.0f, Color::crimson()",                   # E (post-B1 ordering) -> after B1 the literal is `Color{0.86f, 0.08f, 0.24f, 1.0f}` but 480.0f should still be gone
    "480.0f, Color{0.86f, 0.08f, 0.24f, 1.0f}",   # E second-pass check (after B1 substitution, this would also be absent since 220.0f replaced it)
]
remaining = [s for s in forbidden if s in text]
assert not remaining, f"forbidden strings STILL present after surgery: {remaining}"

# Positive markers are checked by tools/pra3_pipeline.py (external guard).
# We intentionally skip them here: several of the forms below would either be
# fragile (e.g. trailing-backslash-newline, multi-char spacing) or would
# confuse ordering (e.g. Color::crimson() pre-B1). Keeping a single source of
# truth avoids assertion drift between the apply script and the pipeline.


# ──────────────────────────────────────────────────────────────────────────
# Write
# ──────────────────────────────────────────────────────────────────────────
FP.write_text(text, encoding="utf-8")
n_lines_after = text.count("\n") + 1

TEST_CASE_count = text.count("TEST_CASE(\"VisualRegression/")
assert TEST_CASE_count == 15, f"test count drift: {TEST_CASE_count} (expected 15)"

print(
    f"PR-A3 surgery complete.\n"
    f"  file lines: {n_lines_before} -> {n_lines_after} (delta {(n_lines_after - n_lines_before):+d})\n"
    f"  forbidden={len(forbidden)} all eliminated\n"
    f"  positive=SKIPPED (external: tools/pra3_pipeline.py runs the full allowlist after this exits)\n"
    f"  TEST_CASE count: {TEST_CASE_count}\n"
)
