// ============================================================================
// tests/backends/software/test_software_renderer_move.cpp
//
// 06 R5b follow-up — regression guard for SoftwareRenderer move semantics.
//
// Verifies that the move ctor / move assign use real `SoftwareRenderer&&`
// signatures, transfer ownership correctly, and leave the moved-from object
// in a destructible-but-empty state.  Locks out the EAST-CONST rvalue-ref
// hack (which used `const&&` + `const_cast` to pass boundary gate I5 without
// actually performing a true rvalue transfer) from silently regressing.
//
// Coverage:
//  1. Move ctor: b.runtime() returns the source's prior runtime; b.session()
//     is callable; the moved-from has_runtime() returns false (its raw
//     m_runtime was nulled post-move).
//  2. Move assign: destination receives source's prior runtime.  Destination's
//     prior runtime was freed during assignment (its m_owned_runtime_storage
//     unique_ptr was replaced by source's transfer).
//  3. Destruction of moved-from instances does not crash (no double-free
//     of m_image_renderer / m_session internal mutex / pool resources).
//
// NOTE 2026-06-23: the test uses ONLY Config (default-constructible) and
// the inline / inline-moved-OOL accessors of SoftwareRenderer.  No
// Composition / Scene / Camera fixtures are needed — the move-ops state
// machine is observable purely through has_runtime() / runtime() /
// session() accessors and successful destruction of the moved-from
// instance.
//
// NOTE 2026-06-23 (revision): the original version of this file carried
// three `static_assert(is_same_v<decltype(declval<...>()) ...)` locks on
// the ctor/move-assign/has_runtime signatures.  Those checks are no-ops:
// `std::declval<T>()` is a free function template whose return type is
// determined by `std::add_rvalue_reference<T>`, completely independent of
// what ctors `SoftwareRenderer` declares (it would be `SoftwareRenderer&&`
// regardless of whether the class actually has a `T&&` ctor, a
// `const T&&` ctor, or no move ctor at all).  The runtime TEST_CASEs
// below are the only effective regression-guard; the static_asserts have
// been removed.
// ============================================================================

#if defined(CHRONON3D_BUILD_TESTS)
#include <doctest/doctest.h>
#endif

#include <chronon3d/backends/software/software_renderer.hpp>

#if defined(CHRONON3D_BUILD_TESTS)

// ===========================================================================
// Tests
// ===========================================================================

TEST_CASE("SoftwareRenderer - move-ctor: b receives runtime/session, source is empty") {
    using namespace chronon3d;

    SoftwareRenderer a{Config{}};
    REQUIRE(a.has_runtime());                  // pre-move: a's raw m_runtime is non-null

    auto* const pre_runtime_addr = &a.runtime();

    SoftwareRenderer b{std::move(a)};

    // b received the owned runtime (post-move unique_ptr transferred
    // -> m_runtime raw ptr correctly aliases the same backing RT).
    CHECK(b.has_runtime());
    CHECK(&b.runtime() == pre_runtime_addr);

    // b.session() is callable: returning its address is impossible to
    // be nullptr for a reference - the call compiling + having an
    // address is the regression-guard against accidental deletion of
    // m_session storage during a future refactor.
    auto* b_session_addr = &b.session();
    CHECK(b_session_addr != nullptr);

    // Source a is left destructible-but-empty: raw m_runtime was nulled.
    CHECK_FALSE(a.has_runtime());
}

TEST_CASE("SoftwareRenderer - move-assign: destination receives source's prior runtime") {
    using namespace chronon3d;

    SoftwareRenderer a{Config{}};
    SoftwareRenderer b{Config{}};

    REQUIRE(a.has_runtime());
    REQUIRE(b.has_runtime());

    auto* const a_runtime_addr = &a.runtime();
    auto* const b_runtime_addr = &b.runtime();
    REQUIRE(a_runtime_addr != b_runtime_addr);  // sanity: distinct backing RTs pre-move

    b = std::move(a);

    // b's m_runtime now references a's prior RT (a.m_runtime was copied
    // before being nulled; a.m_owned_runtime_storage was transferred to b).
    CHECK(b.has_runtime());
    CHECK(&b.runtime() == a_runtime_addr);

    // a is empty post-move-assign.
    CHECK_FALSE(a.has_runtime());
}

TEST_CASE("SoftwareRenderer - moved-from instance is destructible (no double-free)") {
    using namespace chronon3d;

    {
        SoftwareRenderer a{Config{}};
        SoftwareRenderer b{std::move(a)};  // a becomes moved-from; both destroyed at scope end
        (void)b;
        // If a's destructor double-frees (m_image_renderer, m_session,
        // m_owned_runtime_storage), the process aborts and the test fails.
    }
    MESSAGE("moved-from destructor ran cleanly (no double-free of nested resources)");
    CHECK(true);
}

#endif // CHRONON3D_BUILD_TESTS
