# TICKET-AE-PARITY-KILLER-EXPRESSION-SELECTOR — Phase 2 Killer 2 determinism lock

| Field | Value |
|---|---|
| **Status** | DONE (2026-07-07, commit (this commit)) |
| **Date** | 2026-07-07 |
| **Deciders** | Agent 3 (text subsystem) |
| **Tags** | `text`, `kinetic-typography`, `AE-parity`, `Expression-Selector`, `per-character`, `determinism`, `reentrant`, `cat-2`, `feature-freeze-V0.1-revoked` |
| **Related** | [docs/tickets/TICKET-AE-LIKE-TEXT-ACTION-PLAN.md](TICKET-AE-LIKE-TEXT-ACTION-PLAN.md) (umbrella strategy, Decision 2 Killer 2); [docs/TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md](../TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md) Blocco 6 (selector engine); [docs/FOLLOWUP_TICKETS.md](../FOLLOWUP_TICKETS.md) (canonical index — DONE row); `tests/text_golden/ae_parity_killer/killer_01_wiggly_wave.cpp` (sibling Killer 1, same 2×3 pattern); `tools/wrap_push.sh` (GATE-MNT-01); `tools/check_doc_sync.sh` (gate #7) |

---

## Context

Phase 2 Killer 2 of the AE-like kinetic typography 2D rollout (per
`docs/tickets/TICKET-AE-LIKE-TEXT-ACTION-PLAN.md` Decision 2 Killer 2). The
`ExpressionSelector` evaluates the per-character formula
`amount = selectorValue * textIndex / textTotal` — a linear ramp from 0
(at textIndex=0) to `selectorValue * (textTotal-1) / textTotal` (at
textIndex=textTotal-1). This is the canonical "AE-style per-character
expression" surface that the future production `ExpressionSelector`
implementation (new file `src/text/selector/expression_evaluator.cpp`
per Blocco 6 of `docs/TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md`,
integrated via `AnimatorResolver`) will compose on.

The killer test locks **what is lockable today** (the formula contract
as a pure function, plus the end-to-end pipeline substrate via the
production `evaluate_animator` + a `GlyphSelectorSpec` that emulates
the per-character linear ramp) and **marks what is forward-only**
(the production `ExpressionSelector` class itself, which is PLANNED).

---

## Decision 1 — Per-character formula contract (test-internal pure function)

### Spec

The contract formula is `amount = selectorValue * textIndex / textTotal`,
where:
- `selectorValue` is the "amount" parameter of the selector (f32 in [0, 1])
- `textIndex` is the 0-based index of the current character/glyph
- `textTotal` is the total number of characters/glyphs

The test-internal helper `expression_ramp(selector_value, text_index, text_total)`
is a `constexpr` pure function with no side effects and no global state.
Any future production `ExpressionSelector` implementation must match this
contract.

### Source anchor

`tests/text_golden/ae_parity_killer/killer_02_expression_selector.cpp` (NEW)

The `expression_ramp` helper is defined in the anonymous namespace at
the top of the test file. It returns `0.0f` when `text_total == 0`
(graceful degradation for empty input) and otherwise computes the
linear ramp exactly as the action plan specifies.

### Test lock

`TEST_CASE 1 — KILLER 02 expression_selector — per-character formula substrate`
(3 SUBCASEs):
- **SUBCASE 1 — formula byte-identity (cross-call predictability)**: same
  `(selectorValue, textIndex, textTotal)` input → byte-identical output
  across 3 sequential invocations. Boundary contracts:
  - `textIndex=0` → `amount=0` (no shift at start)
  - `textIndex=textTotal-1` → `amount=selectorValue * (textTotal-1) / textTotal`
- **SUBCASE 2 — formula has zero global mutable state (pure function)**:
  2 sequential passes of the formula produce identical per-position
  output. If the formula had global mutable state, the second pass
  would differ.
- **SUBCASE 3 — formula reentrant across serial/parallel (4 threads × 16
  iterations)**: 4 parallel threads each run 16 iterations of the
  formula and write into per-thread slots. All slots match the
  reference single-threaded run byte-for-byte.

### Failure mode (if silently broken)

- **Future ExpressionSelector IMPL** uses a different formula (e.g.
  `selectorValue * (textIndex+1) / textTotal` with the off-by-one shift).
  The byte-identity SUBCASE catches this on next CI run.
- **Formula acquires hidden state** (e.g. a `static` cache). The
  reentrant SUBCASE catches this on next CI run.

---

## Decision 2 — End-to-end pipeline substrate (production `evaluate_animator`)

### Spec

The end-to-end test uses the production `evaluate_animator` function
(`include/chronon3d/text/animation/text_animator_evaluator.hpp`) with a
`TextAnimatorSpec` that emulates the per-character linear ramp via the
existing `GlyphSelectorSpec` surface:

```cpp
GlyphSelectorSpec{unit=Character, shape=RampUp,
                  start=0, end=100,
                  amount=AnimatedValue{selectorValue*100}}
+ PositionProperty{Vec3{1.0, 0.0, 0.0}}
```

The per-glyph position is `position.x = selectorValue * (textIndex + 0.5) / textTotal`
(production RampUp shape uses the unit-centering offset of +0.5). When
TICKET-EXPRESSION-IMPL lands, this emulation is replaced with a direct
`ExpressionSelector` construction; the per-glyph contract is unchanged.

### Source anchor

- `tests/text_golden/ae_parity_killer/killer_02_expression_selector.cpp` (NEW)
- `include/chronon3d/text/animation/text_animator_evaluator.hpp` (production evaluator)
- `include/chronon3d/text/glyph_selector.hpp` (production `GlyphSelectorSpec` + `SelectorWeight` types)
- `include/chronon3d/text/animation/text_animator_spec.hpp` (production `TextAnimatorSpec`)

### Test lock

`TEST_CASE 2 — KILLER 02 expression_selector — end-to-end pipeline (evaluate_animator)`
(3 SUBCASEs):
- **SUBCASE 1 — random-access frame 30 == sequential up to frame 30
  (byte-identical)**: evaluating the spec at frame 30 directly produces a
  per-glyph state vector byte-identical to evaluating frames 0..30 in
  sequence. This is the determinism contract for the production
  `evaluate_animator` pipeline.
- **SUBCASE 2 — zero global mutable state in evaluator (sequential vs
  polluted-then-sequential)**: run the spec twice in sequence, with a
  "pollution" pass with a different `selectorValue` in between. Both
  runs produce identical output (proves the evaluator reads NO global
  mutable state).
- **SUBCASE 3 — reentrant across serial/parallel (4 threads × 16
  iterations)**: 4 parallel threads each run 16 iterations of the
  evaluator, writing into per-thread slots. All slots match the
  reference single-threaded run.

### Failure mode (if silently broken)

- **Production regression** — `evaluate_animator` acquires global mutable
  state. The 3-SUBCASE pattern catches this on next CI run.
- **Selector cross-contamination** — per-character selector leaks
  through `static` global state on parallel runs.

---

## Test lock (per-commit)

- Build: `cmake --build build/chronon/linux-fast-dev --target chronon3d_text_golden_tests -j$(nproc)` exit 0
- Run: `./build/chronon/linux-fast-dev/tests/chronon3d_text_golden_tests --test-case="KILLER 02*"` exit 0
- ctest: `cd build/chronon/linux-fast-dev && ctest -R TextGoldenKiller --output-on-failure` exit 0
- Doc-sync: `tools/check_doc_sync.sh` exit 0 (gate #7)

---

## Forward-only contract

- When TICKET-EXPRESSION-IMPL lands (new `src/text/selector/expression_evaluator.cpp`
  + `AnimatorResolver` integration per Blocco 6), TEST_CASE 2 is
  extended to use the production `ExpressionSelector` surface directly
  (instead of the `GlyphSelectorSpec` emulation). The cell-level
  substrate (TEST_CASE 1) is the contract the future `ExpressionSelector`
  must implement.
- Production `ExpressionSelector` is currently PLANNED, NOT YET IMPL.
  Verified via find-grep: zero `expression_evaluator.cpp` files in
  `src/text/`.

---

## Cross-cutting gates (must hold at the end of the commit)

```
[tools/check_doc_sync.sh]              exit 0  (doc drift caught)
[tools/check_architecture_boundaries.sh] exit 0  (16/16 PASS incluso Check 14/15 legacy)
[tools/check_legacy_text_pipeline.sh]  exit 0  (3/3 PASS)
[tools/wrap_push.sh origin main]       exit 0  (GATE-MNT-01 + per-branch rebase)
[chronon3d_text_golden_tests]          PASS on `KILLER 02*` TEST_CASE (6/6 SUBCASEs)
```

---

## Cross-link

- `docs/tickets/TICKET-AE-LIKE-TEXT-ACTION-PLAN.md` Decision 2 Killer 2
- `docs/TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md` Blocco 6 (selector engine)
- `tests/text_golden/ae_parity_killer/killer_01_wiggly_wave.cpp` (sibling Killer 1, same 2×3 pattern)
- `docs/adr/ADR-014-text-golden-coverage.md` (the AGENTS Cat-5 freeze-compliant doc-only precedent for killer scaffolding)
- `docs/FOLLOWUP_TICKETS.md` (canonical index — DONE row with `(this commit)` SHA placeholder)
