// ===========================================================================
// tests/text/test_anim_typewriter_error_path.cpp
//
// Azione 18 — regression lock for the silent-failure fix in
// `content/animation_compositions.cpp::anim_typewriter()` (P0 #3
// closed gap, see animation_compositions.cpp:98-103 for the canonical
// spdlog::error emit).
//
// Strategy: cat-2 freeze-compliant static-source grep lock.  If the
// canonical contiguous `"[AnimTypewriter] typewriter_build failed:`
// substring disappears from the production source — OR the silent
// degrade pattern re-emerges in its place — this TEST_CASE fails loud.
// No font engine, no threads, no time, no PRNG, no SoftwareRenderer
// pipeline dependency.
//
// Tightened per code-review: a single multi-token substring anchor
// reproducing P0 #3's exact contiguous emit form replaces the
// previous 4-tuple loose grep.  Drift where `spdlog::error` survives
// elsewhere but the AnimTypewriter-specific path silently regresses
// can no longer pass the lock.
// ===========================================================================

#include <doctest/doctest.h>

#include <fstream>
#include <sstream>
#include <string>

// Azione 18 — production-source absolute path is injected at compile
// time via target_compile_definitions (CONTENT_ANIMATION_COMPOSITIONS_PATH
// = ${CMAKE_SOURCE_DIR}/content/animation_compositions.cpp).  This
// makes the test CWD-independent: ctest may run from
// build/<preset>/tests/, not the project root.  The #error guard
// surfaces a misconfigured target at compile time rather than
// silently producing an empty-src = find_npos = false-positive
// failure at runtime.
#ifndef CONTENT_ANIMATION_COMPOSITIONS_PATH
#error "CONTENT_ANIMATION_COMPOSITIONS_PATH must be defined by CMake (target_compile_definitions)."
#endif

namespace {

// Slurp a source file at an absolute path.  Robust to build-preset
// CWD: the path is injected at compile time.
[[nodiscard]] std::string slurp(const char* absolute_path) {
    std::ifstream f(absolute_path);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

}  // namespace

// ─── Azione 18 — AnimTypewriter silent-failure static-source lock ─────────

TEST_CASE("Azione 18: content/animation_compositions.cpp emits the contiguous [AnimTypewriter] spdlog::error block") {
    const std::string src = slurp(CONTENT_ANIMATION_COMPOSITIONS_PATH);

    // The exact contiguous shape P0 #3 introduces in
    // content/animation_compositions.cpp:100.  Raw-string literal:
    // escapes NOT processed — the literal ASCII space (U+0020)
    // between `[` and `AnimTypewriter]` is preserved byte-exact to
    // match the production source.
    constexpr const char kCanonicalEmit[] = R"("[AnimTypewriter] typewriter_build failed:)";
    CHECK(src.find(kCanonicalEmit) != std::string::npos);

    // Belt-and-suspenders inverse-assert against the silent-degrade
    // rot pattern (user-spec verbatim).
    CHECK(src.find("// silent degrade") == std::string::npos);
}
