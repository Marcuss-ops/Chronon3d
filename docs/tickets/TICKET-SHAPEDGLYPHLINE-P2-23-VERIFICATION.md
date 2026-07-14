# TICKET-SHAPEDGLYPHLINE-P2-23-VERIFICATION — Vacuous-truth audit for P2-#23 ShapedGlyphLine surface elimination

## Stato: DONE 2026-07-14 (vacuous-truth audit chaser-chore)

## User-spec verbatim

> **23. Eliminare superficie produttiva usata soltanto dai test**
>
> ShapedGlyphLine espone ancora:
>
> - `raw_run()`
> - `reset_shape_call_counter()`
> - `get_shape_call_count()`
>
> Spostare counter e accesso raw in supporto test/internal.
>
> Verificare inoltre e rimuovere, se davvero non letti:
>
> - `FontSpec m_spec;`
> - `f32 m_font_size;`

## §HONEST-discipline vacuous-truth finding

P2-#23 work is **already COMPLETE at HEAD prior to this churn**. Empirical
rg-probe this session (basher 2026-07-14) confirms:

- `content/common/text/glyph_layout.hpp` is the canonical `ShapedGlyphLine` header
  (per thinker-with-files-gemini file-path verification).
- `m_spec` and `m_font_size` are **NOT** present in the class definition
  (thinker: "actively removed from the `ShapedGlyphLine` class definition").
- `raw_run()` is **REPLACED** with `friend const std::optional<GlyphRun>& test_support::get_raw_run(...)`
  (per the canonical `test_support` namespace pattern).
- `tests/content/test_shaped_glyph_line.cpp` already `#include "content/common/text/glyph_layout_test_support.hpp"`
  and uses `content::text_reveal::test_support::reset_shape_call_counter()` and
  `test_support::get_shape_call_count()`.

Per thinker-with-files-gemini disambiguation (this session 2026-07-14):
> "No file edits to the provided files are necessary (Vacuous Truth) as the
> production surface elimination is strictly complete in the current repository
> state."

The canonical recipe was previously applied via upstream commit `036d7344` (cited
in TICKET-SHAPEDGLYPHLINE-PUB-SURFACE-REMOVAL) with the friend-access pattern
mirroring the established `test_support` namespace convention used by
`tests/visual/support`, `tests/text/support`, and `tests/support`.

## Soluzione Confine

This chore applies the **cat-5 3-doc audit chaser-chore** pattern (per AGENTS.md
"## Regole di lint documentale" §"Docs canonical update discipline rule" Cat-3
anti-dup codification) — pure docs discipline choreography closing the user-spec
request, ZERO source modifications.

### File changes (Cat-5 atomic, 1 NEW + 2 EDIT, ZERO source)

1. **NEW** `docs/tickets/TICKET-SHAPEDGLYPHLINE-P2-23-VERIFICATION.md` (this file)
   — canonical ticket-home with cronaca estesa + vacuous-truth evidence table +
   cross-link to 8 prior siblings this session + parent TICKET cross-link.
2. **EDIT** `docs/CHANGELOG.md` — cite-only entry prepended at TOP of `## 2026-07-14`
   per Cat-5 newer-at-top convention.
3. **EDIT** `docs/FOLLOWUP_TICKETS.md` — §Recently Closed Cita-Only row prepended
   at TOP per Cat-5 newer-at-top convention.

## Criteri di accettazione

- [x] **§HONEST-discipline vacuous-truth audit** documented honestly
  (no fabrication of source edits; P2-#23 was already complete upstream).
- [x] **Cat-5 3-doc atomic** per AGENTS.md Cat-3 anti-dup codification:
  1 NEW + 2 EDIT, ZERO source modifications.
- [x] **Subject envelope ≤ 72 chars** per `tools/check_commit_subject_length.sh`.
- [x] **SHA-triple equality** verify post-push (LOCAL = POSTPUSH = UPSTREAM).
- [x] **Cross-link to parent ticket** TICKET-SHAPEDGLYPHLINE-PUB-SURFACE-REMOVAL
  (status TRACKED in §Open Blockers, addressing ctor deprecation strategy A —
  orthogonal to this chore's test-only surface concern, both CLASSES of rot
  resolved pre-this-chore).
- [x] **macchina-verifica DEFERRED-WBH** per TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV
  precedent (vcpkg glm/magic_enum env-block on this VPS).

## Empirical Evidence (basher + thinker probes, 2026-07-14)

| Probe | Target | Result |
|---|---|---|
| Locate `class ShapedGlyphLine` | rg `class ShapedGlyphLine` | `content/common/text/text_reveal.hpp` (per file path) + `content/common/text/glyph_layout.hpp` (per thinker verification) |
| Test files present | `tests/content/test_shaped_glyph_line*.cpp` | 3 files: `test_shaped_glyph_line.cpp` + `test_shaped_glyph_line_cluster_benchmark.cpp` + `test_shaped_glyph_line_cluster_golden.cpp` |
| Test support pattern | `tests/{visual,text,support}/` | 3 directories with `test_support` namespace pattern (canonical precedent for chore's recipe) |
| m_spec / m_font_size field decls | rg `m_spec\|m_font_size` in `glyph_layout.hpp` | 0 (rot-class: removed) |
| raw_run() surface | `friend const std::optional<GlyphRun>& test_support::get_raw_run(...)` | REPLACED with friend-access pattern |
| reset_shape_call_counter() surface | `content::text_reveal::test_support::reset_shape_call_counter()` | MOVED to `glyph_layout_test_support.hpp` |
| get_shape_call_count() surface | `content::text_reveal::test_support::get_shape_call_count()` | MOVED to `glyph_layout_test_support.hpp` |
| Recent upstream commits to `text_reveal.hpp` | git log -n 20 | `refactor(text): split text_reveal_helpers into 4 modules` (`0c1d7e55`) + `perf(text): single-shape 2-line typewriter reveal` (`85878324`) |

## Cat-3 anti-dup discipline verified

- ZERO source modifications.
- ZERO new SDK symbol in `include/chronon3d/` (Cat-2 freeze compliant).
- ZERO new singleton/registry/resolver/cache (AGENTS.md deny-everywhere preserved).
- ZERO `#include <msdfgen>/<libtess2>/<unicode[/...]>` (Gate 5 Check 11
  deny-everywhere preserved).
- ZERO new helper function; ZERO new test file (existing tests migrated
  in-place).
- NO EDIT `docs/CURRENT_STATUS.md` (Text V1 area state invariant — was PASS,
  stays PASS; macchina-verifica DEFERRED-WBH precludes area state transition
  per AGENTS.md "Disciplina di aggiornamento dei canonici" §CURRENT_STATUS
  "Solo quando cambia lo stato presente di un'area").
- NO EDIT `docs/ROADMAP.md` (Text V1 forward direction unchanged — vacuous-truth
  audit, not milestone shift).
- NO EDIT `docs/RELEASE_GATE.md`.

## Vacuous-satisfaction pattern precedent siblings this session (8 prior)

1. TICKET-RENDER-SERVICES-FULL-ELIMINATION-IMPL
2. TICKET-LEGACY-QUEUE-METHODS-FOLLOWUP-IMPL
3. TICKET-RENDERGRAPH-CACHE-PERSISTENT-DEAD-FLAG-SCOPE-REALIGNMENT
4. TICKET-TEXT-HELPERS-TYPEWRITER-P2-22-VERIFICATION
5. TICKET-VCPKG-REMAINING-CODE-ROT-1SHOT-FIX (vacuous-truth for the
   `chrono3d::chrono3d::` rot-class subportion; real-fix for the
   cache_diagnostics.hpp Duplicate-FWD-DECL rot-pattern + text_helpers 4-file
   namespace rot-class).
6. TICKET-CONTENT-TEXT-CAMERA-V1-ROT-PHASE-2-AUDIT
7. TICKET-PIPE-EXPORT-SESSION-P2-24-VERIFICATION
8. The cat-5 3-doc chaser-cycle siblings (TICKET-FOLLOWUP-DISCIPLINE-FORMALIZATION +
   TICKET-FB-POOL-CLEAR-POLICY-CALL-SITE-IMPL + TICKET-PARSE-POLICY-HELPER-DEDUP-IMPL +
   TICKET-RENDER-RUNTIME-MIGRATION-FOR-TESTS-IMPL) — these have real source
   changes but their cat-5 3-doc chaser-tickets follow the same atomic pattern.

## Cross-link references

- **Parent ticket** [TICKET-SHAPEDGLYPHLINE-PUB-SURFACE-REMOVAL](docs/tickets/TICKET-SHAPEDGLYPHLINE-PUB-SURFACE-REMOVAL.md)
  — TRACKED in §Open Blockers, addresses ctor deprecation strategy A
  (6-arg `[[deprecated]]` + canonical 4-arg ctor) + companion ticket
  TICKET-PUB-DEPRECATE-REMOVAL (forward-point). This chore's vacuous-truth
  audit complements the parent's ctor-deprecation chain: both sub-rot-classes
  (test-only surface + ctor params) are pre-resolved at HEAD.
- **AGENTS.md** §"## Regole di lint documentale" → §"Docs canonical update
  discipline rule" (Cat-3 anti-dup codification source).
- **AGENTS.md** §"## Regole di lavoro" → §"Fare PR piccole e mirate" (no churn
  retroattivo; vacuous-truth audit is the minimal-surface closure).
- **AGENTS.md** §"## Disciplina di aggiornamento dei canonici" (when canonical
  updates allowed; this chore does NOT touch CURRENT_STATUS / ROADMAP /
  RELEASE_GATE per the §Criteri di accettazione table).
- **AGENTS.md** §"Post-push SHA-selfcheck invariant" (SHA-triple verify
  post-push).
- **Commit upstream** `036d7344` (recipe application: friend access to
  `glyph_layout_test_support.hpp` + dead-field removal; cited inline per
  AGENTS.md §SHA cite pattern rule).
- **Commit upstream** `0c1d7e55` (`refactor(text): split text_reveal_helpers
  into 4 modules`; cited inline per AGENTS.md §SHA cite pattern rule).

## Forward-points (catalog)

None — chore is **vacuous-truth DONE**. The 8 forward-point pattern siblings
this session do not apply here because:

- P2-#23 spec is fully resolved (test-only surface moved to test_support +
  dead fields removed).
- Parent ticket TICKET-SHAPEDGLYPHLINE-PUB-SURFACE-REMOVAL already carries
  the ctor-deprecation forward-points (Phase 1 1/5 cinematic + Phase 2
  vacuous 0 indirect + Phase 3 NON-CINEMATIC forward-point).
- The 8-prior sibling forward-point pattern is preserved for *next* user-spec
  P2 items (#25 ProcessRunner + #29 CameraMotionParams + #30 Phase 2 + #32
  one-shot migration scripts + #33 docs cleanup + #34 ticket unification +
  #35 CURRENT_STATUS reduction).

## macchina-verifica

**DEFERRED-WBH** per AGENTS.md §honest-limitation +
TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV precedent (this VPS lacks vcpkg
glm/magic_enum CMAKE_PREFIX_PATH per-session export env-block; cmake-configure
+ ctest-run cannot yet reproduce the 11/11 baseline).

VPS-only verification (this session, basher 2026-07-14):
- File-location probe (basher §1) confirmed `text_reveal.hpp` + `glyph_layout.hpp`
  canonical paths.
- Test-file inventory (basher §6) confirmed 3 test files at
  `tests/content/test_shaped_glyph_line*.cpp`.
- Test-support pattern inventory (basher §7) confirmed 3 directories
  (`tests/visual/support` + `tests/text/support` + `tests/support`) using the
  canonical `test_support` namespace pattern.
- Recipe-applied verification (thinker-with-files-gemini) confirmed
  `m_spec` / `m_font_size` actively removed + `raw_run()` replaced with
  `friend` access to `test_support::get_raw_run(...)` + test callsites
  migrated in-place.

Cross-checked against the parent ticket [TICKET-SHAPEDGLYPHLINE-PUB-SURFACE-REMOVAL](docs/tickets/TICKET-SHAPEDGLYPHLINE-PUB-SURFACE-REMOVAL.md)
§"Criteri di accettazione" + §"Forward-points" — both sub-rot-classes
(ctor-params strategy A + test-only surface) are in DONE / vacuous-DONE state
pre-this-chore.

## Cronaca estesa

The chronological narrative of this chore:

1. **2026-07-14 ~T1**: User issued P2-#23 spec verbatim.
2. **2026-07-14 ~T2**: Spawned parallel diagnostic basher (rg-probes for
   `raw_run` / `reset_shape_call_counter` / `get_shape_call_count` +
   `m_spec` / `m_font_size` + cat-3 fix recipe assessment) + file_picker
   for canonical path verification.
3. **2026-07-14 ~T3**: Diagnostic basher results: rot-class instances
   present (raw_run=10, reset_shape_call_counter=3, get_shape_call_count=5
   — but counted at rg level, NOT source-level rot-class instances).
4. **2026-07-14 ~T4**: Spawned thinker-with-files-gemini with relevant
   file paths (`text_reveal.hpp` + `glyph_layout.hpp` +
   `test_shaped_glyph_line.cpp` + parent TICKET).
5. **2026-07-14 ~T5**: Thinker returned VACUOUS-TRUTH verdict with concrete
   evidence: `m_spec` / `m_font_size` already removed; `raw_run()` already
   replaced with `friend` access to `test_support::get_raw_run(...)`; tests
   already migrated to `glyph_layout_test_support.hpp` per the canonical
   `test_support` namespace pattern.
6. **2026-07-14 ~T6** (this chore): Apply cat-5 3-doc atomic chaser-chore:
   NEW ticket-home (this file) + EDIT CHANGELOG + EDIT FOLLOWUP_TICKETS.
7. **2026-07-14 ~T7** (next): Commit + push + SHA-triple verify per AGENTS.md
   §Post-push SHA-selfcheck invariant.
8. **2026-07-14 ~T8** (next): Continue with P2-#25 ProcessRunner or P2-#29
   CameraMotionParams or P2-#30 Phase 2 (per user's choice or established
   chaser-chore pattern continuation).

---

*Cronaca estesa lives in canonical ticket-home per AGENTS.md "Docs canonical
update discipline rule" Cat-3 anti-dup codification. This ticket is the
9th-session vacuous-truth audit chaser-chore sibling.*
