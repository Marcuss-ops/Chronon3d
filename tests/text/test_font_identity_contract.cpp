// ═══════════════════════════════════════════════════════════════════════════
// tests/text/test_font_identity_contract.cpp
//
//   P1-8 — regression lock for the `font_identity_of(FontSpec) ->
//   FontIdentity` canonical helper and `FontIdentity::operator== / !=`.
//
//   The helper projects a full FontSpec to its identity subset
//   (font_path, font_family, font_weight, font_style) and DROPS
//   font_size because size is a layout concern, not a font identity:
//   superscript / drop-cap / size-varying typewriter intros all use
//   the SAME FONT at different sizes.
//
//   This test locks two contracts:
//
//     (1) The 4 identity fields are the ONLY fields compared by the
//         canonical equality.  font_size is silently dropped (changing
//         font_size alone returns EQUAL — even at wildly different
//         sizes like 6 vs 96).
//
//     (2) Every single identity field is sufficient on its own to
//         produce a NOT-EQUAL identity.  Path / family / weight / style
//         each independently trip the equality.
//
//   No font engine, no system fonts, no PRNG, no threads, no time —
//   pure helper / POD test (AGENTS.md v0.1 cat-2 freeze-compliant).
//   Mirrors the test_layout_cache_collision.cpp pattern (also pure
//   key-construction).
//
//   These contracts MUST hold for the multi-font pre-check in
//   `compile_text_document` (P1-7 site) and the font_spans emission
//   in `compile_text_layout` (line 564) to stay in sync — a future
//   helper change that drops a field would silently break the
//   multi-font detection in the OPPOSITE direction.
// ═══════════════════════════════════════════════════════════════════════════

#include <doctest/doctest.h>
#include <chronon3d/text/font_engine.hpp>

using chronon3d::FontSpec;
using chronon3d::FontIdentity;
using chronon3d::font_identity_of;

namespace {

/// Canonical baseline identity.  Every test in this file mutates ONE
/// field off `base_spec` and asserts the resulting projection is the
/// same identity (==) or different identity (!=) — the contract this
/// test is locking.
const FontSpec base_spec{
    /*font_path   = */ "fonts/Inter-Regular.ttf",
    /*font_family = */ "Inter",
    /*font_weight = */ 400,
    /*font_style  = */ "normal",
    /*font_size   = */ 16.0f,  // <-- intentionally different on every
                               //     "only font_size changes" mutation
};

const FontIdentity base_id = font_identity_of(base_spec);

} // namespace


// ── Group A — identity field semantics: font_size NEVER diverges ──────
//
//   Changing font_size alone (the 5th field of FontSpec, the ONLY field
//   FontSpec carries that is NOT part of identity) MUST NOT diverge the
//   identity.  This is the bug that superscript / drop-cap / typewriter
//   intro regression tests would silently fail on if a future refactor
//   were to add font_size to FontIdentity by accident.

TEST_CASE("font_identity_of: font_size change does NOT diverge identity (contract)") {
    // Sanity: identity is its own projection.
    CHECK(font_identity_of(base_spec) == base_id);

    // 6 pt — small (subscript / fine print).
    CHECK(font_identity_of(FontSpec{
        base_spec.font_path, base_spec.font_family,
        base_spec.font_weight, base_spec.font_style,
        /*font_size=*/6.0f}) == base_id);

    // 16 pt — same as baseline.
    CHECK(font_identity_of(FontSpec{
        base_spec.font_path, base_spec.font_family,
        base_spec.font_weight, base_spec.font_style,
        /*font_size=*/16.0f}) == base_id);

    // 36 pt — large (heading).
    CHECK(font_identity_of(FontSpec{
        base_spec.font_path, base_spec.font_family,
        base_spec.font_weight, base_spec.font_style,
        /*font_size=*/36.0f}) == base_id);

    // 96 pt — enormous (poster / drop-cap).  Even at 6x the baseline,
    // a same-font at different size is still the SAME identity.
    CHECK(font_identity_of(FontSpec{
        base_spec.font_path, base_spec.font_family,
        base_spec.font_weight, base_spec.font_style,
        /*font_size=*/96.0f}) == base_id);

    // 0 pt — defensive (a "size zero" FontSpec is malformed but the
    // identity projection must still not diverge: identity is
    // independent of size).
    CHECK(font_identity_of(FontSpec{
        base_spec.font_path, base_spec.font_family,
        base_spec.font_weight, base_spec.font_style,
        /*font_size=*/0.0f}) == base_id);
}


// ── Group B — identity field semantics: each of the 4 fields diverges
//   independently ──────────────────────────────────────────────────────
//
//   Each identity field on its own is sufficient to flip the canonical
//   equality.  If any of these ever stop diverging, the multi-font
//   pre-check in `compile_text_document` will start grouping different
//   fonts as a single identity — a silent and severe regression for the
//   Phase 1.4 additive font_spans path.

TEST_CASE("font_identity_of: font_path change DOES diverge identity (contract)") {
    // Different file at the same family — clearly different font.
    CHECK(font_identity_of(FontSpec{
        "fonts/Inter-Bold.ttf",
        base_spec.font_family, base_spec.font_weight,
        base_spec.font_style, base_spec.font_size}) != base_id);
    // Same family but missing at runtime — still different identity.
    CHECK(font_identity_of(FontSpec{
        "fonts/NonExistent.ttf",
        base_spec.font_family, base_spec.font_weight,
        base_spec.font_style, base_spec.font_size}) != base_id);
    // Empty path (FailedFont state) is also distinct from filled path.
    CHECK(font_identity_of(FontSpec{
        /*font_path=*/std::string{},
        base_spec.font_family, base_spec.font_weight,
        base_spec.font_style, base_spec.font_size}) != base_id);
}

TEST_CASE("font_identity_of: font_family change DOES diverge identity (contract)") {
    // Different family at same path (rare file reuse across faces).
    CHECK(font_identity_of(FontSpec{
        base_spec.font_path,
        /*font_family=*/"Roboto",
        base_spec.font_weight, base_spec.font_style,
        base_spec.font_size}) != base_id);
    // Empty family (failed resolve + missing fallback) — also distinct.
    CHECK(font_identity_of(FontSpec{
        base_spec.font_path,
        /*font_family=*/std::string{},
        base_spec.font_weight, base_spec.font_style,
        base_spec.font_size}) != base_id);
}

TEST_CASE("font_identity_of: font_weight change DOES diverge identity (contract)") {
    // CSS-style weights 100..900.  Bumping from regular (400) to
    // bold (700) MUST diverge — that's why the multi-font detector
    // exists at all.
    CHECK(font_identity_of(FontSpec{
        base_spec.font_path, base_spec.font_family,
        /*font_weight=*/700,
        base_spec.font_style, base_spec.font_size}) != base_id);
    // The 100-step granularity between weights (300 vs 400) MUST
    // also diverge — Italic-vs-Oblique subtleties later.
    CHECK(font_identity_of(FontSpec{
        base_spec.font_path, base_spec.font_family,
        /*font_weight=*/300,
        base_spec.font_style, base_spec.font_size}) != base_id);
}

TEST_CASE("font_identity_of: font_style change DOES diverge identity (contract)") {
    // CSS-style strings; regular -> italic is the canonical
    // multi-font trigger in mixed-style paragraphs.
    CHECK(font_identity_of(FontSpec{
        base_spec.font_path, base_spec.font_family,
        base_spec.font_weight,
        /*font_style=*/"italic",
        base_spec.font_size}) != base_id);
    // Oblique MUST also diverge from regular — different shaping.
    CHECK(font_identity_of(FontSpec{
        base_spec.font_path, base_spec.font_family,
        base_spec.font_weight,
        /*font_style=*/"oblique",
        base_spec.font_size}) != base_id);
    // Empty style (parameter unset) — also distinct.
    CHECK(font_identity_of(FontSpec{
        base_spec.font_path, base_spec.font_family,
        base_spec.font_weight,
        /*font_style=*/std::string{},
        base_spec.font_size}) != base_id);
}


// ── Group C — combined field changes still diverge; trivial identity is equal
//
//   The helper composes correctly against itself: any combination of
//   1..4 field changes is NOT equal.  Two trivially-default
//   FontSpecs project to the SAME identity (the docs-required
//   default-init contract).

TEST_CASE("font_identity_of: divergent on combined field changes (contract)") {
    // All 4 fields changed at once — still different identity.
    CHECK(font_identity_of(FontSpec{
        "fonts/Roboto-Bold.ttf", "Roboto", 700, "italic",
        72.0f}) != base_id);

    // Two fields changed at once (path + weight) — still different.
    CHECK(font_identity_of(FontSpec{
        "fonts/Roboto-Regular.ttf", base_spec.font_family,
        /*font_weight=*/700, base_spec.font_style,
        base_spec.font_size}) != base_id);

    // Combined with size shift — size is dropped, so still different.
    CHECK(font_identity_of(FontSpec{
        "fonts/NotOurFont.ttf", base_spec.font_family,
        base_spec.font_weight, base_spec.font_style,
        /*font_size=*/999.0f}) != base_id);
}

TEST_CASE("font_identity_of: two default-constructed FontSpecs share identity (contract)") {
    // Default FontSpec{}.  Both members pull the in-class initializers
    // (font_weight=400, font_style="normal").  Both have empty
    // font_path / font_family — everything matches.
    FontSpec a{};
    FontSpec b{};
    CHECK(font_identity_of(a) == font_identity_of(b));
    CHECK_FALSE(font_identity_of(a) != font_identity_of(b));
}


// ── Group D — operator== and operator!= consistency ────────────────────
//
//   The two operators MUST be exact logical complements: x==y ⟺ !(x!=y).
//   This catches the class of bug where `operator==` and `operator!=`
//   could drift in their implementations (e.g. one adds a field the
//   other does not).

TEST_CASE("font_identity_of: operator== and operator!= are exact complements (contract)") {
    // Equal case: == true, != false (verified for the trivial equal pair).
    FontSpec same_a = base_spec;
    FontSpec same_b{
        base_spec.font_path, base_spec.font_family,
        base_spec.font_weight, base_spec.font_style,
        /*font_size=*/48.0f};  // font_size alone change: identity equal
    const FontIdentity id_a = font_identity_of(same_a);
    const FontIdentity id_b = font_identity_of(same_b);
    CHECK(id_a == id_b);
    CHECK_FALSE(id_a != id_b);

    // Not-equal case: == false, != true.
    FontSpec different = base_spec;
    different.font_path = "fonts/OtherFont.ttf";
    const FontIdentity id_c = font_identity_of(different);
    CHECK_FALSE(id_a == id_c);
    CHECK(id_a != id_c);
}


// ── Group E — backwards-compatible with FontSpec::operator== ──────────
//
//   FontSpec::operator== uses the SAME 4 fields (font_engine.hpp
//   confirms this).  For FontSpec a/b that are FontSpec-equal, their
//   font_identity_of projections MUST also be equal.  This lock
//   prevents the helper from drifting to a different notion of
//   equality than the full FontSpec equality.

TEST_CASE("font_identity_of: FontSpec equality implies identity equality (lock with FontSpec::operator==)") {
    FontSpec equal_to_base{
        base_spec.font_path, base_spec.font_family,
        base_spec.font_weight, base_spec.font_style,
        /*font_size=*/99.0f};  // different size, same identity

    // Sanity: FontSpec equality holds (same 4 fields).
    CHECK(equal_to_base == base_spec);

    // Identity must agree with FontSpec equality here.
    CHECK(font_identity_of(equal_to_base) == font_identity_of(base_spec));

    // Now toggle ONE field — FontSpec diverges, identity diverges.
    FontSpec not_equal_to_base{
        "fonts/Different.ttf", base_spec.font_family,
        base_spec.font_weight, base_spec.font_style, base_spec.font_size};
    CHECK_FALSE(not_equal_to_base == base_spec);
    CHECK(font_identity_of(not_equal_to_base) != font_identity_of(base_spec));
}
