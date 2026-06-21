// ============================================================================
// tests/core/test_execution_scope.cpp
//
// WP-6 PR 6.0 acceptance tests for ExecutionScope (root/tile/precomp).
//
// Verifies:
//   1. Root construction: kind=Root, depth=0, parent=null, chain_length=1.
//   2. Child ctor clamps depth correctly across a multi-level chain.
//   3. is_descendant_of() distinguishes ancestors vs siblings.
//   4. would_recurse() rejects direct + indirect precomp loops by key.
//   5. Child arena is INDEPENDENT of the parent's resource() — calling
//      reset() on the child does NOT touch the parent.
//   6. chain_length() tracks depth + 1.
//   7. kind() preserves the value passed at construction.
//   8. Chain depth is bounded by kMaxScopeDepth (depth stays finite when
//      many parents nest).
//
// Header-only type under test: no .cpp file pairing needed for this PR.
// ============================================================================

#include <doctest/doctest.h>

#include <chronon3d/core/scope/execution_scope.hpp>
#include <chronon3d/core/memory/arena.hpp>
#include <chronon3d/runtime/render_session.hpp>
#include <chronon3d/render_graph/core/node_identity.hpp>

#include <cstdint>
#include <string>

using chronon3d::FrameArena;
using chronon3d::RenderSession;
using chronon3d::graph::ExecutionScope;
using chronon3d::graph::ExecutionScopeKind;
using chronon3d::graph::GraphInstanceId;
using chronon3d::graph::kMaxScopeDepth;

TEST_CASE("ExecutionScope: root construction — kind=Root, depth=0, parent=null") {
    RenderSession session;
    ExecutionScope root(ExecutionScopeKind::Root, session, GraphInstanceId{1});

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
    ExecutionScope root(ExecutionScopeKind::Root, root_session, GraphInstanceId{1});
    ExecutionScope tile(ExecutionScopeKind::Tile, root_session,
                       GraphInstanceId{2}, &root);

    CHECK(tile.kind() == ExecutionScopeKind::Tile);
    CHECK(tile.parent() == &root);
    CHECK(tile.depth() == 1);
    CHECK(tile.chain_length() == 2);
    CHECK(tile.is_descendant_of(root));
    CHECK_FALSE(root.is_descendant_of(tile));
}

TEST_CASE("ExecutionScope: sibling scopes are NOT descendants of each other") {
    RenderSession session;
    ExecutionScope root(ExecutionScopeKind::Root, session, GraphInstanceId{1});
    ExecutionScope tile_a(ExecutionScopeKind::Tile, session,
                          GraphInstanceId{2}, &root);
    ExecutionScope tile_b(ExecutionScopeKind::Tile, session,
                          GraphInstanceId{3}, &root);

    CHECK_FALSE(tile_a.is_descendant_of(tile_b));
    CHECK_FALSE(tile_b.is_descendant_of(tile_a));
    CHECK(tile_a.is_descendant_of(root));
    CHECK(tile_b.is_descendant_of(root));
    CHECK(tile_a.parent() == tile_b.parent());
}

TEST_CASE("ExecutionScope: deeper chain — grandchild depth=2") {
    RenderSession session;
    ExecutionScope root( ExecutionScopeKind::Root,    session, GraphInstanceId{1});
    ExecutionScope tile( ExecutionScopeKind::Tile,    session, GraphInstanceId{2}, &root);
    ExecutionScope pre(  ExecutionScopeKind::Precomp, session, GraphInstanceId{3}, &tile);

    CHECK(pre.depth() == 2);
    CHECK(pre.chain_length() == 3);
    CHECK(pre.is_descendant_of(tile));
    CHECK(pre.is_descendant_of(root));
    CHECK_FALSE(tile.is_descendant_of(pre));
    CHECK_FALSE(root.is_descendant_of(pre));
}

TEST_CASE("ExecutionScope: would_recurse — direct precomp loop rejected") {
    RenderSession session;
    ExecutionScope root(ExecutionScopeKind::Root, session, GraphInstanceId{1});
    ExecutionScope pc1(ExecutionScopeKind::Precomp, session,
                      GraphInstanceId{2}, &root);
    pc1.set_owner_key(0xDEADBEEFull);

    CHECK(pc1.would_recurse(0xDEADBEEFull));          // same key → would loop
    CHECK_FALSE(pc1.would_recurse(0xCAFEBABEull));    // different key → ok
    CHECK_FALSE(pc1.would_recurse(0u));               // unset key (no match)
}

TEST_CASE("ExecutionScope: would_recurse — indirect loop detected") {
    RenderSession session;
    ExecutionScope root(ExecutionScopeKind::Root, session, GraphInstanceId{1});
    ExecutionScope pc1(ExecutionScopeKind::Precomp, session,
                      GraphInstanceId{2}, &root);
    ExecutionScope pc2(ExecutionScopeKind::Precomp, session,
                      GraphInstanceId{3}, &pc1);
    pc2.set_owner_key(0x123ull);
    pc1.set_owner_key(0x456ull);

    CHECK(pc2.would_recurse(0x123ull));
    CHECK(pc1.would_recurse(0x456ull));
    CHECK_FALSE(pc2.would_recurse(0x456ull));
    CHECK_FALSE(pc1.would_recurse(0x123ull));
}

TEST_CASE("ExecutionScope: child arena — independent of parent arena") {
    RenderSession parent_session;
    FrameArena child_arena;                  // explicit independent arena
    ExecutionScope parent(ExecutionScopeKind::Root,
                          parent_session, GraphInstanceId{1});
    ExecutionScope child( ExecutionScopeKind::Tile,
                          parent_session, child_arena, GraphInstanceId{2}, &parent);

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
    ExecutionScope root(ExecutionScopeKind::Root,    session, GraphInstanceId{0});
    ExecutionScope tile(ExecutionScopeKind::Tile,    session, GraphInstanceId{0}, &root);
    ExecutionScope pre( ExecutionScopeKind::Precomp, session, GraphInstanceId{0}, &tile);

    CHECK(root.kind() == ExecutionScopeKind::Root);
    CHECK(tile.kind() == ExecutionScopeKind::Tile);
    CHECK(pre.kind()  == ExecutionScopeKind::Precomp);

    CHECK(std::string(execution_scope_kind_name(ExecutionScopeKind::Root))    == "Root");
    CHECK(std::string(execution_scope_kind_name(ExecutionScopeKind::Tile))    == "Tile");
    CHECK(std::string(execution_scope_kind_name(ExecutionScopeKind::Precomp)) == "Precomp");
}

TEST_CASE("ExecutionScope: chain depth grows by one per nested child") {
    // Stack-allocated chain — Scope references outlive their owners only
    // for the duration of the test; pointer arithmetic on `cur_parent` is
    // safe because every nested scope is still in scope when we read its
    // .parent() through the previous link.
    RenderSession session;
    ExecutionScope root(ExecutionScopeKind::Root,    session, GraphInstanceId{0});
    ExecutionScope t1(  ExecutionScopeKind::Tile,    session, GraphInstanceId{0}, &root);
    ExecutionScope t2(  ExecutionScopeKind::Tile,    session, GraphInstanceId{0}, &t1);
    ExecutionScope t3(  ExecutionScopeKind::Tile,    session, GraphInstanceId{0}, &t2);
    ExecutionScope t4(  ExecutionScopeKind::Tile,    session, GraphInstanceId{0}, &t3);
    ExecutionScope t5(  ExecutionScopeKind::Tile,    session, GraphInstanceId{0}, &t4);

    CHECK(t1.depth() == 1);
    CHECK(t2.depth() == 2);
    CHECK(t3.depth() == 3);
    CHECK(t4.depth() == 4);
    CHECK(t5.depth() == 5);
    CHECK(t5.chain_length() == 6);
    CHECK(t5.is_descendant_of(root));
    CHECK(t5.is_descendant_of(t3));
    CHECK_FALSE(root.is_descendant_of(t5));
}

TEST_CASE("ExecutionScope: anthrilinear (ancestor-then-sibling) chain rejected") {
    // ROOT → TILE_A → PRECOMP_A → TILE_B → re-enter PRECOMP_A
    // Construction of scope chain is independent of recursion detection;
    // would_recurse() walks the parent chain so the second PRECOMP_A
    // re-entry is detected via the ancestor PRECOMP_A on the chain.
    RenderSession session;
    ExecutionScope root( ExecutionScopeKind::Root,    session, GraphInstanceId{0});
    ExecutionScope a(    ExecutionScopeKind::Tile,    session, GraphInstanceId{0}, &root);
    ExecutionScope pa(   ExecutionScopeKind::Precomp, session, GraphInstanceId{0}, &a);
    ExecutionScope b(    ExecutionScopeKind::Tile,    session, GraphInstanceId{0}, &pa);
    pa.set_owner_key(0xABCDEFull);

    // From `b`, walking the parent chain: b → pa → a → root.
    // Re-entering pa's owner key from inside b detects the loop.
    CHECK(b.would_recurse(0xABCDEFull));
    CHECK_FALSE(b.would_recurse(0x1111ull));
}

TEST_CASE("ExecutionScope: kMaxScopeDepth is the published constant") {
    REQUIRE(kMaxScopeDepth == 16);
}

TEST_CASE("ExecutionScope: kMaxScopeDepth clamps depth + would_overflow() reports it") {
    // PR 6.5 — chain depth > kMaxScopeDepth must be CLAMPED (not overflow
    // past the bound, not throw/abort).  would_overflow() latches true on
    // the offending scope so callers can detect + route to a deterministic
    // fallback (empty fb per docs/03 §4.4).
    RenderSession session;
    FrameArena child_arena;  // distinct from parent.arena() to test independent allocation path

    // Build a chain of depth=kMaxScopeDepth (anchor).  Establishes the chain
    // anchor without overflowing; first child (depth 1) + (kMaxScopeDepth-1)
    // nested children = total depth kMaxScopeDepth, no overflow.
    // `stack.reserve(kMaxScopeDepth + 1)` matches the exact capacity needed
    // (1 anchor + kMaxScopeDepth children), guaranteeing no reallocation
    // across the emplace_back loop — parent addresses (`&stack.back()`) stay
    // stable throughout.
    std::vector<ExecutionScope> stack;
    stack.reserve(kMaxScopeDepth + 1);
    stack.emplace_back(ExecutionScopeKind::Root, session, GraphInstanceId{0});
    for (int i = 1; i <= kMaxScopeDepth; ++i) {
        stack.emplace_back(
            ExecutionScopeKind::Tile, session,
            child_arena, GraphInstanceId{static_cast<std::uint64_t>(i)},
            &stack.back());
    }
    // After (kMaxScopeDepth) nested children on top of anchor, the deepest
    // child has depth = kMaxScopeDepth (boundary case, NOT overflow).
    ExecutionScope& deepest = stack.back();
    CHECK(deepest.depth() == kMaxScopeDepth);
    CHECK_FALSE(deepest.would_overflow());
    CHECK(deepest.chain_length() == kMaxScopeDepth + 1);

    // Adding one more child should clamp depth and latch would_overflow().
    ExecutionScope overflow_child(
        ExecutionScopeKind::Tile, session, child_arena,
        GraphInstanceId{0xFFFFu}, &deepest);
    CHECK(overflow_child.depth() == kMaxScopeDepth);  // clamped
    CHECK(overflow_child.would_overflow());
    CHECK(overflow_child.chain_length() == kMaxScopeDepth + 1);  // still bounded

    // Every scope in the stack BEFORE the overflow child must NOT report
    // would_overflow() — the latch fires only on the scope whose proposed
    // depth strictly exceeded kMaxScopeDepth.  stack[0] = root anchor,
    // stack[1..kMaxScopeDepth] = boundary-correct children.
    CHECK_FALSE(stack.front().would_overflow());
    for (std::size_t i = 1; i < stack.size(); ++i) {
        CHECK_FALSE(stack[i].would_overflow());
    }

    // would_recurse + is_descendant_of still terminate bounded by the clamp.
    overflow_child.set_owner_key(0xEEEEu);
    CHECK(overflow_child.would_recurse(0xEEEEu));  // self-loop detection still works on overflow scope
    CHECK_FALSE(overflow_child.would_recurse(0xABCDu));
}
