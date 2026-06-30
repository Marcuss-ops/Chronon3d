// ═══════════════════════════════════════════════════════════════════════════
// FASE 5 (TICKET-099a) — internal-only domain type aliases.
//
// Typed aliases over existing C++ primitives that capture the domain
// vocabulary: FontAssetId / ResolvedFontPath / FontFamilyName /
// Bcp47LanguageTag / TextPresetId / SelectorId / FontWeight.
//
// These aliases are NOT installed, NOT exported in `include/chronon3d/`.
// They live under `src/text/` so internal-only consumers (resolver,
// selector, registry, registry-resolver) can use them for compile-time
// semantic tagging without expanding the public API surface.
//
// Freeze compliance (AGENTS.md v0.1): the aliases resolve to the SAME
// bytewise layout as their underlying types (`std::string`,
// `std::string_view`, `int`).  No new class, no new runtime representation.
// Promotion to `include/chronon3d/text/` (TICKET-101, deferred) is
// reserved for the post-baseline-verde freeze revocation per
// AGENTS.md §Feature Freeze — Revoca.
//
// AGENTS.md Gate 5 (deny-list) compliance: no `<unicode/...>` or
// `<libtess2>` includes anywhere in this header.
//
// Usage (internal only):
//     #include "src/text/aliases.hpp"
//     using namespace chronon3d::text::domain;
//     FontAssetId id = "fonts/Inter-Regular.ttf";      // std::string_view
//     FontWeight  w  = 400;                             // int
//     auto p = static_cast<ResolvedFontPath>(resolved);  // std::string
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include <string>
#include <string_view>

namespace chronon3d {
namespace text {
namespace domain {

// ── Font identity ──────────────────────────────────────────────────────────
//
// FontAssetId        — opaque handle to a font asset (logical file name or
//                      resolver-keyed slot).  Same bytewise layout as
//                      `std::string_view`.  Use for inputs that the asset
//                      resolver will resolve against the canonical asset
//                      tree.
//
// ResolvedFontPath   — absolute or asset-rooted filesystem path produced
//                      by `AssetResolver::resolve_lexical(...)`.  Same
//                      bytewise layout as `std::string`.
//
// FontFamilyName     — human-readable family name (e.g. "Inter").  Same
//                      bytewise layout as `std::string`.
using FontAssetId      = std::string_view;
using ResolvedFontPath = std::string;
using FontFamilyName   = std::string;

// ── BCP-47 language tag ────────────────────────────────────────────────────
//
// Bcp47LanguageTag   — RFC 5646 language tag (e.g. "en-US", "ar",
//                      "zh-Hant-HK").  Passes through to HarfBuzz
//                      `hb_language_from_string(...)` unchanged.
//                      Same bytewise layout as `std::string`.
using Bcp47LanguageTag = std::string;

// ── Preset / selector identity ─────────────────────────────────────────────
//
// TextPresetId       — opaque handle to a `TextPresetRegistry` entry
//                      (e.g. "fade_in", "cinematic_text_camera").  Same
//                      bytewise layout as `std::string`.
//
// SelectorId         — opaque handle to a `GlyphSelectorSpec::id` field
//                      (e.g. "presetc_word_cascade_sel_word").  Same
//                      bytewise layout as `std::string`.
using TextPresetId = std::string;
using SelectorId   = std::string;

// ── Font weight ────────────────────────────────────────────────────────────
//
// FontWeight         — OpenType-compatible font weight (100..900, regular
//                      = 400, bold = 700).  Same bytewise layout as `int`.
//                      Matches the existing `FontSpec::font_weight` field
//                      in `include/chronon3d/text/font_engine.hpp`.
using FontWeight = int;

} // namespace chronon3d::text::domain
} // namespace chronon3d::text
} // namespace chronon3d
