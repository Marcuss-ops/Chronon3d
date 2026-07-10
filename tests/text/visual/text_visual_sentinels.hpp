// ═══════════════════════════════════════════════════════════════════════════
// tests/text/visual/text_visual_sentinels.hpp — Phase-2.1 P0 split
//
// 128 explicit sentinel constants (16 presets × 8 each: 4 timestamps ×
// 2 aspect ratios).  Mechanical split-off from
// tests/text/test_text_preset_visual.cpp (formerly 248–440 LOC).  // drift-allow: stale-ref
//
// Per user spec: `i sentinels sono in file dati (non logica test)`.
// This header holds ONLY constexpr uint64_t data + header comments.
// No TEST_CASE / no SUBCASE / no logic — keep it pure-data so the
// capture workflow (`grep first hash to capture: <hash>`) can land
// each captured value directly into its literal without argument
// confusion.
//
// Captured 2026-06-24 from main@56cde97fd9 (build RC=0, run RC=0,
// 3-run determinism: hashes identical across consecutive runs).
// Each sentinel stores the observed framebuffer_hash for the
// (preset, ratio, frame) point.  Multiple sentinels may share the
// same observed hash (e.g. all 32 Transparent + PartialCoverage
// sentinels render an all-zero-alpha framebuffer) — this is by
// design, not a capture bug.
// ═══════════════════════════════════════════════════════════════════════════
#pragma once

#include <cstdint>

// ── Reveal (10 presets × 8 sentinels = 80 sentinels) ─────────────────────

// TextAnimations
inline constexpr std::uint64_t kRefTextPresTextAnimations_169_F000 = 0xCA7F696086E7F8B4ULL;
inline constexpr std::uint64_t kRefTextPresTextAnimations_169_F020 = 0xC6A045F7AD1A0327ULL;
inline constexpr std::uint64_t kRefTextPresTextAnimations_169_F030 = 0xC6A045F7AD1A0327ULL;
inline constexpr std::uint64_t kRefTextPresTextAnimations_169_F040 = 0xC6A045F7AD1A0327ULL;
inline constexpr std::uint64_t kRefTextPresTextAnimations_916_F000 = 0xCA7F696086E7F8B4ULL;
inline constexpr std::uint64_t kRefTextPresTextAnimations_916_F020 = 0xB6B2271F075C563EULL;
inline constexpr std::uint64_t kRefTextPresTextAnimations_916_F030 = 0xB6B2271F075C563EULL;
inline constexpr std::uint64_t kRefTextPresTextAnimations_916_F040 = 0xB6B2271F075C563EULL;

// FadeIn
inline constexpr std::uint64_t kRefTextPresFadeIn_169_F000 = 0xCA7F696086E7F8B4ULL;
inline constexpr std::uint64_t kRefTextPresFadeIn_169_F020 = 0x27AA41CD4F05D75CULL;
inline constexpr std::uint64_t kRefTextPresFadeIn_169_F030 = 0x27AA41CD4F05D75CULL;
inline constexpr std::uint64_t kRefTextPresFadeIn_169_F040 = 0x27AA41CD4F05D75CULL;
inline constexpr std::uint64_t kRefTextPresFadeIn_916_F000 = 0xCA7F696086E7F8B4ULL;
inline constexpr std::uint64_t kRefTextPresFadeIn_916_F020 = 0x6AD1620C3845F8EDULL;
inline constexpr std::uint64_t kRefTextPresFadeIn_916_F030 = 0x6AD1620C3845F8EDULL;
inline constexpr std::uint64_t kRefTextPresFadeIn_916_F040 = 0x6AD1620C3845F8EDULL;

// BlurIn
inline constexpr std::uint64_t kRefTextPresBlurIn_169_F000 = 0xCA7F696086E7F8B4ULL;
inline constexpr std::uint64_t kRefTextPresBlurIn_169_F020 = 0xCA7F696086E7F8B4ULL;
inline constexpr std::uint64_t kRefTextPresBlurIn_169_F030 = 0xDFCB192ADA69503AULL;
inline constexpr std::uint64_t kRefTextPresBlurIn_169_F040 = 0xDFCB192ADA69503AULL;
inline constexpr std::uint64_t kRefTextPresBlurIn_916_F000 = 0xCA7F696086E7F8B4ULL;
inline constexpr std::uint64_t kRefTextPresBlurIn_916_F020 = 0xCA7F696086E7F8B4ULL;
inline constexpr std::uint64_t kRefTextPresBlurIn_916_F030 = 0x6A8A5E5F038BD129ULL;
inline constexpr std::uint64_t kRefTextPresBlurIn_916_F040 = 0x6A8A5E5F038BD129ULL;

// SlideUp
inline constexpr std::uint64_t kRefTextPresSlideUp_169_F000 = 0xCA7F696086E7F8B4ULL;
inline constexpr std::uint64_t kRefTextPresSlideUp_169_F020 = 0xFC161DFF734E490FULL;
inline constexpr std::uint64_t kRefTextPresSlideUp_169_F030 = 0xFC161DFF734E490FULL;
inline constexpr std::uint64_t kRefTextPresSlideUp_169_F040 = 0xFC161DFF734E490FULL;
inline constexpr std::uint64_t kRefTextPresSlideUp_916_F000 = 0xCA7F696086E7F8B4ULL;
inline constexpr std::uint64_t kRefTextPresSlideUp_916_F020 = 0x0FD52CD0B591CDB4ULL;
inline constexpr std::uint64_t kRefTextPresSlideUp_916_F030 = 0x0FD52CD0B591CDB4ULL;
inline constexpr std::uint64_t kRefTextPresSlideUp_916_F040 = 0x0FD52CD0B591CDB4ULL;

// SlideDown
inline constexpr std::uint64_t kRefTextPresSlideDown_169_F000 = 0xCA7F696086E7F8B4ULL;
inline constexpr std::uint64_t kRefTextPresSlideDown_169_F020 = 0xE59D392631667582ULL;
inline constexpr std::uint64_t kRefTextPresSlideDown_169_F030 = 0xE59D392631667582ULL;
inline constexpr std::uint64_t kRefTextPresSlideDown_169_F040 = 0xE59D392631667582ULL;
inline constexpr std::uint64_t kRefTextPresSlideDown_916_F000 = 0xCA7F696086E7F8B4ULL;
inline constexpr std::uint64_t kRefTextPresSlideDown_916_F020 = 0x4ACE36B7135C6526ULL;
inline constexpr std::uint64_t kRefTextPresSlideDown_916_F030 = 0x4ACE36B7135C6526ULL;
inline constexpr std::uint64_t kRefTextPresSlideDown_916_F040 = 0x4ACE36B7135C6526ULL;

// ScaleIn
inline constexpr std::uint64_t kRefTextPresScaleIn_169_F000 = 0xCA7F696086E7F8B4ULL;
inline constexpr std::uint64_t kRefTextPresScaleIn_169_F020 = 0x59356830127DB75CULL;
inline constexpr std::uint64_t kRefTextPresScaleIn_169_F030 = 0x59356830127DB75CULL;
inline constexpr std::uint64_t kRefTextPresScaleIn_169_F040 = 0x59356830127DB75CULL;
inline constexpr std::uint64_t kRefTextPresScaleIn_916_F000 = 0xCA7F696086E7F8B4ULL;
inline constexpr std::uint64_t kRefTextPresScaleIn_916_F020 = 0x1C7A03DA4E64D649ULL;
inline constexpr std::uint64_t kRefTextPresScaleIn_916_F030 = 0x1C7A03DA4E64D649ULL;
inline constexpr std::uint64_t kRefTextPresScaleIn_916_F040 = 0x1C7A03DA4E64D649ULL;

// TrackingClose — AGENT 2 / TICKET-A2 reset (resolver-driven, see explainer
// in tests/text/test_text_preset_reveal.cpp caption above each emit_preset_gate).
inline constexpr std::uint64_t kRefTextPresTrackingClose_169_F000 = 0xDEADBEEFDEADBEEFULL;
inline constexpr std::uint64_t kRefTextPresTrackingClose_169_F020 = 0xDEADBEEFDEADBEEFULL;
inline constexpr std::uint64_t kRefTextPresTrackingClose_169_F030 = 0xDEADBEEFDEADBEEFULL;
inline constexpr std::uint64_t kRefTextPresTrackingClose_169_F040 = 0xDEADBEEFDEADBEEFULL;
inline constexpr std::uint64_t kRefTextPresTrackingClose_916_F000 = 0xDEADBEEFDEADBEEFULL;
inline constexpr std::uint64_t kRefTextPresTrackingClose_916_F020 = 0xDEADBEEFDEADBEEFULL;
inline constexpr std::uint64_t kRefTextPresTrackingClose_916_F030 = 0xDEADBEEFDEADBEEFULL;
inline constexpr std::uint64_t kRefTextPresTrackingClose_916_F040 = 0xDEADBEEFDEADBEEFULL;

// MaskedLineReveal
inline constexpr std::uint64_t kRefTextPresMaskedLineReveal_169_F000 = 0xCA7F696086E7F8B4ULL;
inline constexpr std::uint64_t kRefTextPresMaskedLineReveal_169_F020 = 0xCA7F696086E7F8B4ULL;
inline constexpr std::uint64_t kRefTextPresMaskedLineReveal_169_F030 = 0x0F44807361BA6E90ULL;
inline constexpr std::uint64_t kRefTextPresMaskedLineReveal_169_F040 = 0x0F44807361BA6E90ULL;
inline constexpr std::uint64_t kRefTextPresMaskedLineReveal_916_F000 = 0xCA7F696086E7F8B4ULL;
inline constexpr std::uint64_t kRefTextPresMaskedLineReveal_916_F020 = 0xCA7F696086E7F8B4ULL;
inline constexpr std::uint64_t kRefTextPresMaskedLineReveal_916_F030 = 0xD566CEBA7F5907F8ULL;
inline constexpr std::uint64_t kRefTextPresMaskedLineReveal_916_F040 = 0xD566CEBA7F5907F8ULL;

// WordCascade — AGENT 2 / TICKET-A2 reset
inline constexpr std::uint64_t kRefTextPresWordCascade_169_F000 = 0xDEADBEEFDEADBEEFULL;
inline constexpr std::uint64_t kRefTextPresWordCascade_169_F020 = 0xDEADBEEFDEADBEEFULL;
inline constexpr std::uint64_t kRefTextPresWordCascade_169_F030 = 0xDEADBEEFDEADBEEFULL;
inline constexpr std::uint64_t kRefTextPresWordCascade_169_F040 = 0xDEADBEEFDEADBEEFULL;
inline constexpr std::uint64_t kRefTextPresWordCascade_916_F000 = 0xDEADBEEFDEADBEEFULL;
inline constexpr std::uint64_t kRefTextPresWordCascade_916_F020 = 0xDEADBEEFDEADBEEFULL;
inline constexpr std::uint64_t kRefTextPresWordCascade_916_F030 = 0xDEADBEEFDEADBEEFULL;
inline constexpr std::uint64_t kRefTextPresWordCascade_916_F040 = 0xDEADBEEFDEADBEEFULL;

// CharacterCascade — AGENT 2 / TICKET-A2 reset
inline constexpr std::uint64_t kRefTextPresCharacterCascade_169_F000 = 0xDEADBEEFDEADBEEFULL;
inline constexpr std::uint64_t kRefTextPresCharacterCascade_169_F020 = 0xDEADBEEFDEADBEEFULL;
inline constexpr std::uint64_t kRefTextPresCharacterCascade_169_F030 = 0xDEADBEEFDEADBEEFULL;
inline constexpr std::uint64_t kRefTextPresCharacterCascade_169_F040 = 0xDEADBEEFDEADBEEFULL;
inline constexpr std::uint64_t kRefTextPresCharacterCascade_916_F000 = 0xDEADBEEFDEADBEEFULL;
inline constexpr std::uint64_t kRefTextPresCharacterCascade_916_F020 = 0xDEADBEEFDEADBEEFULL;
inline constexpr std::uint64_t kRefTextPresCharacterCascade_916_F030 = 0xDEADBEEFDEADBEEFULL;
inline constexpr std::uint64_t kRefTextPresCharacterCascade_916_F040 = 0xDEADBEEFDEADBEEFULL;

// ── Emphasis (4 presets × 8 sentinels = 32 sentinels) ─────────────────────

// WordPop
inline constexpr std::uint64_t kRefTextPresWordPop_169_F000 = 0xCA7F696086E7F8B4ULL;
inline constexpr std::uint64_t kRefTextPresWordPop_169_F020 = 0x12F47704662555FEULL;
inline constexpr std::uint64_t kRefTextPresWordPop_169_F030 = 0x12F47704662555FEULL;
inline constexpr std::uint64_t kRefTextPresWordPop_169_F040 = 0x12F47704662555FEULL;
inline constexpr std::uint64_t kRefTextPresWordPop_916_F000 = 0xCA7F696086E7F8B4ULL;
inline constexpr std::uint64_t kRefTextPresWordPop_916_F020 = 0xB500B523E8B2386DULL;
inline constexpr std::uint64_t kRefTextPresWordPop_916_F030 = 0xB500B523E8B2386DULL;
inline constexpr std::uint64_t kRefTextPresWordPop_916_F040 = 0xB500B523E8B2386DULL;

// ScalePunch
inline constexpr std::uint64_t kRefTextPresScalePunch_169_F000 = 0xCA7F696086E7F8B4ULL;
inline constexpr std::uint64_t kRefTextPresScalePunch_169_F020 = 0x925D84677F40E896ULL;
inline constexpr std::uint64_t kRefTextPresScalePunch_169_F030 = 0x925D84677F40E896ULL;
inline constexpr std::uint64_t kRefTextPresScalePunch_169_F040 = 0x925D84677F40E896ULL;
inline constexpr std::uint64_t kRefTextPresScalePunch_916_F000 = 0xCA7F696086E7F8B4ULL;
inline constexpr std::uint64_t kRefTextPresScalePunch_916_F020 = 0x50E732BAC355B50FULL;
inline constexpr std::uint64_t kRefTextPresScalePunch_916_F030 = 0x50E732BAC355B50FULL;
inline constexpr std::uint64_t kRefTextPresScalePunch_916_F040 = 0x50E732BAC355B50FULL;

// ColorAccent
inline constexpr std::uint64_t kRefTextPresColorAccent_169_F000 = 0xCA7F696086E7F8B4ULL;
inline constexpr std::uint64_t kRefTextPresColorAccent_169_F020 = 0x27AA41CD4F05D75CULL;
inline constexpr std::uint64_t kRefTextPresColorAccent_169_F030 = 0x27AA41CD4F05D75CULL;
inline constexpr std::uint64_t kRefTextPresColorAccent_169_F040 = 0x27AA41CD4F05D75CULL;
inline constexpr std::uint64_t kRefTextPresColorAccent_916_F000 = 0xCA7F696086E7F8B4ULL;
inline constexpr std::uint64_t kRefTextPresColorAccent_916_F020 = 0x6AD1620C3845F8EDULL;
inline constexpr std::uint64_t kRefTextPresColorAccent_916_F030 = 0x6AD1620C3845F8EDULL;
inline constexpr std::uint64_t kRefTextPresColorAccent_916_F040 = 0x6AD1620C3845F8EDULL;

// GradientFill
inline constexpr std::uint64_t kRefTextPresGradientFill_169_F000 = 0xCA7F696086E7F8B4ULL;
inline constexpr std::uint64_t kRefTextPresGradientFill_169_F020 = 0x28E291D3D7F8CD29ULL;
inline constexpr std::uint64_t kRefTextPresGradientFill_169_F030 = 0x28E291D3D7F8CD29ULL;
inline constexpr std::uint64_t kRefTextPresGradientFill_169_F040 = 0x28E291D3D7F8CD29ULL;
inline constexpr std::uint64_t kRefTextPresGradientFill_916_F000 = 0xCA7F696086E7F8B4ULL;
inline constexpr std::uint64_t kRefTextPresGradientFill_916_F020 = 0xA1E89278EF91ACB9ULL;
inline constexpr std::uint64_t kRefTextPresGradientFill_916_F030 = 0xA1E89278EF91ACB9ULL;
inline constexpr std::uint64_t kRefTextPresGradientFill_916_F040 = 0xA1E89278EF91ACB9ULL;

// ── Subtitle (2 presets × 8 sentinels = 16 sentinels; caption_box +
//    glow_pulse deferred to A4.1 once bit-stable) ───────────────────────

// MinimalWhite — static text, all frames Visible.
inline constexpr std::uint64_t kRefTextPresMinimalWhite_169_F000 = 0x27AA41CD4F05D75CULL;
inline constexpr std::uint64_t kRefTextPresMinimalWhite_169_F020 = 0x27AA41CD4F05D75CULL;
inline constexpr std::uint64_t kRefTextPresMinimalWhite_169_F030 = 0x27AA41CD4F05D75CULL;
inline constexpr std::uint64_t kRefTextPresMinimalWhite_169_F040 = 0x27AA41CD4F05D75CULL;
inline constexpr std::uint64_t kRefTextPresMinimalWhite_916_F000 = 0x6AD1620C3845F8EDULL;
inline constexpr std::uint64_t kRefTextPresMinimalWhite_916_F020 = 0x6AD1620C3845F8EDULL;
inline constexpr std::uint64_t kRefTextPresMinimalWhite_916_F030 = 0x6AD1620C3845F8EDULL;
inline constexpr std::uint64_t kRefTextPresMinimalWhite_916_F040 = 0x6AD1620C3845F8EDULL;

// YellowKeyword
inline constexpr std::uint64_t kRefTextPresYellowKeyword_169_F000 = 0xCA7F696086E7F8B4ULL;
inline constexpr std::uint64_t kRefTextPresYellowKeyword_169_F020 = 0x27AA41CD4F05D75CULL;
inline constexpr std::uint64_t kRefTextPresYellowKeyword_169_F030 = 0x27AA41CD4F05D75CULL;
inline constexpr std::uint64_t kRefTextPresYellowKeyword_169_F040 = 0x27AA41CD4F05D75CULL;
inline constexpr std::uint64_t kRefTextPresYellowKeyword_916_F000 = 0xCA7F696086E7F8B4ULL;
inline constexpr std::uint64_t kRefTextPresYellowKeyword_916_F020 = 0x6AD1620C3845F8EDULL;
inline constexpr std::uint64_t kRefTextPresYellowKeyword_916_F030 = 0x6AD1620C3845F8EDULL;
inline constexpr std::uint64_t kRefTextPresYellowKeyword_916_F040 = 0x6AD1620C3845F8EDULL;

// ── Total: 16 presets × 8 sentinels = 128 sentinels. ───────────────────────
