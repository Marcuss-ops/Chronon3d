// ─── src/text/internal/text_resolver_helpers.hpp ────────────────────────────
//
// FASE 5 reviewer-fixup (TICKET-108a) — adopt the 7 internal domain type
// aliases declared in `src/text/aliases.hpp` (FontAssetId, ResolvedFontPath,
// FontFamilyName, Bcp47LanguageTag, TextPresetId, SelectorId, FontWeight)
// into a small set of internal helper functions so the typedefs have
// actual callers in `src/`.
//
// The helpers are pure functions over the domain primitives (range check,
// canonicalize, validate).  They live under `src/text/internal/` and are
// NOT installed.  No public API surface in `include/chronon3d/` is touched.
//
// Freeze compliance (AGENTS.md v0.1):
//   - Internal-only header, no new public classes.
//   - Gate 5 deny-list compliance: no `<unicode/...>` or `<libtess2>`
//     includes anywhere in this header or its .cpp companion.
//   - The aliases resolve to the SAME bytewise layout as their underlying
//     types (`std::string`, `std::string_view`, `int`) — promotion to
//     `include/chronon3d/text/` (TICKET-101, deferred) is reserved for
//     the post-baseline-verde freeze revocation.
//
// Usage (internal-only):
//     #include "src/text/internal/text_resolver_helpers.hpp"
//     using namespace chronon3d::text::internal;
//     if (validate_bcp47_language_tag(canonicalize_bcp47_tag("en-US"))) { ... }
//     auto weight = parse_font_weight(400);    // FontWeight in [100, 900]
// ─────────────────────────────────────────────────────────────────────────────

#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "src/text/aliases.hpp"   // TICKET-099a — internal domain type aliases.

namespace chronon3d {
namespace text {
namespace internal {

// ── BCP-47 language tag helpers ────────────────────────────────────────────
//
// RFC 5646 language tags (e.g. "en-US", "ar", "zh-Hant-HK").  Pass through
// to HarfBuzz `hb_language_from_string(...)` unchanged.  These helpers
// are pure-validators + canonicalizers — they do NOT parse the tag into
// sub-components (that's a far heavier undertaking reserved for a
// post-baseline-verde ADR).
//
// `validate_bcp47_language_tag` returns true when the tag is non-empty,
// contains no embedded null bytes, and has at most a single `-` per
// sub-component.  Not a full RFC 5646 ABNF — just a smoke test.
//
// `canonicalize_bcp47_language_tag` lower-cases the input (BCP-47
// canonical form is case-insensitive, lower-cased) and returns a
// Bcp47LanguageTag value.  Empty / whitespace-only inputs return an
// empty string; callers must validate before passing to HarfBuzz.
[[nodiscard]] bool validate_bcp47_language_tag(std::string_view tag) noexcept;

[[nodiscard]] Bcp47LanguageTag
canonicalize_bcp47_language_tag(std::string_view tag);

// ── Font weight helpers ────────────────────────────────────────────────────
//
// OpenType-compatible font weight (100..900, regular = 400, bold = 700).
// Matches the existing `FontSpec::font_weight` field in
// `include/chronon3d/text/font_engine.hpp`.  `parse_font_weight` accepts
// any FontWeight (int) and returns the value if it falls in the canonical
// OpenType range, std::nullopt otherwise.
[[nodiscard]] std::optional<FontWeight> parse_font_weight(FontWeight w) noexcept;

// ── Font asset identity helpers ─────────────────────────────────────────────
//
// `make_resolved_font_path` composes a ResolvedFontPath (a std::string)
// from a logical FontAssetId (a std::string_view) and an assets-root
// prefix.  This is the *internal* counterpart to the public
// `AssetResolver::resolve_lexical(...)` — used by sibling internal TUs
// that need a canonical resolved-path string without taking on the
// full AssetResolver surface (which lives in `include/chronon3d/`).
[[nodiscard]] ResolvedFontPath
make_resolved_font_path(FontAssetId asset_id, std::string_view assets_root);

[[nodiscard]] FontFamilyName
make_font_family_name(std::string_view family_name);

// ── Preset / selector identity helpers ─────────────────────────────────────
//
// Canonicalize (trim leading/trailing whitespace, reject empty inputs)
// into the typed PresetId / SelectorId handles.  Empty inputs return
// empty strings; callers should validate before use.
[[nodiscard]] TextPresetId make_text_preset_id(std::string_view raw_id);
[[nodiscard]] SelectorId   make_selector_id   (std::string_view raw_id);

} // namespace internal
} // namespace text
} // namespace chronon3d
