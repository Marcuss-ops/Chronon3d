# TICKET-CENTERED-TEXT-TESTS-DETERMINISTIC-AREA-NON-VACUOUS-AUDIT — Audit del sub-chore (f) Blocco 5.2 (NON-VACUOUS finding)

| ID            | TICKET-CENTERED-TEXT-TESTS-DETERMINISTIC-AREA-NON-VACUOUS-AUDIT                                                                                                |
|---------------|------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| Status        | **DONE** 2026-07-14 (non-vacuous audit chaser-chore, Cat-5 3-doc atomic per AGENTS.md §honest-discipline)                                                          |
| Parent        | [TICKET-CENTERED-TEXT-MIGRATION-CHORE-B-BULK-MIGRATION](TICKET-CENTERED-TEXT-MIGRATION-CHORE-B-BULK-MIGRATION.md) (sub-chore (f) TESTS-DETERMINISTIC-AREA audit)     |
| Asset class   | docs discipline choreography (ZERO source modification; NON-VACUOUS finding disclosed per §honest-discipline)                                                    |
| Surface       | `docs/tickets/TICKET-CENTERED-TEXT-TESTS-DETERMINISTIC-AREA-NON-VACUOUS-AUDIT.md` (NEW canonical cronaca home)                                                     |
| Forward-point | [TICKET-CENTERED-TEXT-TESTS-DETERMINISTIC-AREA-MIGRATION](TICKET-CENTERED-TEXT-TESTS-DETERMINISTIC-AREA-MIGRATION.md) (NEW migration ticket, future source code chore) |

## Stato: DONE (NON-VACUOUS finding — diverges from user hint + catena-precedent)

## Contesto e Risultati dell'Audit (§honest-discipline)

A differenza dei precedenti sub-chore (c), (d), (e) e (i) che si sono rivelati **vacuous** (zero callers pre-migrated M1.8 §2D / helper already absent pre-session), l'audit trinity-probe del sub-chore (f) TESTS-DETERMINISTIC-AREA ha confermato che il target **NON È VACUOUS**: il file `tests/deterministic/test_visual_regression_scenarios.cpp` contiene **9 chiamanti reali** a `centered_text(...)` che devono ancora essere migrati alla forma canonica `TextDefinition{}` direct construction.

### User-spec verbatim 2026-07-14 vs trinity-probe findings

| User-spec claim | Trinity-probe finding | Divergence? |
|---|---|---|
| `~5 callsites in tests/deterministic/test_visual_regression_scenarios.cpp` | **9 rg matches (8 code-only + 1 comment-only L10)** `centered_text(` callers (rg with comment-strip filter) | YES (9 total vs 5 code-only estimate) |
| `macchina-verifica: rg -c 'centered_text\(...' tests/deterministic/ + tests/text/test_visual_regression_scenarios.cpp = 0` | rg returns 9 (8 code-only + 1 comment) in test_visual_regression_scenarios.cpp + 0 in FILE_NOT_FOUND second file | YES (current state ≠ vacuous) |
| `Per catena-precedent (c)/(d)/(e) all vacuous, expect (f) also vacuous` | 9 real code-only callers (catena-precedent diverges) | **YES — HONEST FINDING** |
| `tests/text/test_visual_regression_scenarios.cpp` (2nd file) | **FILE_NOT_FOUND** (vacuous via file-absence) | partial (2nd file vacuous via file-absence) |
| `CRITICAL: regression lock preservation obbligatorio (CenterTextOptions ↔ TextDefinition byte-equivalence test predecessore)` | kRefVR* sentinel-capture pattern preserved (PR-A3 precedent) + VR_GATE macro regression lock | CONFIRMED (constraint noted, will be enforced in migration ticket) |

## §HONEST-discipline macchina-verifica rigorosa (4-probe pattern, this session 2026-07-14)

### [PROBE-1] rg-probe per-file with comment-strip filter

| File | centered_text( | glow_text( | compute_single_line_glyph_layout( | CenterTextOptions | TextDefinition | Vacuous? |
|---|---|---|---|---|---|---|
| `tests/deterministic/test_visual_regression_scenarios.cpp` (526 LoC) | **9 rg matches (8 code-only + 1 comment-only)** | 0 | 0 | 7 (struct usage) | **0** | **NO (NON-VACUOUS)** |
| `tests/text/test_visual_regression_scenarios.cpp` | **FILE_NOT_FOUND** | n/a | n/a | n/a | n/a | YES (file-absence) |
| **TOTAL (sub-chore (f) audit scope)** | **9** | **0** | **0** | **7** | **0** | **NO (NON-VACUOUS, real source migration required)** |

### [PROBE-2] 4th scope-check: broader `tests/` rg with awk sum (corrected rg pattern post-review)

- `rg -c 'centered_text\(' tests/` = 56 (7 files total; 1 in (f) territory + 6 in (g) territory)
- `rg -c 'glow_text\(' tests/` = 0
- `rg -c 'compute_single_line_glyph_layout\(' tests/` = 0
- `rg -c 'centered_text\(' tests/deterministic/` = 9 (audit-scope confirmed; no surprise residual in other tests/deterministic/ files)
- `rg -c 'centered_text\(' tests/text/` = 47 (5 source files + 1 cmake, all in (g) TESTS-TEXT-AREA sub-chore territory per parent bulk-migration catena; **catena-overlap informational, NOT (f) audit scope**)

### [PROBE-2.1] Catena-overlap disambiguation vs sub-chore (g) TESTS-TEXT-AREA

The broader `rg -c 'centered_text\(' tests/` returns 56 matches across 7 files. Breakdown by catena sub-chore:

| Sub-chore | Files | Total rg matches | Territory |
|---|---|---|---|
| **(f) TESTS-DETERMINISTIC-AREA** (this audit) | `tests/deterministic/test_visual_regression_scenarios.cpp` | 9 | (f) — this audit's scope |
| **(g) TESTS-TEXT-AREA** (separate sub-chore) | `tests/text/test_text_definition.cpp` (24) + `tests/text/test_text_simplicity_adapters.cpp` (18) + `tests/text/test_text_preset_subtitle.cpp` (2) + `tests/text/test_text_golden.cpp` (1) + `tests/text/visual/text_visual_fixture.hpp` (1) + `tests/text_simplicity_adapters_tests.cmake` (1) | 47 | (g) — catena-overlap, NOT (f) audit scope |

**§honest-discipline boundary correction**: the initial audit's 4th scope-check claim "no surprise residual in OTHER test files" was INFERRED based on sub-chore (f) audit-scope only. The corrected rg pattern (single-quote escaped) revealed 6 additional files in (g) sub-chore territory. Per AGENTS.md §honest-discipline, this catena-overlap is documented transparently: the 47 matches in (g) territory are real residual callers, but they are the responsibility of sub-chore (g) TESTS-TEXT-AREA, not (f). The (f) audit scope is strictly `tests/deterministic/test_visual_regression_scenarios.cpp` (9 matches) + `tests/text/test_visual_regression_scenarios.cpp` (FILE_NOT_FOUND).

### [PROBE-3] catena-context broader sweep (content/ + apps/ + src/)

- `rg -c 'centered_text\(' content/ apps/ src/` = 0
- `rg -c 'glow_text\(' content/ apps/ src/` = 0
- `rg -c 'compute_single_line_glyph_layout\(' content/ apps/ src/` = 0

### [PROBE-4] include path verification + file existence (basher-verified)

| File path | Status | LoC |
|---|---|---|
| `include/chronon3d/text/text_helpers.hpp` | NOT_FOUND | n/a |
| `include/chronon3d/text/text_helpers_centered.hpp` | NOT_FOUND (per sub-chore (i) HELPER-REMOVAL-FINAL chaser-ticket) | n/a |
| `content/text/text_helpers.hpp` | **EXISTS** | 15 |
| `content/text/text_helpers_centered.hpp` | **EXISTS** (defines `centered_text` + `CenterTextOptions` + helper functions) | 148 |
| `content/text/text_helpers_typewriter.hpp` | EXISTS (sibling context) | 94 |
| `content/text/text_glow_helpers.hpp` | EXISTS (canonical AE-glow, per sub-chore (e) audit) | 93 |

Test file L54 verbatim: `#include <content/text/text_helpers.hpp>`. The `centered_text` function lives at `content/text/text_helpers_centered.hpp` (148 LoC, content-text-scoped, NOT SDK-public). Migration can happen by replacing the 9 callsites with `TextDefinition{}` direct construction WITHOUT removing the helper (helper can remain as `[[deprecated]]` wrapper for now; canonical pattern is direct construction).

## Per-call evidence (line-numbered + 5-line context, basher-verified)

| # | L | Code pattern | Effect description |
|---|---|---|---|
| 1 | 10 | `//   (A) Text-bearing scenarios use canonical centered_text(CenterTextOptions)` | Header comment (deprecation-locked at M1.8 §2D) |
| 2 | 218 | `auto ts = centered_text(opt);` | Single static-text scenario (canonical pattern) |
| 3 | 276 | `auto spec = centered_text(make_opts("STROKE", 96.0f, Color::black()));` | Stroke effect scenario |
| 4 | 297 | `auto spec = centered_text(make_opts("GRADIENT", 96.0f, Color::white()));` | Gradient fill scenario |
| 5 | 324 | `auto spec = centered_text(make_opts("SHADOW", 96.0f, Color::black()));` | Shadow effect scenario |
| 6 | 344 | `auto spec = centered_text(make_opts("GLOW", 96.0f, Color::white()));` | Glow effect scenario |
| 7 | 372 | `auto spec = centered_text(make_opts("BLUR", 96.0f, Color::black()));` | Blur effect scenario |
| 8 | 506 | `auto tiny = centered_text(make_opts("tiny", 8.0f, Color{...}, ...));` | Tiny font scenario (multi-line call) |
| 9 | 509 | `auto huge = centered_text(make_opts("HUGE", 220.0f, Color{...}, ...));` | Huge font scenario (multi-line call) |

All 9 callers pass the result (as `ts` or `spec`) to `l.text("k", ts)` per file head pattern (L11 verbatim): `s.layer("hero", [&](LayerBuilder& l) { l.text("k", ts); });`.

## CRITICAL: byte-equivalence regression lock preservation

The file uses sentinel-capture pattern via `VR_GATE(short_label, kRef*, m)` macro (PR-A3 precedent). Any source migration MUST NOT change output pixels — `kRefVR*` sentinel hashes MUST remain byte-equivalent post-migration.

### Mapping table (CenterTextOptions → TextDefinition 1:1 verbatim)

| CenterTextOptions field | TextDefinition field | Note |
|---|---|---|
| `CenterTextOptions.text` | `TextDefinition.content` | direct |
| `CenterTextOptions.font_size` | `TextDefinition.style.font.size` | direct |
| `CenterTextOptions.color` | `TextDefinition.style.color` | direct |
| `CenterTextOptions.pin_to(Anchor::Center)` (via opts) | `TextDefinition.frame.{size,anchor,align,...}` | structural |
| `CenterTextOptions.background/shadow/glow` (effect opts) | `TextDefinition.style.{paint,shadows,box_style,...}` | structural (effects integrated) |

The migration ticket [TICKET-CENTERED-TEXT-TESTS-DETERMINISTIC-AREA-MIGRATION](TICKET-CENTERED-TEXT-TESTS-DETERMINISTIC-AREA-MIGRATION.md) MUST apply this mapping table verbatim + verify ctest -R VisualRegression (DEFERRED-WBH per VPS vcpkg env-block).

## honest-limitation

### Catena-overlap disambiguation vs sub-chore (d)/(e) (informational only, NOT audit duplication)

- Sub-chore (d) audit scope = `content/certification/cert_*.cpp` (4 user-spec + 9 sibling)
- Sub-chore (e) audit scope = `content/text/` (2 user-spec + 2 sibling)
- **Sub-chore (f) audit scope = `tests/deterministic/test_visual_regression_scenarios.cpp`** (1 user-spec file present + 1 FILE_NOT_FOUND)
- The broader `rg -l 'centered_text\(' content/` finding of 4 files (cert_*) is from (d) territory; sub-chore (f) probe `rg -l 'centered_text\(' tests/` returns 1 file (the sub-chore (f) audit target itself). NO cross-chore overlap.

### Honest-discipline boundary case: divergence from user hint + catena-precedent

User hint verbatim: "Per catena-precedent (c)/(d)/(e) all vacuous, expect (f) also vacuous — verify via trinity-probe pattern". Trinity-probe revealed 9 REAL code-only callers in `tests/deterministic/test_visual_regression_scenarios.cpp` (catena-precedent diverges). Per AGENTS.md §honest-discipline: the audit correctly reports the divergence rather than retroactively claiming vacuous to match the catena-precedent. Real source migration is forward-pointed to [TICKET-CENTERED-TEXT-TESTS-DETERMINISTIC-AREA-MIGRATION](TICKET-CENTERED-TEXT-TESTS-DETERMINISTIC-AREA-MIGRATION.md) (separate future chore with cat-3 minimal-surface + byte-equivalence regression lock preservation obbligatorio + ctest -R VisualRegression DEFERRED-WBH per VPS vcpkg glm/magic_enum env-block).

## Soluzione applicata (Cat-5 3-doc atomic, mirroring (e) cleanup-chore precedent at commit `db3e088b`)

Per AGENTS.md §honest-discipline + cat-5 3-doc discipline (2 NEW canonicals + 3 EDIT canonicals pattern):

1. **macchina-verifica rigorosa (4-probe pattern)** per confermare NON-VACUOUS finding (this session, 2026-07-14, VPS-side)
2. **NEW cronaca home audit chaser-ticket** (this file): preserve the cronaca ext per Cat-3 anti-dup disciplin + 9-call evidence + per-line snippet + catena-precedent divergence framing + byte-equivalence regression lock documentation
3. **NEW forward-point migration-ticket** ([TICKET-CENTERED-TEXT-TESTS-DETERMINISTIC-AREA-MIGRATION](TICKET-CENTERED-TEXT-TESTS-DETERMINISTIC-AREA-MIGRATION.md)): the future source code migration chore, with explicit byte-equivalence regression lock preservation acceptance criteria
4. **Cat-5 3-doc atomic update**: 2 NEW canonicals (audit + migration fwd-point) + 3 EDIT canonicals (parent bulk-migration §Per-AREA + §Forward-points (f) row updates + CHANGELOG cite-only + FOLLOWUP_TICKETS audit row in §Recently Closed + migration row in §Open Blockers)
5. **ZERO source modification this chore** (cat-3 minimal-surface)
6. **macchina-verifica post-update**: parent bulk-migration §Forward-points (f) row status aggiornato per catena-discovery (OPEN → DONE audit + migration forward-point)

## Vincoli (ottemperati)

- ZERO source modification questo turno (no editing di `tests/deterministic/test_visual_regression_scenarios.cpp`)
- ZERO new SDK API surface in `include/chronon3d/`
- ZERO new singleton/registry/resolver/cache (AGENTS.md deny-everywhere preserved)
- ZERO `#include <msdfgen>/<libtess2>/<unicode[/...]>` (Gate 5 deny-everywhere preserved)
- Cat-3 anti-dup: cronaca ext lives in this canonical ticket-home; CHANGELOG + FOLLOWUP_TICKETS = cite-only + cross-link pointer
- Cat-2 freeze: NON-NEEDED for the audit chore (no SDK API touched); MIGRATION chore is source-replacement (canonical, no ADR needed)
- macchina-verifica: ctest -R VisualRegression DEFERRED-WBH per VPS vcpkg glm/magic_enum env-block (TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV pattern); LOCAL-FS verification PASS this session via rg-probe + 4-probe pattern

## Forward-points

| # | Status | Description |
|---|--------|-------------|
| (f)-migration | OPEN (P2, future chore) | **TICKET-CENTERED-TEXT-TESTS-DETERMINISTIC-AREA-MIGRATION**: source code migration dei 9 chiamanti in `tests/deterministic/test_visual_regression_scenarios.cpp` da `centered_text(CenterTextOptions)` a `TextDefinition{}` direct construction. **CRITICAL**: byte-equivalence regression lock preservation obbligatorio (kRefVR* sentinel hashes MUST remain byte-equivalent). ctest -R VisualRegression DEFERRED-WBH per VPS vcpkg glm/magic_enum env-block. Mapping table (CenterTextOptions → TextDefinition) documentata in §CRITICAL sopra. Subject envelope proposed per future chore: `chore(text): migrate sub-chore (f) tests-deterministic 9 callers` (~58 chars ≤72). |
| (f)-phase3-remove | OPEN (P3, future chore post-(f)-migration) | After (f)-migration DONE: `centered_text` function itself can be removed from `content/text/text_helpers_centered.hpp` per TICKET-CENTERED-TEXT-MIGRATION §Criteri (Phase 3 overload REMOVAL + ABI-stability ADR per cat-2 freeze). The `CenterTextOptions` struct itself is also legacy-deprecated; same Phase 3 removal scope. |

## Cronologia chiusura (per SHA cite-pattern AGENTS.md §Regole di lint documentale)

- **Parent bulk-migration catena**: [TICKET-CENTERED-TEXT-MIGRATION-CHORE-B-BULK-MIGRATION](TICKET-CENTERED-TEXT-MIGRATION-CHORE-B-BULK-MIGRATION.md) (parent; sub-chore (f) → DONE audit per this chaser; (f)-migration forward-pointed)
- **Parent strategy**: [TICKET-CENTERED-TEXT-MIGRATION](TICKET-CENTERED-TEXT-MIGRATION.md) (P2 OPEN, helper rimosso vacuous pre-session per sub-chore (i))
- **Sibling chaser-ticket precedent (vacuous-truth + audit chaser pattern this catena session)**:
  - [TICKET-CENTERED-TEXT-PLACEMENT-AREA-VACUOUS-VERIFY](TICKET-CENTERED-TEXT-PLACEMENT-AREA-VACUOUS-VERIFY.md) (sub-chore (c), vacuous)
  - [TICKET-CENTERED-TEXT-CERTIFICATION-AREA-VACUOUS-VERIFY](TICKET-CENTERED-TEXT-CERTIFICATION-AREA-VACUOUS-VERIFY.md) (sub-chore (d), vacuous)
  - [TICKET-CENTERED-TEXT-OTHER-AREA-VACUOUS-VERIFY](TICKET-CENTERED-TEXT-OTHER-AREA-VACUOUS-VERIFY.md) (sub-chore (e), vacuous + 2nd cleanup-chore at commit `db3e088b`)
  - [TICKET-CENTERED-TEXT-HELPER-REMOVAL-FINAL-VACUOUS-VERIFY](TICKET-CENTERED-TEXT-HELPER-REMOVAL-FINAL-VACUOUS-VERIFY.md) (sub-chore (i), vacuous)
  - **TICKET-CENTERED-TEXT-TESTS-DETERMINISTIC-AREA-NON-VACUOUS-AUDIT** (sub-chore (f), NON-VACUOUS — FIRST diverging sub-chore in this catena, audit-ticket-home per §honest-discipline)
- **Forward-point ticket (this catena)**: [TICKET-CENTERED-TEXT-TESTS-DETERMINISTIC-AREA-MIGRATION](TICKET-CENTERED-TEXT-TESTS-DETERMINISTIC-AREA-MIGRATION.md) (NEW migration ticket, future source code chore, cat-3 minimal-surface with byte-equivalence regression lock preservation)
- **Forward-point hygiene ticket (this catena)**: [TICKET-CENTERED-TEXT-BULK-MIGRATION-CRITERI-DUPLICATE-BUG-CHASER](TICKET-CENTERED-TEXT-BULK-MIGRATION-CRITERI-DUPLICATE-BUG-CHASER.md) (separate forward-point pattern; (e) cleanup-chore rider-4)
- **AGENTS.md governance rules invoked**:
  - `§Honest-discipline` (NON-VACUOUS finding documented honestly, NOT executed as duplicate work; divergence from user hint + catena-precedent reported transparently)
  - `§Cat-3 anti-dup` (cronaca NOT in canonical docs; lives in this canonical ticket-home + migration ticket-home)
  - `§Docs canonical update discipline rule` (Cat-5 3-doc discipline chore framing: 2 NEW canonicals + 3 EDIT canonicals + ZERO source this chore)
  - `§Fare PR piccole e mirate` (atomic per-AREA bounded sub-chores; (f) audit + (f)-migration split per Chore B precedent)
  - `§Post-push SHA-selfcheck invariant` (SHA-triple verify post-push — will execute per cat-7 GATE-MNT-01 triad)
  - `§GATE-MNT-01 closure lineage` (per-branch rebase + tools/wrap_push.sh + tools/check_main_clean.sh triad)
  - `§Per-branch rebase convention` (read-side, branch.main.rebase = true)
  - `§Regole di lint documentale` (canonico-fence rule + CHANGELOG cite-only ≤300 + subject envelope ≤72)

## Lessons learned (per Cat-5 chaser-chore precedent lineage)

Per future agenti:

- **Trinity-probe pattern obbligatorio** per ogni sub-chore (anche se user hint dice "vacuous"): macchina-verifica rigorosa prima di aprire un chaser-ticket, anche quando catena-precedent dice vacuous. The trinity-probe finds reality, not what the user hint expects.
- **Honest-discipline boundary case**: quando la catena-precedent dice vacuous ma la trinity-probe rivela callers reali, documentare la divergenza nel chaser-ticket-home, NON retroattivamente "fingere" vacuous per matchare la catena. The §honest-discipline rule takes priority over catena-coherence.
- **Forward-point pattern (cat-5 3-doc 2-ticket topology)**: when audit reveals real work, open a separate forward-point ticket for the future source code migration (cat-3 minimal-surface + byte-equivalence regression lock preservation for tests). Mirrors (e) cleanup-chore rider-4 (CRITERI-DUPLICATE-BUG-CHASER hygiene-ticket).
- **Byte-equivalence regression lock** (kRefVR* sentinel hashes + VR_GATE macro) MUST be preserved through any test file migration: mapping table (CenterTextOptions → TextDefinition) is the canonical 1:1 verbatim replacement; ctest -R VisualRegression real-green-PASS post-migration.
- **Include path disambiguation**: `content/text/text_helpers.hpp` (15 LoC, exists) ≠ `include/chronon3d/text/text_helpers.hpp` (FILE_NOT_FOUND) ≠ `include/chronon3d/text/text_helpers_centered.hpp` (FILE_NOT_FOUND per sub-chore (i)). The actual `centered_text` definition lives at `content/text/text_helpers_centered.hpp` (148 LoC, content-text-scoped, not SDK-public). Test file includes `<content/text/text_helpers.hpp>` which transitively reaches the helper.
- **9 vs 5 user-spec estimate discrepancy**: user-spec was estimate; trinity-probe reveals actual count 9. Future sub-chores should pre-verify estimate with rg-probe before committing to estimate in user-spec format.
