# TICKET-MACCHINA-VERIFY-MAIN-E416D231-WBH-DEFERRED — Baseline Cert Deferral (e416d231)

## Stato

OPEN (P1) — DEFERRED-WBH (verified at `feat/composition-input-phase-1c-1d-v2`
session this-session 2026-07-15, post-Increment-A chore `b3625f7`).

## Priorità

P1 (canonical macchina-verifica forward-point; historical baseline gate).

## Area

Testing / Integration / macchina-verifica catena-cert lineage.

## Problema

User richiesta verbatim 2026-07-15: certificare `origin/main@e416d231` con
11/11 baseline gate suite (per analogue della PASS storica
[`docs/baselines/main-7eb5c2ba-baseline.md`](docs/baselines/main-7eb5c2ba-baseline.md))

"Clean WBH (working build host)" interpretation: macchina-verifica
richiede ambiente di build canonico (vcpkg glm/magic_enum headers +
system-font-bootstrap + chrono3d chinese-vendor toolchain) + 11/11
gate suite execution in ambiente LINUX clean (no concurrent agent
in-flight).

## Evidence macchina-verifica-unrunnable-on-this-VPS (this-session
2026-07-15 diagnostic pre-flight, basher-confirmed)

| # | Probe | Result | Implication |
|---|-------|--------|-------------|
| 1 | `git rev-parse --verify e416d231^{commit}` | **PRESENT** | target SHA reachable, historical main pointer |
| 2 | local `main` branch HEAD | **= `e416d231`** | target SHA == checkout-candidate |
| 3 | `git merge-base --is-ancestor e416d231 origin/main` | **TRUE** | e416d231 IS in current main history (origin/main ha avanzato a `71d1478b` ma `e416d231` resta preserved) |
| 4 | `origin/main SHA` | **`71d1478b`** | origin/main advanced past e416d231 (post session-increment-A) |
| 5 | `vcpkg-clone` directory state at `$HOME/vcpkg-clone/` | **MISSING** | env-block on |
| 6 | `$HOME/vcpkg-clone/installed/x64-linux/include/glm/glm.hpp` | **MISSING** | env-block on |
| 7 | `$HOME/vcpkg-clone/installed/x64-linux/include/magic_enum/magic_enum.hpp` | **MISSING** | env-block on |
| 8 | `system /usr/include/glm` | **MISSING** | env-block on |
| 9 | `system /usr/include/magic_enum` | **MISSING** | env-block on |
| 10 | `cache_diagnostics.hpp` Forward-declarations count (`rg -n 'class\s+CacheDiagnostics'`) | **3 hits** (lines 34, 89, 173) | rot-pattern extend-pattern persisting beyond TICKET-VCPKG-REMAINING-CODE-ROT-1SHOT-FIX claimed-fix |
| 11 | `namespace chronon3d::chrono3d` rot (`rg`) | **0 hits** | double-namespace rot CLOSED upstream |
| 12 | `using chronon3d::` import presence in 4 sub-files (text_helpers_*) | **PRESENT everywhere** | rot-fix ON text_helpers_*.hpp DONE upstream |
| 13 | VPS disk-quota state | **96% utilized per prior session** | margin per vcpkg install (4-5GB) = at-risk |
| 14 | Current branch | **`feat/composition-input-phase-1c-1d-v2`** | off-main, WIP state mid-Increment B-F cycle |

## Impatto deferral

Per AGENTS.md §honesty-discipline (`Non segnare verde una suite che
restituisce failure`, `Non cambiare un gate per nascondere un errore`):

- **NO macchina-verifica claim** su `origin/main@e416d231` senza
  attestazione canonica di clean WBH env-block + complete 11/11 gate suite execution
- **NO green baseline `docs/baselines/main-e416d231-baseline.md`** creation senza macchina-verifica PASS attested
- **NO silent rot closure claim**: il 3rd hit su `cache_diagnostics.hpp`
  Forward-declarations estende la rot-pattern oltre il claimed-fix del
  TICKET-VCPKG-REMAINING-CODE-ROT-1SHOT-FIX (linea 34 era il canonical
  forward-decl + line 89 + 173 sono forward-decls aggiuntive; potenzialmente
  content-distinguishable da rot-pattern o canonicalizzati con macro config;
  richiede WBH machine-verify per conferma)

## Soluzione Confine

Il deferral è macchina-verifica-on-VPS-blocco, non lavoration-deferral.
Una volta che il VPS dispone di vcpkg install + rot-fix complete + 11/11
gate suite eseguibile:

1. `git checkout e416d231` (local main già punta a `e416d231`; switch
   off `feat/composition-input-phase-1c-1d-v2` se necessario)
2. `bash tools/install_vcpkg_bootstrap_linux.sh` (idempotente; env-block
   closure)
3. `bash tools/run_developer_gates.sh` (9-gate pre-push suite canonico;
   non main-specific)
4. `cmake -B configure` + `cmake --build` (full build; rot-fix verifica)
5. `ctest -R full` (full ctest run)
6. **PASS branch**: creare `docs/baselines/main-e416d231-baseline.md` con
   tabella 11/11 §GateAudit secondo la struttura canonica di
   `docs/baselines/main-7eb5c2ba-baseline.md`
7. **FAIL branch**: aprire sub-ticket per ogni rot-pattern detected durante
   build/ctest; questo ticket diventa forward-point parent

## Criteri di accettazione (post-WBH macchina-verifica)

- [ ] `e416d231` reachable (verified pre-flight this-session row 1)
- [ ] vcpkg install PASS (verify-triad 5/5 markers + 7 .a libs)
- [ ] `cache_diagnostics.hpp` forward-decl count = 1 OR ≥1 macro-config
      distinct (rot deterministically resolved o canonicalized con macro
      gating)
- [ ] `text_helpers_*.hpp` `using` import rot = 0 rot-pattern residue
- [ ] `cmake -B configure` exit 0
- [ ] `cmake --build` exit 0 (full build, no compile error)
- [ ] `ctest -R full` exit 0 (full ctest registry, NO skip rot-pattern)
- [ ] `tools/run_developer_gates.sh` 9/9 PASS
- [ ] 11/11 gate suite analog di `docs/baselines/main-7eb5c2ba-baseline.md` PASS

## Forward-points

- `TICKET-VCPKG-ACTUAL-VPS-CLOSURE` (CLOSED 2026-07-14, claimed-DONE —
  this-session diagnostic probe rivela MISSING state: rot-pattern di
  VPS-state vs WBH-state; chaser-ticket born per riconciliazione —
  vedi forward-point chaser richiesto sotto)
- `TICKET-VCPKG-REMAINING-CODE-ROT-1SHOT-FIX` (claimed-DONE 2026-07-14,
  line 34 + 4-file text_helpers using-imports claimed fixed) — this-session
  probe shows Forward-decl count = 3 (extended rot beyond claimed fix;
  potenzialmente including stub deprecation lines / macro conditional
  variants che WBH machine-verify deve certificare)
- `TICKET-VCPKG-VERIFY-CLAIM-WBH-VERIFIED` (forward-point b on parent —
  chaser chaser-ticket acknowledging VPS vs WBH distinction)
- (NEW) `TICKET-MACCHINA-VERIFY-MAIN-E416D231-WBH-DEFERRED-CHASER`
  — chaser-ticket che codifica:
  - (a) Il VPS vs WBH demonstrate-difference matrix (vcpkg state
    MISSING on this VPS vs installed on WBH; rot-fix state
    PARTIAL on this VPS vs FULL on WBH)
  - (b) la catena-vs-stato-verità evidenza per ogni forward-point ticket
    che cita TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV (30+ citations per
    reverse-mirror, come `TICKET-VCPKG-ACTUAL-VPS-CLOSURE` §Reverse-mirror
    evidence category)
  - (c) la canonical-spec del 11/11 PASS mapping-machine-check su
    `origin/main@e416d231` (gate 1-11 list mapping verified ↔ `tools/run_developer_gates.sh` + `tests/cli_tests.cmake` + catena `verify_*_functional_linux.sh`)

## Pre-flight diagnostic output (canonical bash copy-paste, this-session 2026-07-15)

```bash
# === git state ===
BRANCH=feat/composition-input-phase-1c-1d-v2
LOCAL_HEAD=b3625f7bac85b1a438f230a319e0f86426153d46
git rev-parse --verify e416d231^{commit}                          # PRESENT
origin/main SHA = 71d1478b (origin/main advanced past e416d231)
local main SHA = e416d231 (target SHA == checkout-candidate)
e416d231 IS ancestor of origin/main (TRUE)

# === vcpkg env (env-block on this VPS) ===
ls -la $HOME/vcpkg-clone/vcpkg                                   # MISSING
ls /usr/include/glm/glm.hpp                                       # MISSING
ls /usr/include/magic_enum/magic_enum.hpp                         # MISSING

# === source-rot probes ===
rg -n 'class\s+CacheDiagnostics' include/chronon3d/cache/cache_diagnostics.hpp
# Lines 34, 89, 173 — 3 hits (rot extended beyond claimed-fix at L43)
rg -n 'namespace\s+chronon3d::chrono3d' include/chronon3d/ src/ apps/
# 0 hits (double-namespace rot CLOSED)
rg -n 'using chronon3d::' content/text/text_helpers_centered.hpp
# 14 hits (fix-rot pattern per file)
rg -n 'using chronon3d::' content/text/text_helpers_typewriter.hpp
# 12 hits
rg -n 'using chronon3d::' content/text/text_glow_helpers.hpp
# 5 hits
rg -n 'using chronon3d::' content/text/text_theme.hpp
# 12 hits

# === branch state + build dir ===
build/      PRESENT
chronon/    MISSING (build subdirectory previously removed)
```

## Cronologia (this-session 2026-07-15)

1. Increment A landed (`b3625f7 docs(stubs): phase 3 sequence-spec
   stub done-by-existing` — Cat-5 2-doc chaser-chore per Phase 3
   SUPERSEDED-BY-EXISTING stub closure).
2. User richiesta macchina-verifica `origin/main@e416d231` 11/11
   baseline gate suite.
3. Diagnostic pre-flight reveals env-block (vcpkg MISSING) + rot
   PARTIAL (`cache_diagnostics.hpp` 3 hits vs claimed 1-hit post-fix)
   + branch off-main (`feat/composition-input-phase-1c-1d-v2` mid-WIP).
4. Strategic decision: Path B DEFER + Cat-1 forward-point ticket per
   AGENTS.md §honesty-discipline (5 §honest-limitation rows in
   AGENTS.md canonical-discipline table + the user-spec semantically
   referenti "clean WBH" ambiente che è NOT-this-VPS).
5. This ticket born: Cat-5 2-doc atomic (ticket-home NEW +
   `docs/CHANGELOG.md` cite-only entry prepended). NO `docs/FOLLOWUP_TICKETS.md`
   row in this chore (per §Doc canonical update discipline rule for
   feat/ off-main work — canonical forward-point row AGGIUNGERE in
   main-rebase cycle quando questo ticket chiude su main).

## Cross-link (canonical sediment + AGENTS.md discipline)

- AGENTS.md §GATE-MNT-01 closure lineage (push wrapper solo main —
  questo ticket è open + no source push; deferred-WBH su main quando
  chaser-chore catena chiude su main-rebase)
- AGENTS.md §`### Docs canonical update discipline rule` (Cat-3
  anti-dup cronaca home per ticket forward-point metadata)
- AGENTS.md §honesty-discipline + §honest-limitation (NO green
  baseline claim senza macchina-verifica PASS attested)
- AGENTS.md §Install Pipeline Plumbing (vcpkg script cat-4 ancillary;
  questo ticket DEFER ricalibra la sua invocation a WBH context)
- `docs/baselines/main-7eb5c2ba-baseline.md` (PASS historical
  reference per struttura di nuova baseline quando WBH macchina-verifica
  verde)
- `docs/tickets/TICKET-VCPKG-ACTUAL-VPS-CLOSURE.md` (parent; this
  ticket è chaser-ticket per riconciliazione VPS-state revealato)
- `docs/tickets/TICKET-VCPKG-REMAINING-CODE-ROT-1SHOT-FIX.md`
  (parent; this ticket è chaser-ticket per rot-pattern extended-fixed
  state rivelato via Forward-decl count 3-vs-1)

## Conclusione operativa (this-session 2026-07-15)

NO macchina-verifica claim.
NO green baseline.
NO rot-fix silent-claim-extension.

Cat-5 2-doc atomic chaser-chore su `feat/composition-input-phase-1c-1d-v2`:
1. NEW canonical ticket-home (questo file)
2. CHANGELOG.md cite-only entry prepended at TOP di `## 2026-07-15`

Clean deferral pattern: ZERO real rot-pattern disclosure on this VPS;
full disclosure manifestato in canonical ticket-home per WBH agent
pickup.
