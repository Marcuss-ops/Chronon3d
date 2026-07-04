// SPDX-License-Identifier: MIT
#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// src/text/resolver/font_resolver.hpp — INTERNAL
//
// M1.5#8 — the FontResolver service.  This is the SINGLE canonical font
// fallback entry point for src/text/.  All font resolution inside the
// text pipeline (resolve_text_run_tree → emit bidi runs) flows through
// `FontResolver::resolve(FontRequest)` — no second resolver exists in
// the backend or in text_resolver_helpers.
//
// Placement (Cat-3 freeze compliance): this header is STRICTLY internal
// under `src/text/resolver/`.  It is NOT installed under
// `include/chronon3d/`, NOT reachable by SDK consumers.  The user spec
// mentions the `FontResolutionResult FontResolver::resolve(const
// FontRequest&)` signature as the canonical one — that contract is met
// here, but in the internal namespace `chronon3d::text::resolver`.
//
// DECISION (thinker-with-files-gemini verdict M1.5#8 APPROVE-WITH-CHANGES):
//   1. New class types live INTERNAL — `include/chronon3d/text/text_resolver.hpp`
//      keeps `resolve_fallback_fonts` as a `[[deprecated]]` free function
//      whose impl in `text_font_resolver.cpp` delegates here.
//   2. `chrono3d::text::resolver::FontResolver` is the one and only font
//      fallback service.  No second resolver in the backend or in the
//      builder (architectural strong constraint from the user spec).
//   3. The `[[deprecated]]` `resolve_fallback_fonts` free function is
//      kept as a single-line forwarder, not a separate impl — it
//      delegates to `FontResolver::resolve` so the "un solo servizio"
//      promise stays true.

#include <chronon3d/text/font_engine.hpp>  // FontSpec, FontEngine

#include <cstddef>
#include <string>

namespace chronon3d::text::resolver {

// ── FontRequest — request to resolve a font through the fallback chain ───
//
// Carries the primary font spec to attempt and a flag distinguishing
// "fallback eagerly" (default) from "force exact path only" (kept for
// future API extensions — currently ignored by the fallback chain).

struct FontRequest {
    /// Primary font spec.  If engine.can_load returns true, returned
    /// unchanged.  Otherwise walked through the fallback chain.
    FontSpec primary;

    /// Hint flag — when true, the resolver prefers candidates that
    /// share at least the font_family with the primary spec.  Default
    /// is true (family-coherent fallback).
    bool prefer_family_coherent{true};

    /// Pointer to a matcher list of preferred fallback family names.
    /// When empty (the default), the resolver uses the canonical
    /// 5-tier chain (primary → family-only → sans-candidates →
    /// serif-candidates → primary as-is).  This pointer is borrowed;
    /// the caller's array must outlive the resolver call.
    const char* const* extra_family_candidates{nullptr};
    std::size_t        extra_family_candidates_count{0};
};

// ── FontResolutionResult — outcome of a font resolution attempt ───────────
//
// Stringifies the outcome in three states.  Loaded = primary was
// loadable, fallback was not used.  FellBack = primary was not loadable;
// resolved contains one of the fallback candidates.  Unresolved = primary
// was not loadable AND no candidate in the chain was loadable; resolved
// is set to the original primary as-is (caller must treat as invisible).

struct FontResolutionResult {
    enum class Status : unsigned char {
        Loaded     = 0,  // primary was loadable; resolved == primary
        FellBack   = 1,  // primary was NOT loadable; resolved is a fallback candidate
        Unresolved = 2,  // no candidate was loadable; resolved == primary
    };

    /// The resolved font spec (== primary if Status is Loaded or Unresolved).
    FontSpec resolved;

    /// Outcome code.
    Status   status{Status::Loaded};

    /// Index in the fallback chain when Status == FellBack:
    ///   0 = same-family (no path),
    ///   1 = sans-serif candidate,
    ///   2 = serif candidate,
    ///   3 = extra-family candidate (if supplied),
    ///  -1 = fell through (Unresolved).
    int       fallback_index{-1};

    /// Convenience predicate — true iff a fallback stepped in.
    [[nodiscard]] bool is_fallback() const noexcept {
        return status == Status::FellBack;
    }
};

// ── FontResolver — the one and only font fallback service ─────────────────
//
// Construction takes a reference to the live `FontEngine`.  Resolution
// uses the engine's `can_load(FontSpec)` capability check to short-circuit
// loading actual glyphs (no I/O at resolve time — the engine cache kicks
// in on the FIRST shape call only).  The resolver holds NO state across
// calls; it can be a stack-local in the orchestrator.
//
// Lifetime contract: engine must outlive the resolver call.  The resolver
// itself does not allocate beyond a single `FontResolutionResult`.

class FontResolver {
public:
    explicit FontResolver(FontEngine& engine) noexcept
        : engine_(engine) {}

    // Non-copyable (cheap stack instances preferred); movable trivially.
    FontResolver(const FontResolver&)            = delete;
    FontResolver& operator=(const FontResolver&) = delete;
    FontResolver(FontResolver&&) noexcept        = default;
    FontResolver& operator=(FontResolver&&) noexcept = default;

    /// Run the fallback chain on `req`.  Pure function (no I/O, no cache
    /// mutation).  Determinism: identical inputs + identical engine state
    /// produce identical outputs — this is the contract the golden test
    /// (tests/text/test_text_font_resolver_golden.cpp) locks.
    [[nodiscard]] FontResolutionResult resolve(const FontRequest& req) const;

private:
    FontEngine& engine_;
};

} // namespace chronon3d::text::resolver
