// ─── src/text/internal/text_resolver_helpers.cpp ────────────────────────────
//
// Companion .cpp for `src/text/internal/text_resolver_helpers.hpp`.
// Implements the 6 internal helper functions that USE the 7 domain type
// aliases declared in `src/text/aliases.hpp`:
//
//   1. validate_bcp47_language_tag
//   2. canonicalize_bcp47_language_tag
//   3. parse_font_weight
//   4. make_resolved_font_path
//   5. make_font_family_name
//   6. make_text_preset_id
//   7. make_selector_id
//
// Freeze compliance (AGENTS.md v0.1):
//   - Internal-only TU; not installed.
//   - Gate 5 deny-list compliance: no `<unicode/...>` or `<libtess2>`
//     includes anywhere in this TU.
//   - The aliases are bytewise-equivalent to their underlying primitives,
//     so the helpers can stay header-only in spirit; the .cpp is here
//     so the project can build-link the symbols without needing the
//     helper bodies inlined into every consumer TU.
//
// Anti-duplication rule: there is no public twin of these helpers —
// `AssetResolver::resolve_lexical(...)` in `include/chronon3d/` is the
// public production path, and these helpers are the internal smoke
// tests + canonicalization surface used by sibling internal TUs (e.g.,
// resolver scaffolding, registry-resolver TU).
// ─────────────────────────────────────────────────────────────────────────────

#include "src/text/internal/text_resolver_helpers.hpp"

#include <algorithm>   // std::any_of
#include <cctype>      // std::tolower
#include <cstring>     // std::memchr
#include <filesystem>  // std::filesystem::path::generic_string
#include <utility>     // std::move

namespace chronon3d {
namespace text {
namespace internal {

namespace {

// ── Smoke test for "well-formed-ish" BCP-47 tag ──────────────────────────────
//
// Accepts: non-empty, no embedded nulls, ASCII-only alphanumerics + '-'.
// This is NOT a full RFC 5646 ABNF parser — it's a smoke test to keep
// obviously-broken tags from reaching HarfBuzz.
[[nodiscard]] bool is_well_formed_bcp47(std::string_view tag) noexcept {
    if (tag.empty()) return false;
    if (std::memchr(tag.data(), '\0', tag.size()) != nullptr) return false;
    for (char ch : tag) {
        const bool is_alnum = (ch >= 'A' && ch <= 'Z') ||
                              (ch >= 'a' && ch <= 'z') ||
                              (ch >= '0' && ch <= '9');
        const bool is_sep   = (ch == '-');
        if (!is_alnum && !is_sep) return false;
    }
    // No leading or trailing separator.
    if (tag.front() == '-' || tag.back() == '-') return false;
    return true;
}

} // namespace

// ── 1. validate_bcp47_language_tag ─────────────────────────────────────────
bool validate_bcp47_language_tag(std::string_view tag) noexcept {
    return is_well_formed_bcp47(tag);
}

// ── 2. canonicalize_bcp47_language_tag ──────────────────────────────────────
Bcp47LanguageTag
canonicalize_bcp47_language_tag(std::string_view tag) {
    Bcp47LanguageTag out;
    out.reserve(tag.size());
    for (char ch : tag) {
        // BCP-47 canonical form: lower-case ASCII alphanumeric + '-'.
        // std::tolower's int parameter is intentional — tolower takes int
        // (and EOF), the cast is well-defined for char values 0..127.
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    return out;
}

// ── 3. parse_font_weight ───────────────────────────────────────────────────
std::optional<FontWeight> parse_font_weight(FontWeight w) noexcept {
    // OpenType / CSS weight band: 100..900 in steps of 100.  Reject 0 and
    // any value outside the canonical range.  Note: 0 is the project's
    // "unspecified" sentinel on `FontSpec::font_weight` (see
    // `include/chronon3d/text/font_engine.hpp`), so it must be rejected
    // here.
    if (w < 100 || w > 900) return std::nullopt;
    if (w % 100 != 0)       return std::nullopt;
    return w;
}

// ── 4. make_resolved_font_path ─────────────────────────────────────────────
ResolvedFontPath
make_resolved_font_path(FontAssetId asset_id, std::string_view assets_root) {
    // Compose `assets_root / asset_id` via std::filesystem for portable
    // path separators.  generic_string() flattens to forward slashes so
    // the result is consistent across Windows / POSIX builds.
    namespace fs = std::filesystem;
    const fs::path composed =
        fs::path{std::string{assets_root}} / fs::path{std::string{asset_id}};
    return composed.generic_string();
}

// ── 5. make_font_family_name ──────────────────────────────────────────────
FontFamilyName
make_font_family_name(std::string_view family_name) {
    // Trim leading + trailing ASCII whitespace; reject empty.
    std::size_t begin = 0;
    std::size_t end   = family_name.size();
    auto is_space = [](char ch) {
        return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f' || ch == '\v';
    };
    while (begin < end && is_space(family_name[begin])) ++begin;
    while (end > begin && is_space(family_name[end - 1])) --end;
    return FontFamilyName{family_name.substr(begin, end - begin)};
}

// ── 6. make_text_preset_id ────────────────────────────────────────────────
TextPresetId make_text_preset_id(std::string_view raw_id) {
    // Same trim rule as make_font_family_name.  Empty input → empty
    // TextPresetId; callers must validate before lookups.
    std::size_t begin = 0;
    std::size_t end   = raw_id.size();
    auto is_space = [](char ch) {
        return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f' || ch == '\v';
    };
    while (begin < end && is_space(raw_id[begin])) ++begin;
    while (end > begin && is_space(raw_id[end - 1])) --end;
    return TextPresetId{raw_id.substr(begin, end - begin)};
}

// ── 7. make_selector_id ───────────────────────────────────────────────────
SelectorId make_selector_id(std::string_view raw_id) {
    return make_text_preset_id(raw_id);  // identical canonicalization rule.
}

} // namespace internal
} // namespace text
} // namespace chronon3d
