# TICKET-AE-PARITY-KILLER-CHARACTER-OFFSET-VALUE-RANGE — Phase 2 Killer 3 pre-shaping invalidation lock

| Field | Value |
|---|---|
| **Status** | DONE (2026-07-07, commit (this commit)) |
| **Date** | 2026-07-07 |
| **Deciders** | Agent 3 (text subsystem) |
| **Tags** | `text`, `kinetic-typography`, `AE-parity`, `CharacterOffset`, `CharacterValue`, `CharacterRange`, `pre-shaping`, `invalidation`, `cat-2`, `feature-freeze-V0.1-revoked` |
| **Related** | [docs/tickets/TICKET-AE-LIKE-TEXT-ACTION-PLAN.md](TICKET-AE-LIKE-TEXT-ACTION-PLAN.md) (umbrella strategy, Decision 2 Killer 3); [docs/TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md](../TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md) Blocco 2 (pre-shaping invalidation); [docs/FOLLOWUP_TICKETS.md](../FOLLOWUP_TICKETS.md) (canonical index — DONE row); `tests/text_golden/ae_parity_killer/killer_01_wiggly_wave.cpp` (sibling Killer 1, same 2×3 pattern); `tests/text_golden/ae_parity_killer/killer_02_expression_selector.cpp` (sibling Killer 2); `src/text/animation/text_pre_shaping.cpp` (production `apply_character_offset_to_source` + `evaluate_pre_shaping_source`); `tests/text/test_character_offset_pre_shaping.cpp` (the FASE 2a unit-test baseline for CharacterOffset pre-shaping); `tools/wrap_push.sh` (GATE-MNT-01); `tools/check_doc_sync.sh` (gate #7) |

---

## Context

Phase 2 Killer 3 of the AE-like kinetic typography 2D rollout (per
`docs/tickets/TICKET-AE-LIKE-TEXT-ACTION-PLAN.md` Decision 2 Killer 3).
The killer test locks the **pre-shaping invalidation contract** —
`CharacterOffset` modifies the source text BEFORE HarfBuzz shaping
(FASE 2a, `src/text/animation/text_pre_shaping.cpp::apply_character_offset_to_source`),
which means `Offset +5` MUST swap the shaped glyph ID (not just
apply a post-layout visual transform) and MUST invalidate the
shaping + layout caches.  CharacterValue + CharacterRange are the
companion pre-shaping properties (text content override + range
override) that are PLANNED, NOT YET IMPL.

The killer test follows the established Phase 2 pattern (Killer 1
+ Killer 2): **2 TEST_CASEs × 3 SUBCASEs = 6 SUBCASEs**, with
cell-level substrate (TEST_CASE 1) + end-to-end pre-shaping
invalidation (TEST_CASE 2).  The killer test is GREEN today for
the IMPL portions (CharacterOffset + cache key) and
FORWARD-ONLY for the PLANNED portions (CharacterValue + CharacterRange).

---

## Decision 1 — Pre-shaping source transform substrate (cell-level)

### Spec

The pre-shaping source transform contract is locked at the cell
level via direct calls to the production
`chronon3d::apply_character_offset_to_source(source, offset)` function
(Caesar-cipher on UTF-8 code points, A-Z + a-z only, wrap-around,
non-letter pass-through, FASE 2a canonical behavior):

- **byte-identity**: same `(source, offset)` input → byte-identical
  output across 3 sequential invocations (pure-function contract)
- **boundary contracts**: offset=0 (no-op), +5 (A→F), +1 wrap
  (Z→A), -1 reverse (B→A), 26 cycle (HELLO unchanged), empty
  source (returns empty), non-letter pass-through (digits +
  punctuation unchanged)

The pre-shaping composition contract is locked at the cell level
via `chronon3d::evaluate_pre_shaping_source(animators, source)`:
when multiple animators each carry a CharacterOffsetProperty, the
total offset is the **SUM** of all individual offsets.  Negative
offsets are honored (negative composition: -3 + 2 = -1 net).

The pre-shaping detection contract is locked at the cell level
via `chronon3d::has_pre_shaping_properties(animators)`: returns
`true` when ANY enabled animator carries a CharacterOffsetProperty,
`false` otherwise.  PostLayout-only animators (PositionProperty
without CharacterOffset) return `false`.  Mixed stacks (some
pre-shaping + some post-layout) return `true` if any pre-shaping
animator is enabled.  **Disabled** animators with CharacterOffset
return `false` (the `spec.enabled = false` short-circuits the
pre-shaping evaluator).

### Source anchor

- `tests/text_golden/ae_parity_killer/killer_03_character_offset.cpp` (NEW)
- `include/chronon3d/text/animation/text_pre_shaping.hpp` (production API)
- `src/text/animation/text_pre_shaping.cpp` (production implementation)
- `tests/text/test_character_offset_pre_shaping.cpp` (FASE 2a unit-test baseline)

### Test lock

`TEST_CASE 1 — KILLER 03 character_offset — pre-shaping source transform substrate`
(3 SUBCASEs):
- **SUBCASE 1 — apply_character_offset_to_source byte-identity +
  boundary contracts**: 8 boundary CHECKs (offset=0, +5, +1 wrap,
  -1 reverse, 26 cycle, lowercase, non-letter pass-through,
  empty source)
- **SUBCASE 2 — evaluate_pre_shaping_source composes multiple
  CharacterOffset properties**: 2 composition CHECKs (positive
  composition +3 +2 = +5 → "A"→"F"; negative composition
  -3 +2 = -1 → "B"→"A")
- **SUBCASE 3 — has_pre_shaping_properties detection contract**:
  4 detection CHECKs (with pre-shaping → true; PostLayout-only
  → false; mixed → true; disabled → false)

### Failure mode (if silently broken)

- **Future regression** in `apply_character_offset_to_source` that
  changes the Caesar-cipher behavior (e.g. adding accent-aware
  shift, or wrong wrap-around direction).  The boundary-contract
  SUBCASE catches this on next CI run.
- **Future change** in `evaluate_pre_shaping_source` that switches
  from SUM to MULTIPLY or AVERAGE.  The composition SUBCASE
  catches this on next CI run.
- **Future change** in `has_pre_shaping_properties` that
  incorrectly returns `true` for PostLayout-only animators.
  The detection SUBCASE catches this on next CI run.

---

## Decision 2 — End-to-end pre-shaping invalidation

### Spec

The end-to-end pre-shaping invalidation contract is locked via
the production `evaluate_pre_shaping_source` + `TextLayoutCacheKey`
verification path:

- **CharacterOffset +5 swaps the source text (NOT just a visual
  offset)**: source "A" with CharacterOffset +5 → source "F" (Caesar
  +5).  Combined with PositionProperty{Vec3{0,0,0}} (no visual
  offset), the source STILL changes — proving the transformation
  is pre-shaping, not post-layout visual.  This is the **core
  pre-shaping contract** from Blocco 2 of the text roadmap.
  Anti-greenwashing: PositionProperty alone does NOT change the
  source (PostLayout, not pre-shaping).

- **TextLayoutCacheKey digest changes when the pre-shaping source
  changes**: the cache key for the pre-shaping-transformed source
  MUST differ from the cache key for the original source, so the
  cache will miss on next lookup and rebuild with the new source.
  If the cache key did NOT depend on the source, the cache would
  serve a STALE layout for the transformed source — a silent
  corruption.  Anti-greenwashing: same source + same font → same
  digest (cache hit on identical inputs).

- **CharacterValue + CharacterRange (FORWARD-ONLY)**: these are
  PLANNED, NOT YET IMPL.  The production contract that the future
  IMPL must follow is documented in SUBCASE 3 of TEST_CASE 2:
  - CharacterValue replaces a specific index's text content
    (e.g. index 2 in "HELLO" replaced by "X" → "HEXLO")
  - CharacterRange applies an override to a contiguous range
    (e.g. indices 1..3 in "HELLO" set to bold weight via a
    PostLayout property override)
  - Both are PreShaping (they modify the source text or the
    shaping-time properties, so they MUST be evaluated BEFORE
    HarfBuzz shaping and MUST invalidate the shaping + layout
    caches on change).

### Source anchor

- `tests/text_golden/ae_parity_killer/killer_03_character_offset.cpp` (NEW)
- `include/chronon3d/text/animation/text_pre_shaping.hpp` (production `evaluate_pre_shaping_source` + `has_pre_shaping_properties`)
- `include/chronon3d/text/text_layout_cache.hpp` (production `TextLayoutCacheKey::digest()`)
- `src/text/animation/text_pre_shaping.cpp` (production implementation)
- `src/text/text_layout_cache.cpp` (production cache implementation)

### Test lock

`TEST_CASE 2 — KILLER 03 character_offset — end-to-end pre-shaping invalidation`
(3 SUBCASEs):
- **SUBCASE 1 — CharacterOffset +5 swaps the source text (NOT
  just a visual offset)**: 3 CHECKs (CharacterOffset only →
  source swap; combined with PositionProperty{Vec3{0,0,0}} →
  source still swaps; PositionProperty alone → source unchanged)
- **SUBCASE 2 — TextLayoutCacheKey changes when pre-shaping
  source changes (cache invalidation)**: 2 CHECKs (different
  source → different digest; same source + same font → same
  digest)
- **SUBCASE 3 — CharacterValue + CharacterRange contract
  (FORWARD-ONLY smoke-test)**: 1 CHECK(true) placeholder +
  MESSAGE documenting the production contract

### Failure mode (if silently broken)

- **Pre-FASE 2a regression**: a future refactor accidentally
  applies CharacterOffset as a post-layout visual transform
  (the pre-FASE 2a behavior).  The source-swap SUBCASE catches
  this on next CI run.
- **Cache key regression**: a future refactor removes `source_text`
  from `TextLayoutCacheKey::digest()`.  The cache-key SUBCASE
  catches this on next CI run (cache would serve stale layouts
  for pre-shaping-transformed sources).
- **CharacterValue/Range misimplementation**: when the future
  IMPL lands, the FORWARD-ONLY SUBCASE is upgraded to a hard
  CHECK on the production contract.  Any deviation from
  "CharacterValue replaces index content" or "CharacterRange
  applies contiguous range override" is caught on next CI run.

---

## Test lock (per-commit)

- Build: `cmake --build build/chronon/linux-fast-dev --target chronon3d_text_golden_tests -j$(nproc)` exit 0
- Run: `./build/chronon/linux-fast-dev/tests/chronon3d_text_golden_tests --test-case="KILLER 03*"` exit 0
- ctest: `cd build/chronon/linux-fast-dev && ctest -R TextGoldenKiller --output-on-failure` exit 0
- Doc-sync: `tools/check_doc_sync.sh` exit 0 (gate #7)

---

## Forward-only contract

- When TICKET-CHARACTER-VALUE-RANGE-IMPL lands (new
  `src/text/source/character_offset_evaluator.cpp` per Blocco 2
  of `docs/TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md`):
  1. Implement CharacterValue + CharacterRange in
     `src/text/source/character_offset_evaluator.cpp`
  2. Wire them into the `TextAnimatorProperty` variant
     alongside `CharacterOffsetProperty`
  3. Extend `has_pre_shaping_properties` to detect them
  4. Extend `evaluate_pre_shaping_source` to apply them
  5. Upgrade SUBCASE 3 of TEST_CASE 2 from a smoke-test
     placeholder to a hard CHECK on the production contract
- CharacterValue + CharacterRange are currently PLANNED, NOT
  YET IMPL.  Verified via find-grep: zero `character_value*.cpp`
  or `character_range*.cpp` files in `src/text/`.

---

## Cross-cutting gates (must hold at the end of the commit)

```
[tools/check_doc_sync.sh]              exit 0  (doc drift caught)
[tools/check_architecture_boundaries.sh] exit 0  (16/16 PASS incluso Check 14/15 legacy)
[tools/check_legacy_text_pipeline.sh]  exit 0  (3/3 PASS)
[tools/wrap_push.sh origin main]       exit 0  (GATE-MNT-01 + per-branch rebase)
[chronon3d_text_golden_tests]          PASS on `KILLER 03*` TEST_CASE (5/6 hard SUBCASEs + 1 FORWARD-ONLY)
```

---

## Cross-link

- `docs/tickets/TICKET-AE-LIKE-TEXT-ACTION-PLAN.md` Decision 2 Killer 3
- `docs/TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md` Blocco 2 (pre-shaping invalidation)
- `tests/text_golden/ae_parity_killer/killer_01_wiggly_wave.cpp` (sibling Killer 1, same 2×3 pattern)
- `tests/text_golden/ae_parity_killer/killer_02_expression_selector.cpp` (sibling Killer 2)
- `tests/text/test_character_offset_pre_shaping.cpp` (FASE 2a unit-test baseline for CharacterOffset pre-shaping)
- `docs/adr/ADR-014-text-golden-coverage.md` (the AGENTS Cat-5 freeze-compliant doc-only precedent for killer scaffolding)
- `docs/FOLLOWUP_TICKETS.md` (canonical index — DONE row with `(this commit)` SHA placeholder)
