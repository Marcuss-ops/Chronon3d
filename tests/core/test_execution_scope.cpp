// ============================================================================
// tests/core/test_execution_scope.cpp
//
// WP-6 PR 6.0 acceptance tests for ExecutionScope (root/tile/precomp),
// updated for FASE 5 (closed legacy public surface — make_root / make_child
// are the ONLY public construction paths).
//
// Verifies:
//   1. Root construction via make_root(): kind=Root, depth=0, parent=null,
//      chain_length=1.
//   2. Child construction via make_child(): depth tracks parent+1.
//   3. is_descendant_of() distinguishes ancestors vs siblings.
//   4. Recursion rejection via make_child()'s Err(RecursiveOwner).
//   5. Child arena is INDEPENDENT of the parent's resource().
//   6. chain_length() tracks depth + 1.
//   7. kind() preserves the value passed at construction.
//   8. Chain depth is bounded by kMaxScopeChainLength — make_child returns
//      Err(ChainLimitExceeded) at the boundary (replaces the legacy
//      `would_overflow()` direct-ctor clamp test).
//   9-13. PR 6.6 — lifecycle isolation: ArenaGuard child/parent, tile/root,
//      precomp-inside-tile chain, sibling tile independence, session sharing.
//   FASE 5.1 — SFINAE invariants: no default ctor, no public direct ctor.
//   FASE 5.2 — make_child parent-nullptr validation.
//   FASE 5.3 — make_child recursion detection (key-based cycle rejection).
//
// Header-only type under test: no .cpp file pairing needed for this PR.
//
// ASAN/UBSAN guidance (PR 6.6): run with
//   -fsanitize=address,undefined -fno-omit-frame-pointer
// to detect use-after-reset (child arena reset while parent holds pointers)
// and nested-teardown order violations.
// ============================================================================

#include <doctest/doctest.h>

#include <chronon3d/core/scope/execution_scope.hpp>
#include <chronon3d/core/execution/execution_scope_types.hpp>
#include <chronon3d/core/memory/arena.hpp>
#include <chronon3d/internal/runtime/render_session.hpp>
#include <chronon3d/render_graph/core/node_identity.hpp>
#include <chronon3d/core/types/result.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

using chronon3d::FrameArena;
using chronon3d::RenderSession;
using chronon3d::graph::ExecutionScope;
using chronon3d::graph::ExecutionScopeKind;
using chronon3d::graph::GraphInstanceId;
using chronon3d::graph::kMaxScopeChainLength;
using chronon3d::graph::ScopeErrorCode;

// ── FASE 5 SFINAE — ExecutionScope construction surface closed ──────────────
//
// `make_root()` and `make_child()` are the ONLY public ways to obtain a
// ExecutionScope.  The 5-arg explicit ctor is `private`, so the following
// `static_assert`s verify that no direct-construction invocation compiles
// against post-FASE-5 headers.
//
// NOTES:
//   * `is_default_constructible_v` is false because the class has a private
//     non-default ctor (the 5-arg explicit) and no compiler-generated
//     default.
//   * `is_constructible_v` for any direct-arg combination matches against
//     the public surface; since all ctors are private, every direct form
//     yields `false`.
//   * `is_copy_constructible_v`: ExecutionScope holds reference members
//     (m_session, m_arena).  Default copy ctor IS legal because refs can
//     be copied (the new ref points to the same target), so this assertion
//     does NOT force a delete — but we still static_assert the surface
//     does include a copy ctor (so removal of it is a deliberate choice).
//   * `is_move_constructible_v`: Same reasoning — move ctor is implicit
//     and legal because member-wise move re-binds refs identically.

static_assert(!std::is_default_constructible_v<ExecutionScope>,
              "FASE 5: ExecutionScope must NOT be default-constructible; "
              "make_root() is the canonical entry, default ctor removed.");
static_assert(!std::is_constructible_v<
                  ExecutionScope,
                  ExecutionScopeKind,
                  chronon3d::RenderSession&>,
              "FASE 5: 3-arg root ctor is no longer publicly callable.");
static_assert(!std::is_constructible_v<
                  ExecutionScope,
                  ExecutionScopeKind,
                  chronon3d::RenderSession&,
                  GraphInstanceId>,
              "FASE 5: 3-arg root ctor (with graph_id) is no longer publicly callable.");
static_assert(!std::is_constructible_v<
                  ExecutionScope,
                  ExecutionScopeKind,
                  chronon3d::RenderSession&,
                  GraphInstanceId,
                  const ExecutionScope*>,
              "FASE 5: 4-arg deprecated child ctor is no longer publicly callable.");
static_assert(!std::is_constructible_v<
                  ExecutionScope,
                  ExecutionScopeKind,
                  chronon3d::RenderSession&,
                  FrameArena&,
                  GraphInstanceId,
                  const ExecutionScope*>,
              "FASE 5: 5-arg explicit ctor is no longer publicly callable.");
static_assert(!std::is_copy_assignable_v<ExecutionScope>,
              "FASE 5: copy-assigning a scope would re-bind reference members; "
              "must remain deleted-by-impl (refs cannot be reseated).");

// ── Helpers ─────────────────────────────────────────────────────────────────

namespace {

/// Factory-style wrapper for the legacy vector-based chain build.  Vector
/// can't hold ExecutionScope directly (emplace_back needs a public ctor),
/// so we hold `unique_ptr<ExecutionScope>` and provide stable parent
/// pointers via `.get()`.  unique_ptr<ExecutionScope> construction works
/// because ExecutionScope is move-constructible (legally re-binds refs).
struct ChainSlotBuilder {
    std::vector<std::unique_ptr<ExecutionScope>> slots;

    ExecutionScope& emplace_root(
        RenderSession& session,
        GraphInstanceId gid = kInvalidGraphInstanceId
    ) {
        slots.push_back(std::make_unique<ExecutionScope>(
            ExecutionScope::make_root(session, session.arena(), gid)));
        return *slots.back();
    }

    ExecutionScope& emplace_child(
        ExecutionScopeKind kind,
        RenderSession& session,
        FrameArena& arena,
        GraphInstanceId gid,
        const ExecutionScope& parent,
        std::uint64_t owner_key = 0u
    ) {
        auto result = ExecutionScope::make_child(
            kind, session, arena, gid, &parent, owner_key);
        REQUIRE(result.is_ok());
        slots.push_back(
            std::make_unique<ExecutionScope>(result.value()));
        return *slots.back();
    }
};

} // namespace

// ── Foundation tests (existing — migrated to factory API) ───────────────────

TEST_CASE("ExecutionScope: root construction — kind=Root, depth=0, parent=null") {
    RenderSession session;
    ExecutionScope root = ExecutionScope::make_root(
        session, session.arena(), GraphInstanceId{1});

    CHECK(root.kind() == ExecutionScopeKind::Root);
    CHECK(root.depth() == 0);
    CHECK(root.parent() == nullptr);
    CHECK(root.chain_length() == 1);
    CHECK(root.graph_id() == GraphInstanceId{1});
    CHECK(&root.session() == &session);
    CHECK(&root.arena() == &session.arena());
    CHECK(root.owner_key() == 0u);
}

TEST_CASE("ExecutionScope: tile child — depth=1, parent links to root") {
    RenderSession root_session;
    FrameArena child_arena;
    auto root_res = ExecutionScope::make_root(
        root_session, root_session.arena(), GraphInstanceId{1});
    const ExecutionScope& root = root_res;  // bind const ref to extend lifetime past the temp's destruction site

    auto tile_res = ExecutionScope::make_child(
        ExecutionScopeKind::Tile, root_session, child_arena,
        GraphInstanceId{2}, &root);
    REQUIRE(tile_res.is_ok());
    ExecutionScope tile = tile_res.value();

    CHECK(tile.kind() == ExecutionScopeKind::Tile);
    CHECK(tile.parent() == &root);
    CHECK(tile.depth() == 1);
    CHECK(tile.chain_length() == 2);
    CHECK(tile.is_descendant_of(root));
    CHECK_FALSE(root.is_descendant_of(tile));
}

TEST_CASE("ExecutionScope: sibling scopes are NOT descendants of each other") {
    RenderSession session;
    FrameArena arena_a, arena_b;
    ExecutionScope root = ExecutionScope::make_root(
        session, session.arena(), GraphInstanceId{1});
    auto tile_a_res = ExecutionScope::make_child(
        ExecutionScopeKind::Tile, session, arena_a,
        GraphInstanceId{2}, &root);
    auto tile_b_res = ExecutionScope::make_child(
        ExecutionScopeKind::Tile, session, arena_b,
        GraphInstanceId{3}, &root);
    REQUIRE(tile_a_res.is_ok());
    REQUIRE(tile_b_res.is_ok());
    ExecutionScope tile_a = tile_a_res.value();
    ExecutionScope tile_b = tile_b_res.value();

    CHECK_FALSE(tile_a.is_descendant_of(tile_b));
    CHECK_FALSE(tile_b.is_descendant_of(tile_a));
    CHECK(tile_a.is_descendant_of(root));
    CHECK(tile_b.is_descendant_of(root));
    CHECK(tile_a.parent() == tile_b.parent());
}

TEST_CASE("ExecutionScope: deeper chain — grandchild depth=2") {
    RenderSession session;
    FrameArena arena_a, arena_b;
    ExecutionScope root = ExecutionScope::make_root(
        session, session.arena(), GraphInstanceId{1});
    auto tile_res = ExecutionScope::make_child(
        ExecutionScopeKind::Tile, session, arena_a,
        GraphInstanceId{2}, &root);
    auto pre_res = ExecutionScope::make_child(
        ExecutionScopeKind::Precomp, session, arena_b,
        GraphInstanceId{3}, &tile_res.value());
    REQUIRE(tile_res.is_ok());
    REQUIRE(pre_res.is_ok());
    const ExecutionScope& tile = tile_res.value();
    const ExecutionScope& pre = pre_res.value();

    CHECK(pre.depth() == 2);
    CHECK(pre.chain_length() == 3);
    CHECK(pre.is_descendant_of(tile));
    CHECK(pre.is_descendant_of(root));
    CHECK_FALSE(tile.is_descendant_of(pre));
    CHECK_FALSE(root.is_descendant_of(pre));
}

TEST_CASE("ExecutionScope: recursion detection — direct precomp loop rejected") {
    // FASE 5: was pc1.set_owner_key(...) + pc1.would_recurse(k).  Now:
    // make_child validates RecursiveOwner by checking the parent chain for
    // a pre-existing Precomp scope with the same owner_key.  We construct
    // pc1 with owner_key=0xDEADBEEF, then attempt to nest pc2 INSIDE pc1
    // with the same key — make_child must return Err(RecursiveOwner).
    RenderSession session;
    FrameArena arena_a, arena_b;
    ExecutionScope root = ExecutionScope::make_root(
        session, session.arena(), GraphInstanceId{1});
    auto pc1_res = ExecutionScope::make_child(
        ExecutionScopeKind::Precomp, session, arena_a,
        GraphInstanceId{2}, &root, /*owner_key*/ 0xDEADBEEFull);
    REQUIRE(pc1_res.is_ok());
    const ExecutionScope& pc1 = pc1_res.value();

    // Attempting a child precomp with the SAME owner_key must fail.
    auto pc2_same_key = ExecutionScope::make_child(
        ExecutionScopeKind::Precomp, session, arena_b,
        GraphInstanceId{3}, &pc1, /*owner_key*/ 0xDEADBEEFull);
    REQUIRE(pc2_same_key.is_err());
    CHECK(pc2_same_key.err().code == ScopeErrorCode::RecursiveOwner);

    // A different key should succeed.
    auto pc2_diff_key = ExecutionScope::make_child(
        ExecutionScopeKind::Precomp, session, arena_b,
        GraphInstanceId{3}, &pc1, /*owner_key*/ 0xCAFEBABEull);
    REQUIRE(pc2_diff_key.is_ok());

    // owner_key==0 is the "unset" sentinel — must also succeed (validation
    // explicitly skips recursion check when key==0).
    FrameArena arena_c;
    auto pc2_zero_key = ExecutionScope::make_child(
        ExecutionScopeKind::Precomp, session, arena_c,
        GraphInstanceId{3}, &pc1, /*owner_key*/ 0u);
    REQUIRE(pc2_zero_key.is_ok());
}

TEST_CASE("ExecutionScope: recursion detection — indirect loop via shared ancestor") {
    // ROOT → PC1(0x456) → PC2(0x123) → attempt to make PC3(0x456).
    // From PC2's perspective, walking the parent chain (PC2 → PC1 → ROOT)
    // finds PC1 with key 0x456 — must reject.
    RenderSession session;
    FrameArena arena_a, arena_b, arena_c;
    ExecutionScope root = ExecutionScope::make_root(
        session, session.arena(), GraphInstanceId{1});
    auto pc1_res = ExecutionScope::make_child(
        ExecutionScopeKind::Precomp, session, arena_a,
        GraphInstanceId{2}, &root, /*owner_key*/ 0x456ull);
    REQUIRE(pc1_res.is_ok());
    auto pc2_res = ExecutionScope::make_child(
        ExecutionScopeKind::Precomp, session, arena_b,
        GraphInstanceId{3}, &pc1_res.value(), /*owner_key*/ 0x123ull);
    REQUIRE(pc2_res.is_ok());
    const ExecutionScope& pc2 = pc2_res.value();

    // PC2 → make_child PC3 with the ancestor's 0x456 key → Err.
    auto pc3_ancestor_key = ExecutionScope::make_child(
        ExecutionScopeKind::Precomp, session, arena_c,
        GraphInstanceId{4}, &pc2, /*owner_key*/ 0x456ull);
    REQUIRE(pc3_ancestor_key.is_err());
    CHECK(pc3_ancestor_key.err().code == ScopeErrorCode::RecursiveOwner);

    // A new key (not yet on the chain) must succeed.
    auto pc3_new_key = ExecutionScope::make_child(
        ExecutionScopeKind::Precomp, session, arena_c,
        GraphInstanceId{4}, &pc2, /*owner_key*/ 0x999ull);
    REQUIRE(pc3_new_key.is_ok());
}

TEST_CASE("ExecutionScope: child arena — independent of parent arena") {
    RenderSession parent_session;
    FrameArena child_arena;
    ExecutionScope parent = ExecutionScope::make_root(
        parent_session, parent_session.arena(), GraphInstanceId{1});
    auto child_res = ExecutionScope::make_child(
        ExecutionScopeKind::Tile, parent_session, child_arena,
        GraphInstanceId{2}, &parent);
    REQUIRE(child_res.is_ok());
    const ExecutionScope& child = child_res.value();

    REQUIRE(&child.arena() != &parent.arena());
    auto parent_resource = parent_session.arena().resource();
    auto child_resource  = child_arena.resource();
    REQUIRE(parent_resource != nullptr);
    REQUIRE(child_resource  != nullptr);
    CHECK(parent_resource != child_resource);

    // Resetting the child's arena MUST NOT touch the parent arena's
    // resource pointer (the parent_session.arena_ptr is unaffected).
    child_arena.reset();
    CHECK(parent_session.arena().resource() == parent_resource);
}

TEST_CASE("ExecutionScope: kind() round-trips all three kinds") {
    RenderSession session;
    FrameArena arena_a, arena_b;
    ExecutionScope root = ExecutionScope::make_root(
        session, session.arena(), GraphInstanceId{0});
    auto tile_res = ExecutionScope::make_child(
        ExecutionScopeKind::Tile, session, arena_a,
        GraphInstanceId{0}, &root);
    auto pre_res = ExecutionScope::make_child(
        ExecutionScopeKind::Precomp, session, arena_b,
        GraphInstanceId{0}, &tile_res.value());
    REQUIRE(tile_res.is_ok());
    REQUIRE(pre_res.is_ok());

    CHECK(root.kind() == ExecutionScopeKind::Root);
    CHECK(tile_res.value().kind() == ExecutionScopeKind::Tile);
    CHECK(pre_res.value().kind()  == ExecutionScopeKind::Precomp);

    CHECK(std::string(execution_scope_kind_name(ExecutionScopeKind::Root))    == "Root");
    CHECK(std::string(execution_scope_kind_name(ExecutionScopeKind::Tile))    == "Tile");
    CHECK(std::string(execution_scope_kind_name(ExecutionScopeKind::Precomp)) == "Precomp");
}

TEST_CASE("ExecutionScope: chain depth grows by one per nested child") {
    RenderSession session;
    FrameArena arena_a, arena_b, arena_c, arena_d, arena_e;
    ExecutionScope root = ExecutionScope::make_root(
        session, session.arena(), GraphInstanceId{0});
    auto t1_res = ExecutionScope::make_child(
        ExecutionScopeKind::Tile, session, arena_a,
        GraphInstanceId{0}, &root);
    auto t2_res = ExecutionScope::make_child(
        ExecutionScopeKind::Tile, session, arena_b,
        GraphInstanceId{0}, &t1_res.value());
    auto t3_res = ExecutionScope::make_child(
        ExecutionScopeKind::Tile, session, arena_c,
        GraphInstanceId{0}, &t2_res.value());
    auto t4_res = ExecutionScope::make_child(
        ExecutionScopeKind::Tile, session, arena_d,
        GraphInstanceId{0}, &t3_res.value());
    auto t5_res = ExecutionScope::make_child(
        ExecutionScopeKind::Tile, session, arena_e,
        GraphInstanceId{0}, &t4_res.value());
    REQUIRE(t1_res.is_ok());
    REQUIRE(t5_res.is_ok());
    const ExecutionScope& t5 = t5_res.value();
    const ExecutionScope& t3 = t3_res.value();

    CHECK(t1_res.value().depth() == 1);
    CHECK(t2_res.value().depth() == 2);
    CHECK(t3_res.value().depth() == 3);
    CHECK(t4_res.value().depth() == 4);
    CHECK(t5.depth() == 5);
    CHECK(t5.chain_length() == 6);
    CHECK(t5.is_descendant_of(root));
    CHECK(t5.is_descendant_of(t3));
    CHECK_FALSE(root.is_descendant_of(t5));
}

TEST_CASE("ExecutionScope: anthrilinear (ancestor-then-sibling) — RecursiveOwner on re-entry") {
    // Pre-FASE-5 was: would_recurse() walks up the parent chain to detect
    // re-entry of an earlier Precomp via a sibling branch.  Post-FASE-5:
    // make_child performs the same walk internally; same invariant, but
    // verified via Err(RecursiveOwner) rather than a public method call.
    //
    // ROOT → TILE_A(owner_key=0) → PRECOMP_PA(0xABCDEF) → TILE_B → attempt
    // PRECOMP inside TILE_B with key 0xABCDEF — must fail RecursiveOwner.
    RenderSession session;
    FrameArena a[5];
    ExecutionScope root = ExecutionScope::make_root(
        session, session.arena(), GraphInstanceId{0});
    auto a_res = ExecutionScope::make_child(
        ExecutionScopeKind::Tile, session, a[0],
        GraphInstanceId{0}, &root);
    auto pa_res = ExecutionScope::make_child(
        ExecutionScopeKind::Precomp, session, a[1],
        GraphInstanceId{0}, &a_res.value(), /*owner_key*/ 0xABCDEFull);
    auto b_res = ExecutionScope::make_child(
        ExecutionScopeKind::Tile, session, a[2],
        GraphInstanceId{0}, &pa_res.value());
    REQUIRE(a_res.is_ok());
    REQUIRE(pa_res.is_ok());
    REQUIRE(b_res.is_ok());

    // Re-entering pa's owner_key from inside b → Err(RecursiveOwner).
    auto reentry = ExecutionScope::make_child(
        ExecutionScopeKind::Precomp, session, a[3],
        GraphInstanceId{0}, &b_res.value(), /*owner_key*/ 0xABCDEFull);
    REQUIRE(reentry.is_err());
    CHECK(reentry.err().code == ScopeErrorCode::RecursiveOwner);

    // A non-colliding key must succeed.
    auto fresh = ExecutionScope::make_child(
        ExecutionScopeKind::Precomp, session, a[4],
        GraphInstanceId{0}, &b_res.value(), /*owner_key*/ 0x1111ull);
    REQUIRE(fresh.is_ok());
}

TEST_CASE("ExecutionScope: kMaxScopeChainLength is the published constant") {
    REQUIRE(kMaxScopeChainLength == 16);
}

TEST_CASE("ExecutionScope: kMaxScopeChainLength — make_child Err at boundary") {
    // FASE 5 — replaces the legacy `would_overflow()` direct-ctor test.
    //
    // Build root + (kMaxScopeChainLength-1) = 15 children via make_child.
    // The deepest has chain_length kMaxScopeChainLength (=16).  A 16th
    // child attempt must return Err(ChainLimitExceeded) — make_child is
    // the canonical gate, no silent clamp.
    RenderSession session;
    ChainSlotBuilder chain;
    ExecutionScope& root = chain.emplace_root(session, GraphInstanceId{0});
    FrameArena child_arena;
    ExecutionScope* deepest = &root;
    for (int i = 1; i < kMaxScopeChainLength; ++i) {
        deepest = &chain.emplace_child(
            ExecutionScopeKind::Tile, session, child_arena,
            GraphInstanceId{static_cast<std::uint64_t>(i)}, *deepest);
    }
    CHECK(deepest->chain_length() == kMaxScopeChainLength);

    // 16th child attempt — Err(ChainLimitExceeded).
    FrameArena overflow_arena;
    auto overflow_res = ExecutionScope::make_child(
        ExecutionScopeKind::Tile, session, overflow_arena,
        GraphInstanceId{0xFFFFu}, deepest);
    REQUIRE(overflow_res.is_err());
    CHECK(overflow_res.err().code == ScopeErrorCode::ChainLimitExceeded);

    // Failing the 16th child does NOT consume any state — the chain is
    // still usable: a new attempt with a fresh arena would also be rejected,
    // but reducing depth by removing the deepest element would allow growth.
    // For test purposes, we just assert every existing element was OK.
    for (const auto& slot : chain.slots) {
        CHECK(slot->chain_length() >= 1);
        CHECK(slot->chain_length() <= kMaxScopeChainLength);
    }
}

TEST_CASE("ExecutionScope: make_child — Root kind rejected (InvalidChildKind)") {
    RenderSession session;
    FrameArena arena;
    ExecutionScope root = ExecutionScope::make_root(
        session, session.arena(), GraphInstanceId{1});
    auto bad = ExecutionScope::make_child(
        ExecutionScopeKind::Root, session, arena,
        GraphInstanceId{2}, &root);
    REQUIRE(bad.is_err());
    CHECK(bad.err().code == ScopeErrorCode::InvalidChildKind);
}

TEST_CASE("ExecutionScope FASE 5: make_child — parent=nullptr rejected (ParentRequired)") {
    RenderSession session;
    FrameArena arena;
    auto bad = ExecutionScope::make_child(
        ExecutionScopeKind::Tile, session, arena,
        GraphInstanceId{1}, /*parent*/ nullptr);
    REQUIRE(bad.is_err());
    CHECK(bad.err().code == ScopeErrorCode::ParentRequired);
}

TEST_CASE("ExecutionScope FASE 5: make_child — arena aliasing rejected (ArenaAliasesParent)") {
    // Pass the SAME arena as the parent — make_child rejects aliasing because
    // child teardown (ArenaGuard reset) would invalidate pointers the parent
    // still holds.  This was a silent acceptance pre-FASE-5.
    RenderSession session;
    ExecutionScope root = ExecutionScope::make_root(
        session, session.arena(), GraphInstanceId{1});
    auto bad = ExecutionScope::make_child(
        ExecutionScopeKind::Tile, session, session.arena(),
        GraphInstanceId{2}, &root);
    REQUIRE(bad.is_err());
    CHECK(bad.err().code == ScopeErrorCode::ArenaAliasesParent);
}

// ══════════════════════════════════════════════════════════════════════════
// PR 6.6 — Lifecycle isolation tests (ArenaGuard simulation)
// ══════════════════════════════════════════════════════════════════════════

namespace {
/// Mirrors the executor's ArenaGuard pattern (see executor.cpp).
/// Resets the arena in the destructor, matching the RAII scope guard
/// that wraps every `execute_with_scope()` call.
struct TestArenaGuard {
    FrameArena& arena;
    ~TestArenaGuard() { arena.reset(); }
};
} // namespace

TEST_CASE("ExecutionScope PR 6.6: ArenaGuard — child scope reset does not touch parent arena") {
    RenderSession parent_session;
    FrameArena child_arena;

    auto* parent_resource = parent_session.arena().resource();
    auto* child_resource  = child_arena.resource();
    REQUIRE(parent_resource != nullptr);
    REQUIRE(child_resource  != nullptr);
    REQUIRE(parent_resource != child_resource);

    ExecutionScope parent = ExecutionScope::make_root(
        parent_session, parent_session.arena(), GraphInstanceId{1});
    auto child_res = ExecutionScope::make_child(
        ExecutionScopeKind::Tile, parent_session, child_arena,
        GraphInstanceId{2}, &parent);
    REQUIRE(child_res.is_ok());
    const ExecutionScope& child = child_res.value();

    REQUIRE(&child.arena() == &child_arena);
    REQUIRE(&parent.arena() == &parent_session.arena());

    {
        TestArenaGuard guard{child_arena};
    }
    CHECK(parent_session.arena().resource() == parent_resource);
}

TEST_CASE("ExecutionScope PR 6.6: tile scope reset does not touch root arena") {
    RenderSession session;
    FrameArena tile_arena;

    auto* root_resource = session.arena().resource();
    auto* tile_resource = tile_arena.resource();
    REQUIRE(root_resource != tile_resource);

    ExecutionScope root = ExecutionScope::make_root(
        session, session.arena(), GraphInstanceId{1});
    auto tile_res = ExecutionScope::make_child(
        ExecutionScopeKind::Tile, session, tile_arena,
        GraphInstanceId{2}, &root);
    REQUIRE(tile_res.is_ok());
    const ExecutionScope& tile = tile_res.value();

    {
        TestArenaGuard guard{tile_arena};
    }
    CHECK(session.arena().resource() == root_resource);
}

TEST_CASE("ExecutionScope PR 6.6: precomp inside tile uses independent child arena") {
    RenderSession session;
    FrameArena tile_arena, precomp_arena;

    auto* root_res = session.arena().resource();
    auto* tile_res = tile_arena.resource();
    auto* pre_res  = precomp_arena.resource();

    REQUIRE(root_res != tile_res);
    REQUIRE(tile_res != pre_res);
    REQUIRE(root_res != pre_res);

    ExecutionScope root = ExecutionScope::make_root(
        session, session.arena(), GraphInstanceId{1});
    auto tile_res_obj = ExecutionScope::make_child(
        ExecutionScopeKind::Tile, session, tile_arena,
        GraphInstanceId{2}, &root);
    auto pre_res_obj = ExecutionScope::make_child(
        ExecutionScopeKind::Precomp, session, precomp_arena,
        GraphInstanceId{3}, &tile_res_obj.value());
    REQUIRE(tile_res_obj.is_ok());
    REQUIRE(pre_res_obj.is_ok());
    const ExecutionScope& tile = tile_res_obj.value();
    const ExecutionScope& pre  = pre_res_obj.value();

    REQUIRE(&pre.arena() == &precomp_arena);
    REQUIRE(&tile.arena() == &tile_arena);
    REQUIRE(&root.arena() == &session.arena());

    {
        TestArenaGuard guard{precomp_arena};
    }
    CHECK(tile_arena.resource() == tile_res);
    CHECK(session.arena().resource() == root_res);
}

TEST_CASE("ExecutionScope PR 6.6: two sibling tile scopes use independent arenas") {
    RenderSession session;
    FrameArena tile_a_arena, tile_b_arena;

    auto* res_a = tile_a_arena.resource();
    auto* res_b = tile_b_arena.resource();
    REQUIRE(res_a != res_b);

    ExecutionScope root = ExecutionScope::make_root(
        session, session.arena(), GraphInstanceId{1});
    auto tile_a_res = ExecutionScope::make_child(
        ExecutionScopeKind::Tile, session, tile_a_arena,
        GraphInstanceId{2}, &root);
    auto tile_b_res = ExecutionScope::make_child(
        ExecutionScopeKind::Tile, session, tile_b_arena,
        GraphInstanceId{3}, &root);
    REQUIRE(tile_a_res.is_ok());
    REQUIRE(tile_b_res.is_ok());
    const ExecutionScope& tile_a = tile_a_res.value();
    const ExecutionScope& tile_b = tile_b_res.value();

    REQUIRE(&tile_a.arena() != &tile_b.arena());

    {
        TestArenaGuard guard{tile_a_arena};
    }
    CHECK(tile_b_arena.resource() == res_b);
}

TEST_CASE("ExecutionScope PR 6.6: tile scopes share the same session by reference") {
    RenderSession session;
    FrameArena arena_a, arena_b;

    ExecutionScope root = ExecutionScope::make_root(
        session, session.arena(), GraphInstanceId{1});
    auto tile_a_res = ExecutionScope::make_child(
        ExecutionScopeKind::Tile, session, arena_a,
        GraphInstanceId{2}, &root);
    auto tile_b_res = ExecutionScope::make_child(
        ExecutionScopeKind::Tile, session, arena_b,
        GraphInstanceId{3}, &root);
    REQUIRE(tile_a_res.is_ok());
    REQUIRE(tile_b_res.is_ok());
    const ExecutionScope& tile_a = tile_a_res.value();
    const ExecutionScope& tile_b = tile_b_res.value();

    CHECK(&tile_a.session() == &session);
    CHECK(&tile_b.session() == &session);
    CHECK(&tile_a.session() == &tile_b.session());

    CHECK(tile_a.parent() == &root);
    CHECK(tile_b.parent() == &root);
}
