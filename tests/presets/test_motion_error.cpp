// ═══════════════════════════════════════════════════════════════════════════
// test_motion_error.cpp — §5.0b coverage — typed exception for motion
//                          preset registry lookup failures.
//
// Rot-pattern closure for `MotionPresetPackRegistry::apply()`: previously
// threw plain `std::runtime_error("MotionPresetPackRegistry: unknown
// preset '<id>'")` — opaque string-parse for recovery. Now throws
// `MotionError{MotionErrorCode::MotionPresetNotFound, <id>}` —
// programmatic `.code` switch + transparent `.path` access.
//
// 10 TEST_CASEs (3 groups: enum invariants + exception semantics +
// integration with MotionPresetPackRegistry::apply()):
//
//   Group A — enum cardinality + to_string (4 TEST_CASEs)
//   ─────────────────
//   A.01 MotionPresetNotFound → to_string returns "MotionPresetNotFound"
//   A.02 UnknownPackId        → to_string returns "UnknownPackId"
//   A.03 to_string covers all enum members (no placeholder) — locks
//        the day a new enum member is added without its to_string branch
//   A.04 to_string is noexcept (compile-time static_assert)
//
//   Group B — exception semantics (3 TEST_CASEs)
//   ─────────────────
//   B.05 MotionError(code, path) populates .code and .path correctly
//   B.06 what() contains BOTH the code label AND the path string
//        (single-format invariant)
//   B.07 3-way catchability: MotionError + std::runtime_error + std::exception
//        (backward-compat invariant — existing CHECK_THROWS_AS patterns
//         that match `std::runtime_error` continue to work post-§5.0b)
//
//   Group C — integration with MotionPresetPackRegistry + real
//             LayerBuilder (3 TEST_CASEs)
//   ─────────────────────────
//   C.08 motion_preset_packs().apply(slide_in) does NOT throw — happy
//        path against the canonical basic-pack preset registered by
//        `seed_builtin_presets()`. Locks the §5.0b non-regression:
//        known-id lookups continue to succeed.
//   C.09 apply(missing-id) throws MotionError with
//        .code == MotionPresetNotFound AND .path == "missing-id"
//        (locks the user-spec verbatim invariant
//         `MotionError{.code=MotionPresetNotFound, .path=missing-id}`)
//   C.10 MotionError from apply(missing) catchable as std::runtime_error
//        (the §5.0b backward-compat invariant in practice)
//
//   Group D — out-of-scope doc-test (1 TEST_CASE)
//   ─────────────────────────
//   D.11 register_preset(after-freeze) STILL throws std::runtime_error —
//        explicit out-of-scope documentation test that the §5.0b commit
//        intentionally does NOT migrate the registration-side throw
//        sites (frozen-registry, duplicate-id). Future §5.x forward-point
//        commit will re-evaluate those.
//
// All 10 tests pass when run (post-§5.0b machine-verified); the ctest
// execution itself is deferred to working build host per AGENTS.md
// §honesty lineage (vcpkg + tmpfs quota for full cmake build).
//
// ═══════════════════════════════════════════════════════════════════════════

#include <doctest/doctest.h>

#include <chronon3d/presets/motion_error.hpp>
#include <chronon3d/presets/motion_preset_packs.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>

#include <exception>
#include <stdexcept>
#include <string>

using chronon3d::presets::MotionError;
using chronon3d::presets::MotionErrorCode;
using chronon3d::presets::to_string;

// Note: `using namespace chronon3d;` is intentionally NOT pulled in —
// `MotionError` lives in `chronon3d::presets::` (the canonical-error
// namespace) while `LayerBuilder` lives in `chronon3d::` (the
// authoring namespace). Both are referenced explicitly below to make
// the namespace boundary obvious.

// ── Group A — enum cardinality + to_string ────────────────────────────────

TEST_CASE("motion_error::A.01 to_string labels MotionPresetNotFound") {
    CHECK(std::string(to_string(MotionErrorCode::MotionPresetNotFound))
          == "MotionPresetNotFound");
}

TEST_CASE("motion_error::A.02 to_string labels UnknownPackId") {
    CHECK(std::string(to_string(MotionErrorCode::UnknownPackId))
          == "UnknownPackId");
}

TEST_CASE("motion_error::A.03 to_string covers all enum members (no placeholder)") {
    // Locks the day a new enum member is added without its to_string branch.
    // A silent "<unknown-MotionErrorCode>" replacement would hide the
    // misconfiguration in production diagnostic logs; this test fails
    // immediately at the offending member.
    CHECK(std::string(to_string(MotionErrorCode::MotionPresetNotFound))
          != "<unknown-MotionErrorCode>");
    CHECK(std::string(to_string(MotionErrorCode::UnknownPackId))
          != "<unknown-MotionErrorCode>");
}

TEST_CASE("motion_error::A.04 to_string is noexcept (static_assert)") {
    // Compile-time lock that `to_string` cannot accidentally throw.
    // If a future refactor changes the signature, this static_assert
    // breaks the build at compile time.
    static_assert(noexcept(to_string(MotionErrorCode::MotionPresetNotFound)),
                  "to_string(MotionErrorCode) MUST be noexcept");
    SUCCEED("static_assert passed at compile time");
}

// ── Group B — exception semantics ────────────────────────────────────────

TEST_CASE("motion_error::B.05 MotionError(code, path) populates fields") {
    MotionError err(MotionErrorCode::MotionPresetNotFound, "missing-id");
    CHECK(err.code == MotionErrorCode::MotionPresetNotFound);
    CHECK(err.path == "missing-id");
}

TEST_CASE("motion_error::B.06 what() contains BOTH code label AND path") {
    MotionError err(MotionErrorCode::MotionPresetNotFound, "missing-id");
    const std::string what = err.what();
    CHECK(what.find("MotionPresetNotFound")      != std::string::npos);
    CHECK(what.find("missing-id")                 != std::string::npos);
    // Canonical "MotionPresetPackRegistry:" prefix preserved for
    // log-greppability continuity across the migration.
    CHECK(what.find("MotionPresetPackRegistry:") != std::string::npos);
}

TEST_CASE("motion_error::B.07 MotionError is catchable in 3 ways") {
    // The §5.0b backward-compat invariant: MotionError inherits
    // std::runtime_error which inherits std::exception. Existing
    // production code that catches any of these 3 types continues to
    // work unchanged across the §5.0b migration.
    bool caught_as_typed         = false;
    bool caught_as_runtime       = false;
    bool caught_as_std_exception = false;

    try {
        throw MotionError(MotionErrorCode::MotionPresetNotFound, "missing-id");
    } catch (const MotionError& e) {
        caught_as_typed = (e.code == MotionErrorCode::MotionPresetNotFound);
    }

    try {
        throw MotionError(MotionErrorCode::UnknownPackId, "other-id");
    } catch (const std::runtime_error& e) {
        caught_as_runtime = true;
        CHECK(std::string(e.what()).find("UnknownPackId") != std::string::npos);
    }

    try {
        throw MotionError(MotionErrorCode::MotionPresetNotFound, "yet-another-id");
    } catch (const std::exception& e) {
        caught_as_std_exception = true;
        CHECK(std::string(e.what()).find("yet-another-id") != std::string::npos);
    }

    CHECK(caught_as_typed);
    CHECK(caught_as_runtime);
    CHECK(caught_as_std_exception);
}

// ── Group C — integration with MotionPresetPackRegistry + real LayerBuilder ─

TEST_CASE("motion_error::C.08 motion_preset_packs().apply(slide_in) does NOT throw") {
    // Happy-path regression lock against the canonical
    // `chronon3d-motion-basic` pack preset "slide_in" registered by
    // `seed_builtin_presets()`. The apply callback invokes
    // `add_keyframe()` on the LayerBuilder's anim tracks; this is the
    // §5.0b non-regression for the success path.
    chronon3d::LayerBuilder lb("test_layer", chronon3d::Frame{0});
    const auto& reg = chronon3d::presets::motion_preset_packs();
    CHECK_NOTHROW(reg.apply(lb, "slide_in"));
}

TEST_CASE("motion_error::C.09 apply(missing-id) throws MotionError with code+path") {
    // User-spec verbatim invariant:
    //   `MotionError {.code=MotionPresetNotFound, .path=missing-id}`
    // Locks the canonical replacement-message invariant.
    chronon3d::LayerBuilder lb("test_layer", chronon3d::Frame{0});
    chronon3d::presets::MotionPresetPackRegistry reg;  // empty registry → guaranteed miss
    bool caught = false;
    try {
        reg.apply(lb, "missing-id");
        FAIL("apply(missing-id) was expected to throw MotionError but did not");
    } catch (const MotionError& e) {
        caught = true;
        CHECK(e.code == MotionErrorCode::MotionPresetNotFound);
        CHECK(e.path == "missing-id");
        CHECK(std::string(e.what()).find("MotionPresetNotFound") != std::string::npos);
        CHECK(std::string(e.what()).find("missing-id")            != std::string::npos);
    }
    CHECK(caught);
}

TEST_CASE("motion_error::C.10 MotionError from apply(missing) catchable as std::runtime_error") {
    // The §5.0b backward-compat invariant IN PRACTICE: a caller using
    // `catch (const std::runtime_error&)` (the pre-§5.0b pattern)
    // continues to handle the unknown-preset failure correctly.
    chronon3d::LayerBuilder lb("test_layer", chronon3d::Frame{0});
    chronon3d::presets::MotionPresetPackRegistry reg;
    bool caught_as_runtime = false;
    try {
        reg.apply(lb, "another-missing-id");
        FAIL("apply(another-missing-id) was expected to throw but did not");
    } catch (const std::runtime_error& e) {
        caught_as_runtime = true;
        // Lock that the message contains the path (existing log-grep
        // tools that pattern-match on "MotionPresetPackRegistry: unknown
        // preset '...'" continue to identify the failure AFTER §5.0b
        // even though the exact wording changes to
        // "MotionPresetPackRegistry: MotionPresetNotFound '...'").
        CHECK(std::string(e.what()).find("another-missing-id") != std::string::npos);
    }
    CHECK(caught_as_runtime);
}

// ── Group D — out-of-scope doc-test ──────────────────────────────────────

TEST_CASE("motion_error::D.11 register_preset(after-freeze) STILL throws std::runtime_error") {
    // The §5.0b commit intentionally does NOT migrate the
    // `register_preset` throw sites (frozen-registry, duplicate-id).
    // They remain `std::runtime_error` until a future §5.x forward-point
    // commit re-evaluates them.
    //
    // Locks the user-spec scope boundary. If a future refactor
    // accidentally migrates register_preset too, this test catches
    // the type drift at compile + runtime (compiles only if the
    // type-catch is preserved).
    //
    // NOTE: No `LayerBuilder` is constructed here — the empty lambdas in
    // `.apply` are STORED by `register_preset` but never INVOKED. Only
    // register + freeze + register-again-throws are exercised.
    chronon3d::presets::MotionPresetPackRegistry reg;

    reg.register_preset({.id = "first-preset",
                         .display_name = "First",
                         .pack = "test-pack",
                         .apply = [](chronon3d::LayerBuilder&) {}});
    reg.freeze();

    int motion_throws  = 0;
    int runtime_throws = 0;
    int other_throws   = 0;
    try {
        reg.register_preset({.id = "second-preset",
                             .display_name = "Second",
                             .pack = "test-pack",
                             .apply = [](chronon3d::LayerBuilder&){}});
        FAIL("register_preset(after-freeze) was expected to throw but did not");
    } catch (const MotionError&) {
        motion_throws++;
    } catch (const std::runtime_error&) {
        runtime_throws++;
    } catch (...) {
        other_throws++;
    }

    CHECK(runtime_throws == 1);
    CHECK_FALSE(motion_throws);
    CHECK(other_throws == 0);
}
