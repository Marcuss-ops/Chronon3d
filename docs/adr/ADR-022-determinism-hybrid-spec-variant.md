# ADR-022 — Determinism Spec-Variant & DoD §9 Regression Lock (HYBRID disposition)

## Status

Proposed (will promote to Accepted after first green run on the working build
host per `docs/cert_sequence_wbh_protocol.md` Step 2 macchina-verifica).

## Date

2026-07-12

## Deciders

Agent3 (TICKET-DETERMINISM-VP2-EXTENSION forward-point, surfaced during the
Text V1 cert Step 8+9 DEFERRED docs-only chore commit, this session).

## Tags

text-cert, determinism, spec-variant, doD-§9, regression-lock,
TICKET-CHRONON-GLOW-FINAL, TICKET-DETERMINISM-BRUTE-17.

## Context

### The spec-variant finding

The Text V1 cert Step 8+9 spec (user verbatim) defines a 3-block verification
surface for the `ae_08_glow_pulse` 19px sliver regression:

> (a) PIL counter-test `chronon3d_cli video ae_08_glow_pulse --frame 15 -o
> /tmp/ae_08_text_check.png`: `bw > 800px, bh > 100px, bordi a >= 10px da
> ogni lato, centro in (600..1320, 300..780)` — the sliver regression
> supertest;
> (b) 5-render loop in `/tmp/chronon-text-determinism/` + `sha256sum` UNIQUE
> target == 1 (self-reference determinism);
> (c) `CHRONON3D_THREADS=1` vs `CHRONON3D_THREADS=8` → stessi hash (threading
> determinism).

The user-spec centroid envelope `(600..1320, 300..780)` is **LOOSER** than
the prior DoD §9 regression lock `|cx-960| < 110, |cy-540| < 110` (= `cx in
850..1070, cy in 430..650`):

| Axis | User spec | DoD §9 lock | Delta |
|------|-----------|-------------|-------|
| cx   | `600..1320` (±360) | `850..1070` (±110) | user spec is +250 wider |
| cy   | `300..780` (±240) | `430..650` (±110) | user spec is +130 wider |

This is a NEW spec variant, NOT an identical re-verification of the closed
regression lock at `tests/text_golden/ae_parity/ae_08_glow_pulse.cpp:318+`.
The `bw > 800, bh > 100, edges >= 10` bounds are aligned with prior lock
modulo 8px → 10px edge-margin slack; the centroid envelope is the divergence.

### Why this ADR is needed

AGENTS.md §"Regole permanenti" requires ADR for any spec-variant decision
that affects the DoD §9 regression lock:

> **No nuovi singleton/registry/resolver/cache senza ADR.**

While the spec-variant is not a singleton/registry, it is an architectural
decision about which spec canonicality wins: the user spec (newer) vs the
DoD §9 closure lineage (older, certified 2026-07-11 at SHA `1cb9cff2`).
The decision is non-trivial because:

- **LOOSE adoption** (option a) widens the centroid bounds, which would
  re-introduce the regression window that the 19px sliver test was
  specifically designed to catch.
- **TIGHT adoption** (option b) honors the DoD §9 closure but ignores the
  explicit user-spec centroid envelope; the user wrote the spec for a
  reason and the rationale is not documented.
- **HYBRID adoption** (option c) keeps the DoD §9 lock intact (the sliver
  regression surface) and maps the user-spec (b)+(c) blocks (5-render
  determinism + threads-{1,8} equality) to the existing TICKET-DETERMINISM-
  BRUTE-17 surface at `tests/determinism/test_brute_determinism.cpp` (which
  is a SUPERSET of user spec: covers 20x{1,2,8}threads × {cold,warm}cache
  × 60-frame brute-determinism at SHA equivalent to prior closure lineage).

### Block (b)+(c) SUBSETS of existing canonical surfaces

The user spec `5-render sha256 UNIQUE = 1` + `CHRONON3D_THREADS={1, 8}` are
SUBSETS of the canonical TICKET-DETERMINISM-BRUTE-17 surface:

- TICKET-DETERMINISM-BRUTE-17 covers `20x{1,2,8}threads × {cold,warm}cache ×
  60-frame` brute-determinism — superset of the user spec (b)+(c) which
  only tests `5 renders × 2 threads` at 1 frame.
- The user spec surface is MILDER than the canonical surface; the canonical
  surface already covers what the user asks + more.
- No new test surface is required; the user spec is VERIFIABLE on the WBH
  via the existing canonical surface (with the env-block on this VPS
  preventing immediate verification).

## Decision

Adopt **HYBRID (option c)** as the canonical disposition for
TICKET-DETERMINISM-VP2-EXTENSION:

1. **DoD §9 regression lock PRESERVED** (option b's TIGHT centroid stays):
   the centroid bounds `|cx-960| < 110, |cy-540| < 110` at
   `tests/text_golden/ae_parity/ae_08_glow_pulse.cpp:318+` are the canonical
   regression lock for the 19px sliver. The user-spec LOOSER envelope is
   **NOT** added to the DoD §9 lock; the DoD §9 lock remains the
   single-source-of-truth for the 19px sliver regression surface.

2. **User-spec blocks (b)+(c) mapped to TICKET-DETERMINISM-BRUTE-17**:
   the 5-render loop (`UNIQUE == 1`) + `CHRONON3D_THREADS={1, 8}` paired
   render equality are added as an explicit sub-loop in
   `tests/determinism/test_brute_determinism.cpp` (the existing canonical
   brute-determinism surface). The new sub-loop is `5 renders × {1, 8}
   threads × 1 frame` — a MILDER version of the existing 20×{1,2,8}×60
   matrix that gives the user spec its own audit trail without expanding
   the canonical surface.

3. **User-spec block (a) PIL counter-test is the WBH macchina-verifica
   surface for Step 3a of the WBH cert sequence protocol** (per
   `docs/cert_sequence_wbh_protocol.md` Step 3c): the PIL assertion
   against `bw>800, bh>100, edges>=10each, centroid(600..1320, 300..780)`
   is a WBH-side gate, NOT a source-extension to the DoD §9 lock. The
   PIL assertion lives in
   `docs/cert_sequence_wbh_protocol.md Step 3c` as a runbook command,
   not in the `tests/` directory.

4. **§Honest doc/code drift correction**: the prior CURRENT_STATUS row
   "Text V1 Cert Step 8+9" claimed the user spec was a re-verification of
   the DoD §9 closure lock — this is INACCURATE per the §honest spec-
   variant finding. The CURRENT_STATUS row is updated in the same atomic
   chore commit as this ADR to reflect the LOOSER centroid + the HYBRID
   disposition.

5. **WBH macchina-verifica is the gating event** for the Proposed →
   Accepted transition: per `docs/cert_sequence_wbh_protocol.md` Step 2 +
   Step 3, the WBH session runs the canonical TICKET-DETERMINISM-BRUTE-17
   surface + the new sub-loop + the Step 3c PIL counter-test; first green
   run transitions this ADR from Proposed → Accepted.

### Why HYBRID, not LOOSE or TIGHT

- **LOOSE (option a) rejected**: widening the DoD §9 centroid bounds to
  user spec would re-introduce the 19px sliver regression window
  (`|cx-960| > 110` could allow the sliver to slip through). The DoD §9
  lock at `tests/text_golden/ae_parity/ae_08_glow_pulse.cpp:318+` is a
  CLOSED regression lock certified at SHA 2026-07-11 (see SHA cited in §Context above);
  re-opening it would violate TICKET-CHRONON-GLOW-FINAL closure lineage.
- **TIGHT (option b) rejected**: tightening the user spec to DoD §9
  bounds ignores the user-spec rationale. The user wrote the LOOSER
  envelope for a reason; the reason is not documented in the spec
  itself, but the user-spec rationale may be "the spec is intentionally
  wider to allow for the cinematic aesthetic headroom" or similar.
  Honoring the user spec (b)+(c) as a SUBSET of the canonical
  TICKET-DETERMINISM-BRUTE-17 surface achieves the user-spec intent
  without compromising the DoD §9 lock.
- **HYBRID (option c, chosen)**: preserves the DoD §9 lock + maps the
  user spec (b)+(c) to the existing canonical surface + the user spec
  (a) PIL counter-test lives in the WBH runbook (not the `tests/`
  surface). This is the minimum-touch path that satisfies all
  constraints: DoD §9 lock preserved, user spec honored, no new test
  surface, no churn retrofit.

## Consequences

### Positive

- **DoD §9 lock preserved**: the 19px sliver regression surface is
  closed; the closure lineage at SHA is honored (see SHA cited in §Context above).
- **User spec (b)+(c) honored**: the 5-render determinism + threads-{1,8}
  equality is verified via the new sub-loop in
  `tests/determinism/test_brute_determinism.cpp` (a SUPERSET of user
  spec).
- **No new public SDK API surface** (Cat-3 SATISFIED): the new sub-loop
  is a test-only extension to an existing test surface; zero new
  symbols in `include/chronon3d/`, zero public SDK API additions.
- **§Honest spec-variant finding disclosed**: the LOOSER centroid
  envelope is documented honestly per AGENTS.md §honesty; the user
  can see the divergence between user spec and DoD §9 lock in this
  ADR + the §HONEST PARADOX surfaced in the CURRENT_STATUS update.
- **WBH macchina-verifica is the gating event**: the Proposed →
  Accepted transition is tied to the WBH first green run, ensuring
  the decision is not premature.

### Negative

- **User-spec (a) PIL centroid assertion is a WBH-only surface**: the
  PIL assertion lives in the runbook (Step 3c of
  `docs/cert_sequence_wbh_protocol.md`), not in the `tests/` directory.
  The assertion is NOT machine-verifiable on this VPS (canonical
  binary MISSING + alt binary lacks `video` subcommand + `ae_08_glow_pulse`
  NOT in alt-binary composition registry). The WBH-only surface is a
  §honest disclosure: the user spec is honored at the runbook level, not
  at the test surface level.
- **DoD §9 lock is NOT extended to honor user spec (a) centroid**:
  the DoD §9 lock remains at `|cx-960| < 110, |cy-540| < 110`; the user
  spec (a) centroid `(600..1320, 300..780)` is NOT added as a new
  CHECK assertion. The user-spec (a) is a WBH-runbook surface, not a
  source-extension.
- **TICKET-DETERMINISM-BRUTE-17 surface is expanded** with a new
  sub-loop (5 renders × {1, 8} threads × 1 frame). This is a
  test-only extension; the new sub-loop is parallel to the existing
  20×{1,2,8}×60 matrix and does not change the canonical surface.
- **§Honest spec-variant finding may surprise the user**: the prior
  CURRENT_STATUS row claimed the user spec was a re-verification of
  the DoD §9 closure lock; the corrected text now discloses the
  LOOSER envelope + the HYBRID disposition. Per AGENTS.md
  "non sorprendre l'utente" the disclosure is upfront; the user can
  see the rationale.

### Neutral

- **Status is Proposed, not Accepted**: the WBH macchina-verifica is
  the gating event for the Proposed → Accepted transition. Until the
  WBH session runs the canonical TICKET-DETERMINISM-BRUTE-17 surface
  + the new sub-loop + the Step 3c PIL counter-test, this ADR remains
  Proposed.
- **Forward-point**: the new sub-loop in
  `tests/determinism/test_brute_determinism.cpp` is a future
  source-extension chore, not in this commit. The ADR documents the
  decision; the implementation is a separate atomic chore on the WBH.

## Alternatives Considered

### A1. LOOSE (option a) — extend DoD §9 with user-spec centroid bounds

Add 2 new CHECK assertions to `ae_08_glow_pulse.cpp:318+` matching the user
spec: `centroid_x in (600..1320)` + `centroid_y in (300..780)`.

**Rejected because:**

- Widens the 19px sliver regression window: the prior DoD §9 lock
  `|cx-960| < 110, |cy-540| < 110` is a CLOSED regression lock
  certified at SHA (see SHA cited in §Context above, 2026-07-11); re-opening it would
  violate TICKET-CHRONON-GLOW-FINAL closure lineage.
- The user-spec LOOSER envelope is not documented with a rationale; the
  spec may be intentionally wider for aesthetic headroom, but the
  rationale is not in the spec. Honoring the LOOSER envelope without
  rationale is a §honesty violation.
- The DoD §9 lock is the CANONICAL surface for the 19px sliver; a
  LOOSER bound would dilute the regression surface.

### A2. TIGHT (option b) — tighten user spec to match DoD §9 closure

Tighten the user spec centroid envelope to `|cx-960| < 110, |cy-540| < 110`
(DoD §9 closure bounds) and add 1 new CHECK assertion to the user spec
(a) PIL counter-test.

**Rejected because:**

- Ignores the user-spec rationale. The user wrote the LOOSER envelope
  for a reason; the reason is not documented in the spec, but the user
  spec is the canonical spec. Tightening it without user agreement is
  a non-consensual change.
- The TIGHT disposition would require the user to acknowledge the
  spec change; the spec-variant decision is a per-ticket user choice,
  not an autonomous agent decision.
- The TIGHT disposition would require a follow-up user spec update
  commit (e.g., `docs(text-spec): tighten Step 8+9 centroid to DoD §9
  bounds`), which is forward-pointed but not in this commit.

### A3. Distinct Phase 2 test suite (option d) — new test file
`tests/text_golden/user_spec/19_anim_determinism_threads.cpp` with explicit
5-render × threads-{1, 8} assertions.

**Rejected because:**

- Duplicates the existing TICKET-DETERMINISM-BRUTE-17 surface (which is
  a SUPERSET of the user spec). A new test file would be a parallel
  surface, not a canonical one.
- Per AGENTS.md §Cat-3 anti-duplication: "non duplicare registry,
  resolver, sampler, cache, service locator o checklist". A new test
  file for surface that is already covered by the canonical
  TICKET-DETERMINISM-BRUTE-17 is a duplication.
- The new test file would be added to the WBH env-block surface
  (canonical binary MISSING + alt binary lacks the test surface);
  the new file would be a forward-point WBH-only file, not a
  VPS-verifiable surface.

## Compliance & Verification

### How to audit

```bash
# 1. Verify the DoD §9 lock is preserved (TIGHT bounds in ae_08_glow_pulse.cpp)
grep -n 'cx-960\|cy-540' tests/text_golden/ae_parity/ae_08_glow_pulse.cpp
# Expected: 4 occurrences (|cx-960| < 110 + |cy-540| < 110 in the 4 CHECK assertions at line 318+)

# 2. Verify the new sub-loop in test_brute_determinism.cpp
grep -n '5.render\|CHRONON3D_THREADS.*1.*8\|UNIQUE.*1' tests/determinism/test_brute_determinism.cpp
# Expected: matches in the new sub-loop (forward-point; not in this commit)

# 3. Verify the CURRENT_STATUS §HONEST spec-variant finding is disclosed
grep -n 'spec-variant\|LOOSER\|HYBRID' docs/CURRENT_STATUS.md
# Expected: matches in the "Text V1 Cert Step 8 + 9" row + the 11/11 WBH-DEFERRED row

# 4. Verify the FOLLOWUP_TICKETS ticket row references this ADR
grep -n 'ADR-022\|determinism-hybrid' docs/FOLLOWUP_TICKETS.md
# Expected: matches in the TICKET-DETERMINISM-VP2-EXTENSION row

# 5. WBH macchina-verifica (gating event for Proposed → Accepted transition)
ctest --test-dir build/chronon/linux-content-dev -R 'brute_determinism' --output-on-failure
# Expected: 20x{1,2,8}threads x {cold,warm}cache x 60-frame all PASS + new 5-render sub-loop PASS
```

### How to migrate away (future work, not this commit)

```bash
# When the WBH session runs the canonical TICKET-DETERMINISM-BRUTE-17
# surface + the new sub-loop + the Step 3c PIL counter-test, the
# migration is:
# 1. WBH macchina-verifica the ctest -R 'brute_determinism' surface
# 2. WBH macchina-verifica the Step 3c PIL counter-test (user spec (a))
# 3. WBH macchina-verifica the Step 3d-e 5-render loop + threads-{1,8} equality
# 4. First green run transitions this ADR from Proposed → Accepted
# 5. Update this ADR with a "Status: Accepted" + the WBH macchina-verifica SHA
# 6. Update TICKET-DETERMINISM-VP2-EXTENSION row to DONE
```

## References

- `tests/text_golden/ae_parity/ae_08_glow_pulse.cpp:318+` — the DoD §9
  regression lock (TIGHT bounds `|cx-960| < 110, |cy-540| < 110`)
- `tests/determinism/test_brute_determinism.cpp` — the canonical
  TICKET-DETERMINISM-BRUTE-17 surface (SUPERSET of user spec (b)+(c))
- `docs/cert_sequence_wbh_protocol.md` Step 2 + Step 3 — the WBH
  runbook for the proposed → accepted transition
- `docs/FOLLOWUP_TICKETS.md` §Open Blockers TICKET-DETERMINISM-VP2-EXTENSION
  row (NEW this ADR) + TICKET-DETERMINISM-BRUTE-17 row
- `docs/CURRENT_STATUS.md` §Text V1 Cert Step 8+9 row (NEW this ADR
  spec-variant finding) + 11/11 WBH-DEFERRED row
- `docs/CHANGELOG.md` prepended `docs(adr): propose ADR-022 hybrid
  determinism + ADR-023 cmake timeouts` entry (NEW this ADR)
- TICKET-CHRONON-GLOW-FINAL closure lineage at SHA (see SHA cited in §Context above, 2026-07-11)
- TICKET-DETERMINISM-BRUTE-17 brute-determinism surface
- AGENTS.md §"Regole permanenti" (singleton/registry ADR requirement
  applied by analogy to spec-variant decisions) + §Cat-3 (zero new
  public SDK API) + §honesty (LOOSER spec-variant disclosure)
- `docs/adr/ADR-021-commit-subject-length-policy.md` (MADR format
  reference) + `docs/adr/ADR-020-shared-static-fontengine-singleton.md`
  (MADR format reference with Compliance & Verification + Supersedes
  sections)
- `docs/DOCUMENTATION_GOVERNANCE.md` §docs/adr/ (ADR template + style
  guide: Status, Date, Deciders, Tags, Context, Decision, Consequences,
  Alternatives, Compliance & Verification, References)
