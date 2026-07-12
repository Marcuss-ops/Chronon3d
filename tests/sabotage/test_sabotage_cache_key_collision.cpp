// ============================================================================
// tests/sabotage/test_sabotage_cache_key_collision.cpp
// ============================================================================
//
// Sabotage scenario #6: glyph cache lookup returns the SAME cache
// key for two distinct (asset_id, glyph_id) inputs. This is a
// hash-collision rot-pattern that causes the cache to serve stale
// glyphs for the wrong text run, producing silent visual rot that
// is hard to detect from a single render but obviously wrong across
// a sequence of frames.
//
// Engine signature (genuinely-not-yet-exposed surface — NO verified
// real symbol for glyph cache): machine-verified grep 2026-07-12
// for `glyph_cache` + `GlyphCache` returned 0 hits in `include/
// + src/ + content/ + apps/`. The fabricated `GlyphCache::get_or_
// load(asset_id, glyph_id)` claim is REMOVED. The closest real
// surfaces are `cache` + `hash` in timeline composition (NOT
// glyph-specific):
//   include/chronon3d/timeline/composition.hpp
//   include/chronon3d/timeline/compiled_composition.hpp
//   include/chronon3d/timeline/timeline_resolver_v2.hpp
//   include/chronon3d/text/text_document.hpp
//   include/chronon3d/text/text_span.hpp
// The Phase 2 fail-loud HOOK is therefore FULLY PLANNED
// (TICKET-SABOTAGE-PRODUCTION-HOOKS) and requires FIRST landing
// a glyph cache as a forward-point `TICKET-SABOTAGE-GLYPH-CACHE`
// (separate from this commit per AGENTS.md "Fare PR piccole e
// mirate").
//
// Expected fail-loud path: CacheKeyCollision Result<> error +
// the cache MUST NOT silently return the cached glyph for
// distinct inputs (the rot manifests downstream as glyphs from
// asset B appearing in runs that requested asset A).
//
// Per user spec "Ogni test exit non-zero + identifica comp/layer/node
// + fail-loud":
//   - INFO lines identify comp/layer/node (stub labels).
//   - FAIL_CHECK forces the doctest assertion to fail -> exit 1.
// ============================================================================
#include <doctest/doctest.h>

namespace chaos::sabotage::cache_key_collision {

// Engine-side trigger fingerprint. Implementation forward-point:
// TICKET-SABOTAGE-PRODUCTION-HOOKS — FULLY PLANNED.
// (Machine-verified 2026-07-12: `GlyphCache` symbol has 0 grep
// hits in the codebase; the production hook must FIRST land a
// glyph cache family as separate TICKET-SABOTAGE-GLYPH-CACHE.)
bool trigger_cache_key_collision_failure() {
    return false;
}

} // namespace chaos::sabotage::cache_key_collision

TEST_CASE(
    "sabotage/cache_key_collision "
    "[comp=GlyphCache, layer=CacheLayer, "
    "node=cache_lookup_HASH_COLLISION_distinct_inputs_PLANNED]"
) {
    INFO("Comp=GlyphCache [no verified glyph cache in codebase]")
    INFO("Layer=CacheLayer [closest real surface is timeline/"
         "composition.hpp — NOT glyph-specific]")
    INFO("Node=cache_lookup(A,X) == cache_lookup(B,Y) WHERE "
         "(A,X) != (B,Y) [PLANNED TICKET-SABOTAGE-PRODUCTION-HOOKS]")
    INFO("Sabotage scenario: hash collision on distinct cache "
         "inputs -> CacheKeyCollision")
    INFO("Forward-point: TICKET-SABOTAGE-PRODUCTION-HOOKS + "
         "TICKET-SABOTAGE-GLYPH-CACHE")
    FAIL_CHECK(
        "sabotage/cache_key_collision: trigger returns false -- "
        "production fail-loud hook not yet wired "
        "(TICKET-SABOTAGE-PRODUCTION-HOOKS forward-point)"
    );
}
