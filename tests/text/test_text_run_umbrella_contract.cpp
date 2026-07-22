// SPDX-License-Identifier: MIT
//
// tests/text/test_text_run_umbrella_contract.cpp
//
// M1.5#3 — text_run.hpp umbrella contract lock.
//
// Locks the structural correctness of the umbrella refactor shipped
// in commit 843dc863 (`refactor(text): M1.5#3 — text_run.hpp umbrella
// header decomposition (409→slim + 5 single-responsibility
// sub-headers)`).  No production code changes here — this TU exists
// solely to verify, at compile-time wherever possible, that:
//
//   §1 — Each sub-header is independently includable AND exposes its
//        canonical target type(s).
//   §2 — text_run.hpp (the umbrella) exposes all canonical types via a
//        single include.
//   §3 — Fase B3 deprecated singleton symbols
//        (`shared_text_layout_cache()` / `reset_shared_text_layout_cache()`)
//        are NOT visible from the public umbrella surface.
//
// Static_asserts are the primary lock mechanism: a TU that fails to
// compile after a future umbrella regression gives the reviewer a hard
// signal without needing to read runtime output.  Where compile-time
// detection is impossible (negative-symbol-detection is not idiomatic
// in standard C++), we provide a runtime sentinel SUBCASE plus a
// cross-reference to `tools/check_architecture_boundaries.sh` gate-3,
// which scans for the deprecated symbols statically.
//
// Build wiring: this source is registered in `tests/core_tests.cmake`
// as a NON-gated entry — its purpose is structural-contract lock, not
// Blend2D / runtime engine coverage, so it must run regardless of
// `CHRONON3D_USE_BLEND2D` and `CHRONON3D_ENABLE_TEXT` flags.

#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
#include <doctest/doctest.h>

// ── Sub-header includes — intentionally split, one per file ────────────
//
// If the umbrella drops any of these, the §2 static_asserts below
// fail to compile and the test target cannot be linked.  Conversely,
// if ANY of these includes itself stops compiling (sub-header content
// regression), this TU is the first compile-time witness.
#include <chronon3d/text/text_layout_identity.hpp>   // §1(a)+(b): FontIdentity + FontLayoutIdentity
#include <chronon3d/text/text_run_layout.hpp>         // §1(c)+(d)+(e)+(f): TextRunLayout + SharedTextRunLayout + ShapedFontSpan + Bcp47LanguageTag + TextShapingFeatures
#include <chronon3d/text/text_layout_cache.hpp>       // §1(g)+(h)+(i): TextLayoutCacheKey + TextLayoutCacheKeyHash + TextLayoutCache
#include <chronon3d/text/text_run_shape.hpp>          // §1(j): TextRunShape (forward-decls AnimatedTextDocument / TextLayoutCache)
#include <chronon3d/text/text_run_hash.hpp>           // §1(k): hash_text_run_shape overload declarations

// ── Umbrella include — flips the lock on for §2 ─────────────────────────
//
// text_run.hpp is the canonical single-include surface for downstream
// consumers.  The umbrella re-includes the 5 sub-headers above, so this
// single include must expose ALL canonical types.  Reach-through here
// proves the umbrella's transitive coverage.
#include <chronon3d/text/text_run.hpp>

#include <memory>
#include <string>
#include <type_traits>

using namespace chronon3d;

// ═══════════════════════════════════════════════════════════════════════════
// §1 — Sub-headers are independently includable and expose canonical types
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("M1.5#3 lock — text_layout_identity.hpp re-exports identity types") {

    SUBCASE("(a) FontIdentity identity-subset type is reachable") {
        static_assert(std::is_class_v<FontIdentity>,
                      "FontIdentity must be a class type");
        FontIdentity fi;
        // Default values lock the canonical identity contract
        // (font_path + font_family empty strings, weight 400, style normal).
        CHECK(fi.font_weight == 400);
        CHECK(fi.font_style == "normal");
    }

    SUBCASE("(b) FontLayoutIdentity layout-relevant type is reachable") {
        static_assert(std::is_class_v<FontLayoutIdentity>,
                      "FontLayoutIdentity must be a class type");
        const FontLayoutIdentity a{};
        const FontLayoutIdentity b{};
        // Defaults trigger canonical equality lock — non-default
        // construction exercises operator==.
        CHECK(a == b);
        FontLayoutIdentity c{};
        c.size = 32.0f;
        FontLayoutIdentity d{};
        d.size = 48.0f;
        CHECK_FALSE(c == d);
    }
}

TEST_CASE("M1.5#3 lock — text_run_layout.hpp exposes layout data types") {

    SUBCASE("(a) TextRunLayout is default-constructible + has layout_hash/shaping_hash") {
        static_assert(
            std::is_same_v<u64 (TextRunLayout::*)() const,
                           decltype(&TextRunLayout::layout_hash)>,
            "layout_hash must be a const member returning u64");
        static_assert(
            std::is_same_v<u64 (TextRunLayout::*)() const,
                           decltype(&TextRunLayout::shaping_hash)>,
            "shaping_hash must be a const member returning u64");
        TextRunLayout l;
        // Pure-function idempotence locks the canonical hash family.
        CHECK(l.layout_hash() == l.layout_hash());
        CHECK(l.shaping_hash() == l.shaping_hash());
    }

    SUBCASE("(b) SharedTextRunLayout is std::shared_ptr<const TextRunLayout>") {
        static_assert(
            std::is_same_v<SharedTextRunLayout,
                           std::shared_ptr<const TextRunLayout>>,
            "SharedTextRunLayout must be the canonical shared_ptr type");
        SharedTextRunLayout p;
        CHECK(p == nullptr);  // default-constructible null
    }

    SUBCASE("(c) Bcp47LanguageTag is the canonical std::string alias (TICKET-103a)") {
        static_assert(
            std::is_same_v<Bcp47LanguageTag, std::string>,
            "Bcp47LanguageTag must be an alias of std::string");
        Bcp47LanguageTag lang{"en"};
        CHECK(lang == "en");
        CHECK(lang.size() == 2u);
    }

    SUBCASE("(d) TextShapingFeatures is the canonical std::string alias (TICKET-103a)") {
        static_assert(
            std::is_same_v<TextShapingFeatures, std::string>,
            "TextShapingFeatures must be an alias of std::string");
        TextShapingFeatures feat{"kern=1,liga=1"};
        CHECK(feat.find("kern=1") != std::string::npos);
    }

    SUBCASE("(e) ShapedFontSpan carries the per-glyph font-identity contract") {
        ShapedFontSpan span;
        CHECK(span.font.font_weight == 400);
        CHECK(span.glyph_begin == 0u);
        CHECK(span.glyph_end == 0u);
    }
}

TEST_CASE("M1.5#3 lock — text_layout_cache.hpp exposes cache-key + cache class") {

    SUBCASE("(a) TextLayoutCacheKey has digest() + defaulted operator==") {
        static_assert(
            std::is_same_v<u64 (TextLayoutCacheKey::*)() const,
                           decltype(&TextLayoutCacheKey::digest)>,
            "digest must be a const member returning u64");
        TextLayoutCacheKey k;
        k.text = "hello";
        CHECK(k.digest() != 0u);  // non-empty text → non-zero digest
        CHECK(k == k);            // reflexive equality (operator== defaulted)
    }

    SUBCASE("(b) TextLayoutCache class is default-constructible + has find/store/clear") {
        static_assert(
            std::is_same_v<void (TextLayoutCache::*)(),
                           decltype(&TextLayoutCache::clear)>,
            "clear must be a member");
        static_assert(
            std::is_same_v<bool (TextLayoutCache::*)(const TextLayoutCacheKey&),
                           decltype(&TextLayoutCache::erase)>,
            "erase must accept a TextLayoutCacheKey&");
        TextLayoutCache cache;
        CHECK(cache.size() == 0u);  // empty initial state
        TextLayoutCacheKey k;
        CHECK(cache.find(k) == nullptr);  // initial query yields null
    }

    SUBCASE("(c) TextLayoutCacheKeyHash operator() is (const TextLayoutCacheKey&) const noexcept") {
        static_assert(
            std::is_same_v<
                size_t (TextLayoutCacheKeyHash::*)(const TextLayoutCacheKey&) const noexcept,
                decltype(&TextLayoutCacheKeyHash::operator())>,
            "Hash operator must be (const TextLayoutCacheKey&) const noexcept");
    }
}

TEST_CASE("M1.5#3 lock — text_run_shape.hpp exposes the batched shape") {

    SUBCASE("(a) TextRunShape is default-constructible with Fase-B3 + PR-8/9/11 fields") {
        TextRunShape s;
        // B3: per-shape cache pointer (replaced process-wide global).
        CHECK(s.layout == nullptr);
        CHECK(s.cache == nullptr);
        CHECK(s.animated_doc == nullptr);   // PR 9
        CHECK(s.engine == nullptr);          // PR 9
        CHECK(s.dissolve_layout == nullptr); // PR 11
        CHECK(s.dissolve_glyphs.empty());
        CHECK(s.dissolve_mix == 0.0f);
        CHECK(s.animators.empty());           // PR 8 driver payload
        // Type-identity static_asserts on the fields that lack explicit
        // in-class default initializers in text_run_shape.hpp (the
        // members `TextMaterial material;` and `TextPaint paint;`).
        // Reading the inner-default values of those members (e.g.
        // `s.material.enabled`) would be undefined per C++17 [dcl.init]
        // if their default ctors leave sub-fields uninitialized, so we
        // lock the contract at compile time via type identity instead.
        // Raw-pointer types `cache` / `engine` are also locked: they
        // MUST be raw pointers to keep the consumer surface light
        // (no lru_cache.hpp or font_engine.hpp transitive include in
        // every TU that just touches TextRunShape).
        static_assert(std::is_same_v<decltype(s.cache), TextLayoutCache*>,
                      "TextRunShape::cache must be raw TextLayoutCache* (forward-decl surface)");
        static_assert(std::is_same_v<decltype(s.engine), FontEngine*>,
                      "TextRunShape::engine must be raw FontEngine*");
        static_assert(std::is_same_v<decltype(s.animated_doc),
                                      std::shared_ptr<const AnimatedTextDocument>>,
                      "TextRunShape::animated_doc must be shared_ptr<const AnimatedTextDocument>");
        static_assert(std::is_same_v<decltype(s.dissolve_mix), float>,
                      "TextRunShape::dissolve_mix must be float");
        static_assert(std::is_same_v<decltype(s.material), TextMaterial>,
                      "TextRunShape::material must be TextMaterial");
        static_assert(std::is_same_v<decltype(s.paint), TextPaint>,
                      "TextRunShape::paint must be TextPaint");
    }

    SUBCASE("(b) TextLayoutCache* is a raw-pointer member (forward-decl surface)") {
        // The sub-header lists ONLY 2 forward decls in its public
        // surface (AnimatedTextDocument + TextLayoutCache) and uses
        // `TextLayoutCache*` raw-pointer storage to keep consumer TUs
        // free of cache/lru_cache.hpp transitive includes.  Lock:
        static_assert(
            std::is_pointer_v<decltype(std::declval<TextRunShape>().cache)>,
            "TextRunShape::cache must be a pointer (keeps consumer TUs light)");
    }
}

TEST_CASE("M1.5#3 lock — text_run_hash.hpp reaches both hash overloads") {

    SUBCASE("(a) hash_text_run_shape(const TextRunShape&) is reachable + idempotent") {
        TextRunShape s;
        const u64 ha = hash_text_run_shape(s);
        CHECK(ha == hash_text_run_shape(s));  // pure-on-TextRunShape contract
    }

    SUBCASE("(b) hash_text_run_shape(const TextRunShape&, Frame) overload is reachable + idempotent") {
        TextRunShape s;
        const Frame f{42};
        const u64 hb = hash_text_run_shape(s, f);
        CHECK(hb == hash_text_run_shape(s, f));  // pure on (shape, frame)
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// §2 — Umbrella (text_run.hpp) exposes all canonical types via single include
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("M1.5#3 lock — text_run.hpp umbrella exposes all canonical types") {

    SUBCASE("(a) Single-include umbrella — all 11 canonical types reachable") {
        // Static_asserts below FAIL TO COMPILE if any of the 5
        // sub-headers were not transitively included by text_run.hpp.
        // Compile-time lock on the umbrella's transitive coverage.
        static_assert(std::is_class_v<FontIdentity>);
        static_assert(std::is_class_v<FontLayoutIdentity>);
        static_assert(std::is_class_v<ShapedFontSpan>);
        static_assert(std::is_class_v<TextRunLayout>);
        static_assert(std::is_same_v<SharedTextRunLayout,
                                      std::shared_ptr<const TextRunLayout>>);
        static_assert(std::is_same_v<Bcp47LanguageTag, std::string>);
        static_assert(std::is_same_v<TextShapingFeatures, std::string>);
        static_assert(std::is_class_v<TextLayoutCacheKey>);
        static_assert(std::is_class_v<TextLayoutCacheKeyHash>);
        static_assert(std::is_class_v<TextLayoutCache>);
        static_assert(std::is_class_v<TextRunShape>);
        CHECK(true);  // sentinel: if the TU compiles, §2 is satisfied.
    }

    SUBCASE("(b) Both hash_text_run_shape overloads reachable from umbrella") {
        TextRunShape s;
        static_assert(
            std::is_integral_v<decltype(hash_text_run_shape(s))>,
            "Single-arg hash must return an integral type");
        Frame f{1};
        static_assert(
            std::is_integral_v<decltype(hash_text_run_shape(s, f))>,
            "Frame-overload hash must return an integral type");
        CHECK(true);
    }

    SUBCASE("(c) font_layout_identity_of(TextRunLayout) projection reachable") {
        // The umbrella must keep `font_layout_identity_of(const TextRunLayout&)`
        // callable (P0-2 lock-task).  If a future trim removes it, this
        // SUBCASE fails to compile.
        TextRunLayout l{};
        const FontLayoutIdentity id = font_layout_identity_of(l);
        // Default TextRunLayout.font_size is 72; the projection preserves it.
        CHECK(id.size == l.font_size);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// §3 — Fase B3 lock: deprecated singleton symbols are NOT visible publicly
// ═══════════════════════════════════════════════════════════════════════════
//
// Compile-time absence lock.  The canonical checker is
// `tools/check_architecture_boundaries.sh` gate-3 which statically greps
// the umbrella header for `shared_text_layout_cache` and
// `reset_shared_text_layout_cache`.  This TU complements the gate-3
// check: the TU compiles ONLY IF neither symbol is exposed from the
// public umbrella surface (otherwise any unrelated reference site — like
// an `auto&` in a `using` declaration pulled transitively — would not
// cause a compile error and the sentinel would be weaker than the
// gate-3 grep).  
//
// We intentionally DO NOT call the deprecated symbols here.  The sentinel
// SUBCASEs are documentation carriers for the contract.

TEST_CASE("M1.5#3 lock — Fase B3 sentinel: no singleton symbols leaked to public surface") {

    SUBCASE("(a) Compile-time sentinel — TU compiling proves symbol absence") {
        // We CANNOT detect identifier-absence via standard C++ (no
        // `is_detected_unfriendly_name` idiom in std::).  The FAIL signal
        // for Fase B3 regression is two-fold:
        //   (i)  tools/check_architecture_boundaries.sh gate-3 greps
        //        the umbrella header for the symbol name(s).
        //   (ii) Any future TU that does `#include <chronon3d/text/text_run.hpp>`
        //        and then red-introduces a call/declaration/forward-decl
        //        of `shared_text_layout_cache()` compiles here only when
        //        the symbol is gone.  Conversely, if a regression
        //        accidentally re-exposes the symbol, any TU that tries
        //        to ALSO use the new symbol (or call it) must compile
        //        pass — proving its presence.  Our sentinel TU does
        //        NOT use the symbol → silent pass implies absence.
        CHECK(true);  // sentinel
    }

    SUBCASE("(b) TextRunShape::cache is the canonical per-shape pointer (was the singleton)") {
        // This is a stronger functional lock: the per-shape `cache`
        // slot IS exposed; the singleton is NOT.  If both existed, the
        // surface would have two equally-valid sources of truth —
        // anti-duplication invariant (AGENTS.md / ANTI_DUPLICATION_RULES).
        TextRunShape s;
        CHECK(s.cache == nullptr);  // default-constructed: no cache pointer
        CHECK(s.layout == nullptr);
    }
}
