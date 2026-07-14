# TICKET-BENCH-CORPUS-TEST-DEFERRED — bench corpus 12-scene sanity test (deferred after 3-iter lost-commit cycle)

## Stato: DONE (2026-07-14)

Closes via companion impl chore [TICKET-BENCH-CORPUS-TEST-DEFERRED-IMPL.md](docs/tickets/TICKET-BENCH-CORPUS-TEST-DEFERRED-IMPL.md) (this chore = `feat(tests): bench corpus 12-scene sanity test`) + antecedent rot-fix commit `7461c70b fix(text): close TypewriterLayout/CompiledTypewriterGlyph namespace rot` (resolved the content/ build chain rot-pattern che bloccava `cmake --build --target chronon3d_content`). macchina-verifica DEFERRED-WBH per [TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV](docs/tickets/TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV.md); pre-push local-build verification PASS this session.

## Problema

Il chore `feat(tests): bench corpus 12-scene sanity test` (commit locali `7c0d0d2f`, `76b834db`, `228865f2` durante 3 iterazioni di recovery) non e riuscito a raggiungere `origin/main` per via di una systemic upstream race condition che ha prodotto `GATE_FAIL: HEAD and origin/main have diverged` durante ogni push attempt via `bash tools/wrap_push.sh origin main`.

## Cronaca 3-iteration lost-commit cycle

1. **Iter-1 (`7c0d0d2f`):**
   - commit locale `feat(tests): bench corpus 12-scene sanity test` (4 file iniziale: 2 NEW bench_corpus + tests/CMakeLists.txt wire-in + docs/tickets/TICKET-TEXT-ANIMATOR-WORKING-TREE-MIGRATION.md forward-point update)
   - bash tools/wrap_push.sh origin main GATE_FAIL diverged
   - SHA-triple diagnostic: lost-commit pattern confermato

2. **Iter-2 (`76b834db`):**
   - recovery §21ece2b3 variant: git reset --hard @\{u\} + git checkout OLD_COMMIT -- tests/bench_corpus/ + re-applied tests/CMakeLists.txt wire-in
   - commit fresh atop new upstream c61e6147
   - bash tools/wrap_push.sh origin main GATE_FAIL diverged AGAIN -- nuovo upstream afc27c39 nel mentre

3. **Iter-3 (`228865f2`):**
   - recovery cycle repeat: git pull --rebase origin main + new commit atop afc27c39
   - bash tools/wrap_push.sh origin main rejected: "Updates were rejected because the remote contains work that you do not have locally"
   - 9/9 developer gates PASSED consistent ma push cycle rot persiste

## Bail-out decision

Per AGENTS.md §honesty closure lineage + §21ece2b3 unique-edit recovery variant template + §Post-push SHA-selfcheck invariant: cycle >= 3 lost-push iterations = systemic upstream race (non "rare divergence"). Continuare a pushare = §honesty violation (agent reports "pushed" ma chore non raggiunge origin/main).

**Bail-out plan executed:**
- git reset --hard @\{u\} per drop locale (Cat-3 anti-dup OK: bench_corpus files non sono ancora parte di main).
- Backup file fuori-tree: /tmp/bench_corpus-stash/bench_corpus/ (preserva materiale per future agent).
- Documentazione deferral via questo ticket-home + docs/FOLLOWUP_TICKETS.md §Open Blockers row.

## Materiale preservato fuori-tree

/ tmp / bench_corpus - stash / bench_corpus /
├── CMakeLists.txt             (cMakeLists per tests/bench_corpus, INTEGRATION tier override + CONTENT feature gate)
└── test_bench_corpus_scenes.cpp  (12 TEST_CASEs su bench_b00..b11)

tests/CMakeLists.txt wire-in (bench_corpus/CMakeLists.txt line in CHRONON3D_BUILD_CONTENT-gated section) NON preservato fuori-tree (modifica 1-line; triviale da re-applicare in future session).

## Cross-link

- AGENTS.md §honesty closure (3-cycle lost-commit = bail-out canonical)
- AGENTS.md §21ece2b3 unique-edit recovery variant template
- AGENTS.md §Post-push SHA-selfcheck invariant
- AGENTS.md §Disciplina di aggiornamento dei canonici (FOLLOWUP_TICKETS per blocker apertura)
- tests/bench_corpus/test_bench_corpus_scenes.cpp (preserved in /tmp/bench_corpus-stash/)
- tests/bench_corpus/CMakeLists.txt (preserved in /tmp/bench_corpus-stash/)
- tests/CMakeLists.txt (preserved upstream at c61e6147, NO bench_corpus wire-in post-reset)

## Remediation blocks (per code-reviewer RED verdict)

### Sink-cost warning for future agents

NON ritentare il push entro 1 ora dall'ultimo GATE_FAIL diverged, a meno che @{u} non sia stabilmente fermo (verifica con git fetch origin && sleep 3600 && git log @{u} -1).

3 iterazioni consecutive di recovery cycle rot (7c0d0d2f -> 76b834db -> 228865f2) hanno bruciato >15 min del budget di sessione. Ogni nuovo tentativo senza upstream churn settling è waste-of-session + §honesty violation.

### /tmp ephemeral backup caveat

Il materiale di bench_corpus/ è preservato fuori-tree in /tmp/bench_corpus-stash/bench_corpus/. Questo percorso è EFFIMERO: ciclo di reboot o disk pressure cancellano i file. Per preservazione più robusta:
- (a) Salvare come git stash: git stash push -u -m stashed -bench-corpus-tests tests/bench_corpus/
- (b) Salvare come file gitignored: docs/.workbench/bench_corpus-stash-2026-07-14.tar.gz (gitignored)
- (c) Rileggere dal reflog l'orphan SHA 228865f2: il commit contiene byte-identical content. Cherry-pick puro fattibile senza backup esterno.

Opzione (c) è la più onesta per AGENTS.md §honesty closure.

### Forward-point 1 NOT-pivot

NON scambiare forward-point 1 (per-text Animator pipeline) per ridurre il bail-out. Lo stesso rischio race condition è presente perché forward-point 1 tocca gli stessi 5 preset headers come 3535cc7d upstream.

Better pivot candidates (scope-limitato, diversi path):
- (a) Chiudere TICKET-VCPKG-REMAINING-CODE-ROT-1SHOT-FIX (rot già tracciato upstream, fix isolato a content/text/text_helpers_typewriter.hpp).
- (b) Pure doc-only audit (rigenera baselines docs).
- (c) Questo TICKET-BENCH-CORPUS-TEST-DEFERRED (riprova quando @{u} stabilizza).

