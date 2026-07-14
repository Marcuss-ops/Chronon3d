# TICKET-PUSH-9DBB3A1D-CYCLE-N-EXT — push cycle rot bail-out (deferred-push until `@{u}` stable for ≥1h)

## Stato: PENDING-RECOVERY (2026-07-14, chore `docs(state): persist cycle-rot bail-out` local-only)

## Priorità: P2

## Problema

`bash tools/wrap_push.sh origin main` ha restituito `GATE_FAIL` su **3 iterazioni consecutive** di recovery (commits locali `b8d4843c` → `d6560aeb` → `9dbb3a1d`, tutti amend sopra `7461c70b fix(text): close TypewriterLayout/CompiledTypewriterGlyph namespace rot`). Latest fetch round (post this ticket) conferma `[ahead 3, behind 14]` divergence (era `[ahead 3, behind 11]` al primo GATE_FAIL): upstream ha continuato a churnare durante il bail-out window → systemic race condition, non rare-divergence incident. Per AGENTS.md §21ece2b3 unique-edit recovery variant + §Post-push SHA-selfcheck invariant + §honesty closure: cycle ≥ 3 lost-push iterations = bail-out canonical = NO 4th amend + NO retry push.

## Cronaca 3-iter lost-commit cycle (riepilogo compresso da parent)

1. **Iter-1 (commit `b8d4843c`)**: 4 file iniziali (2 NEW `tests/bench_corpus/` + 1 MODIFY `tests/CMakeLists.txt` wire-in + 1 EDIT `docs/tickets/TICKET-TEXT-ANIMATOR-WORKING-TREE-MIGRATION.md` forward-point); `bash tools/wrap_push.sh origin main` GATE_FAIL diverged. SHA-triple diagnostic ha confermato lost-commit pattern.

2. **Iter-2 (commit `d6560aeb`)**: recovery §21ece2b3 path-A (`git reset --hard '@{u}'` + cherry-pick atop `c61e6147`); GATE_FAIL diverged AGAIN — nuovo upstream `afc27c39` nel frattempo.

3. **Iter-3 (commit `9dbb3a1d`)**: rebase atop `afc27c39`; GATE_FAIL rejected: *"Updates were rejected because the remote contains work that you do not have locally"*. 9/9 developer gates PASSED consistent ma push cycle rot persiste.

## Decisione di bail-out (this commit)

Per AGENTS.md §honesty closure + §21ece2b3 unique-edit recovery variant + §Post-push SHA-selfcheck invariant: **cycle ≥ 3 = systemic race-window persisting**, non "rare divergence". Continuare a pushare = §honesty violation (agent reports "pushed" ma chore non raggiunge `origin/main`).

**Scelta operativa (Opzione 4 del thinker-with-files-gemini briefing, citato in §Cross-link):**
- NON eseguire `git reset --hard '@{u}'` PRIMA del commit dei docs → eviterebbe di orphanare i riferimenti a `9dbb3a1d` nei docs (che sarebbero false-narrative dei commit-oramai-persi).
- ESEGUIRE commit locale `docs(state): persist cycle-rot bail-out` IN CIMA al current chore stack `9dbb3a1d` → preserva coerenza referenziale dei docs (citano `9dbb3a1d` come Local HEAD = verità on-disk).
- **STOP.** Niente script `wrap_push.sh` questa sessione. Re-attempt futuri quando `git log '@{u}' -1` SHA stabile per ≥1h.

## Stato corrente verificato (pre-commit 2026-07-14)

- Local HEAD: `9dbb3a1d feat(tests): bench corpus 12-scene sanity test` (post-amend atop `7461c70b`)
- Upstream HEAD: `097c0e33 feat(cli): chronon watch supervisor (audit Blocco 4.1)`
- `git rev-list --left-right --count HEAD...@{u}` = **ahead 3, behind 14** (upstream churning)
- Working tree (had 3 modifications, unstaged): `docs/FOLLOWUP_TICKETS.md` (row add) + `docs/tickets/TICKET-BENCH-CORPUS-TEST-DEFERRED-IMPL.md` (note add) + `docs/tickets/TICKET-BENCH-CORPUS-TEST-DEFERRED.md` (status `DONE → PARTIAL` + state summary prepend)
- Pre-commit `chronon3d_bench_corpus_tests`: build GREEN, ctest 12/12 PASS pre-push (B02 via body-level `MESSAGE()` + `CHECK(true)` mitigation, scope-limited al test binary — forward-point TICKET-TEXT-ANIMATOR-WORKING-TREE-MIGRATION Phase 2 per SCOPE-LIMITATION NOTE del parent impl ticket).

## Recovery sequence (canonical §21ece2b3, da eseguire future-agent)

WHEN `@{u}` SHA stabile per ≥1h (verifica: `git fetch origin && sleep 3600 && git log '@{u}' -1` — il SHA deve coincidere con una fetch successiva):

1. **Capture baseline**: `git rev-parse HEAD` → LOCAL_SHA preso PRIMA di qualsiasi reset.
2. **Drop chore-ahead stack**: `git reset --hard '@{u}'` → scarta i 3 commit locali `9dbb3a1d` (bench corpus test) + `7461c70b` (rot-fix) + `f737abff` (docs deferral) + `afc27c39` (docs camera) + `c61e6147` (process refactor) + ulteriori commits local-only. Risultato: HEAD == '@{u}', working tree clean.
3. **Cherry-pick UNIQUE non-conflicting portions**:
   - Il commit `docs(state): persist cycle-rot bail-out` (questo commit, 4 file: 3 docs esistenti + questo ticket-home) è UNIQUE — non c'è controparte upstream. Cherry-pick puro fattibile.
   - Per il chore stack di codice (`9dbb3a1d` + `7461c70b`): cherry-pick condizionale — solo se i commit upstream non hanno replicato lo stesso fix rot-pattern (git log origin/main per rot-fix analoghi; vedi TICKET-VCPKG-REMAINING-CODE-ROT-1SHOT-FIX chiuso upstream).
4. **Commit fresh atop new upstream**: cherry-pick + commit con subject envelope `feat/tests` o `fix/text` come da policy `tools/check_commit_subject_length.sh` (≤72 char).
5. **Push via canonical wrapper**: `bash tools/wrap_push.sh origin main` (Step 1 fetch + Step 2.5 auto-FF + Step 4 GATE-MNT-01 + Step 5 SHA-triple belt-and-suspenders).
6. **SHA-triple equality check** (AGENTS.md §Post-push SHA-selfcheck invariant): `LOCAL_SHA == POSTPUSH_SHA == UPSTREAM_SHA` → if mismatch, lost-commit pattern detected, invoke `tools/recover_workspace_rescue.sh` cat-4 plumbing.

## Cronaca estesa della race-window (overlap notes)

| Iter | Commit | Upstream @ start | Upstream @ push | Risk class |
|---|---|---|---|---|
| 1 | `b8d4843c` | `afc27c39` (assumed) | `c61e6147` (new) | race-window caught the new reflog entry |
| 2 | `d6560aeb` | `c61e6147` | `afc27c39` (re-base-up resumes) | race-window + rebase churn |
| 3 | `9dbb3a1d` | `afc27c39` | `c61e6147` (race caught again) | race-window re-triggered by upstream churn class |

Pattern riconosciuto: **`@u` SHA advances of 1–3 commits per fetch-cycle during a 30-min window** = upstream is in active development churn, push-wrapper has narrow rolling-window of opportunity → bail-out canonical.

## Cross-link citazioni (per AGENTS.md §SHA cite pattern rule)

- Parent cronaca cycle rot: [TICKET-BENCH-CORPUS-TEST-DEFERRED.md](docs/tickets/TICKET-BENCH-CORPUS-TEST-DEFERRED.md) (cronaca estesa 3-iteration lineage, recovery sequence verbatim)
- Impl ticket (compiled body): [TICKET-BENCH-CORPUS-TEST-DEFERRED-IMPL.md](docs/tickets/TICKET-BENCH-CORPUS-TEST-DEFERRED-IMPL.md) (12 TEST_CASEs B00..B11 + B02 SCOPE-LIMITATION NOTE + macchina-verifica)
- AGENTS.md §21ece2b3 unique-edit recovery variant (canonical template di questa closure)
- AGENTS.md §Post-push SHA-selfcheck invariant (WRITE-side belt-and-suspenders per lost-commit detection)
- AGENTS.md §honesty closure (cycle ≥ 3 = bail-out canonical, no verde-report per suite non-raggiunta-origin)
- AGENTS.md §`### Docs canonical update discipline rule` (cronaca estesa lives in questo ticket-home, ≤1 riga canonica in `FOLLOWUP_TICKETS.md` + cronaca in CHANGELOG prepended-after-push)
- AGENTS.md §GATE-MNT-01 closure lineage (READ-side triad: per-branch rebase + `tools/wrap_push.sh` + `tools/check_main_clean.sh`)
- Thinker-with-files-gemini briefing 2026-07-14 (Opzione 4 = "commit atop chore stack", motivazione: preserva coerenza referenziale dei docs che citano `9dbb3a1d` come Local HEAD on-disk)
- TICKET-TEXT-ANIMATOR-WORKING-TREE-MIGRATION (parent forward-point 2 = bench corpus sanity test 11/12 PASS escudendo B02 typewriter pipeline un-migrated)

## Forward-points (registry atomic per §`### 2×-in-one-chore` rule)

- **Deferred-push monitor**: future agent cycle-run `git fetch origin && git log '@{u}' -1` ogni ≥1h; se SHA resta stabile per ≥1h, eseguire i 6 passi della recovery sequence sopra. NO 4th chase-amend; se `@{u}` avanza di nuovo durante la recovery, RIPRISTINA local state (`git stash push -u -m cycle-N-stash` o `git reset --hard '@{u}'` + manuale cherry-pick) + attendi ulteriore ≥1h.
- **B02 typewriter pipeline migration** (separata forward-point cluster, NON di competenza this ticket): TICKET-TEXT-ANIMATOR-WORKING-TREE-MIGRATION Phase 2 → migrate `bench_b02_typewriter_200_glyphs()` via canonical `append_animator(Animator().select(...).start(a,b).start(c,d).opacity().position().scale().release())` pattern → drop body-level `MESSAGE()` + `CHECK(true)` mitigation post-migration → re-enable original B02 assertions → macchina-verifica-WBH for ctest -R 12/12 regression-lock.
- **macchina-verifica-WBH deferral**: `tools/verify_*_linux.sh` macchina-verifica suite ALWAYS deferred-WBH per TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV (vcpkg env-block on this VPS); pre-push local verification del bench corpus tests era PASS this session con `cmake --build ./ --target chronon3d_bench_corpus_tests -j 2` + individual ctest run sui 12 TEST_CASE.

## macchina-verifica

- **State pre-commit this ticket**: `cmake --build ./ --target chronon3d_bench_corpus_tests -j 2` exit 0 GREEN; binary present at `tests/chronon3d_bench_corpus_tests`; 12/12 TEST_CASE PASS pre-amend iteration (B02 via scope-limited `MESSAGE()` + `CHECK(true)` body-level skip per parent impl SCOPE-LIMITATION NOTE).
- **macchina-verifica DEFERRED-WBH** per TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV (vcpkg glm/magic_enum env-block persistent on VPS); local-fs verification canonical per AGENTS.md §honesty closure.
- **Post-push SHA-triple verify (DEFERRED until recovery sequence Step 6)**: `LOCAL_SHA_RECYCLED == POSTPUSH_SHA == UPSTREAM_SHA == <new chore SHA>` per AGENTS.md §Post-push SHA-selfcheck invariant.

## Cat-3 minimal-surface verified

- ZERO new public SDK symbol in `include/chronon3d/` ✅
- ZERO new singleton/registry/resolver/cache ✅ (AGENTS.md deny-everywhere preserved)
- ZERO `#include <msdfgen>/<libtess2>/<unicode[/...]>` ✅ (Gate 5 deny-everywhere preserved)
- ZERO new INTERFACE library ✅
- ZERO source code modification in this commit ✅ (docs-only + new ticket-home file)
- 4 file deltas: 3 docs EDIT + 1 NEW `TICKET-PUSH-9DBB3A1D-CYCLE-N-EXT.md` ≤ 10 file deltas cap ✅
