# TICKET-FOLLOWUP-TICKETS-REDUCTION-AUDIT-2026-07-14 — Reduce §Open Blockers to ≤10 rows

## Stato

OPEN (2026-07-14, partial-cover) — Phase 1 DONE this chore; Phase 2 deferred per per-row macchina-verifica.

## Problema

`docs/FOLLOWUP_TICKETS.md` §Open Blockers table listed 18 (basher-verified) — exceeding the header convention
`(≤10)`. The overage reflects ~3 months of valid blocker-intent inventory but the table size violates the
"`§Open Blockers (≤10)` table-per-area" convention declared in `docs/FOLLOWUP_TICKETS.md` line 7.

User-directive verbatim (2026-07-14):

> Riduci i 17 row di §Open Blockers in docs/FOLLOWUP_TICKETS.md verso la soglia ≤10: chiudi ticket
> già-DONE che sono solo blocker-indice residuali (es. TICKET-NODE-CACHE-KEY-COLLAPSE-ROT se rot è
> risolto upstream), travasa ticket deferred in §Deferred / Lower Priority, e deduplica i 4 sub-row
> identitari di TICKET-EXECUTOR-TILE-PRUNE-SKIP-UNIFICATION che sono già storicamente chiusi
> (muoverli a §Recently Closed). Agent reminder: AGENTS.md ≥ HONEST 11/11 baseline preservation
> discipline — non chiudere senza macchina-verifica.

## Vincolo §HONEST baseline preservation discipline

Per AGENTS.md §`HONEST baseline preservation discipline`:

> Non chiudere una suite che restituisce failure. Non cambiare un gate per nascondere un errore.

Combined with the per-commit-macchina-verifica pattern (AGENTS.md §HONEST discipline applied at
ticket-closure level): a ticket can be CLOSED only after evidence demonstrates the underlying
rot/code state matches the closure claim. **NO closure without macchina-verifica** in this audit.

## Evidenza — Phase 1 (this chore, `7e53791`+)

| # | Action | Rows Delta | §HONEST discipline implication |
|---|---|---|---|
| 1 | DEDUP `TICKET-TOOLS-ORPHAN-AUDIT` + `TICKET-TEXT-SPEC-FORWARDER-REMOVAL` 2nd occurrences + intermediate `\|---\|---\|---\|---\|` separator row | **−3** | Structural-only, NO state change. Both pairs are byte-identical (cat-3 catena-collapse). |
| 2 | Status-preserving §Deferred migration: `TICKET-PREMULT-TEST-SWEEP` (P2 OPEN) + `TICKET-PREMULT-CALLER-AUDIT` (P2 OPEN) | **−2** | Status/priority preserved (= OPEN at P2). Forward-points are inherently-deferred work — migration to §Deferred is a *metadata reorganization*, NOT a state change. NO macchina-verifica required (per AGENTS.md §`### Disciplina di aggiornamento dei canonici` §Deferred = "in attesa di macchina-verifica futura"). |
| 3 | User-cited `TICKET-EXECUTOR-TILE-PRUNE-SKIP-UNIFICATION` 4-sub-row deduplication | **0** | Already consolidated in §Recently Closed as single-row `TILE-PRUNE-SKIP-UNIFICATION lineage (4 cronache)` per prior cat-5 3-doc commit (grep-verified: only ONE row references the lineage). NOTHING TO DO. |

**Result**: 18 → 13 rows in §Open Blockers (this chore + push, SHA-triple verified).

## Criteri di accettazione — Phase 2 (forward-points per residual overage rows)

| Residual row | Status (basher-verified 2026-07-14) | Forward-point decision rule | Phase-2 ticket |
|---|---|---|---|
| `TICKET-NODE-CACHE-KEY-COLLAPSE-ROT` | P2 OPEN (no ticket-home file) | User-cited candidate IF upstream rot is resolved. macchina-verifica via `rg -c 'NodeCacheKey' src/render_graph/executor/node_executor.cpp` + `cmake --build build/chronon/linux-content-dev --target chronon3d_node_executor_tests 2>&1 \| grep -i 'NodeCacheKey.*not found'` → if NodeCacheKey IS recognized AND build is rot-free, closure candidate. | (open as Phase-2 cat-1) |
| `TICKET-CHANGELOG-UPSTREAM-MARKERS-FIX` | P0 OPEN (no ticket-home file) | CHANGELOG conflict markers audit via `rg -nE '^(<<<<<<<\|=======\|>>>>>>>)' docs/CHANGELOG.md` → macchina-verifica on whether markers remain. Per AGENTS.md §Disciplina di aggiornamento dei canonici P0 = highest-priority and must surface visible P0 evidence before action. | (open as Phase-2 cat-1) |
| `TICKET-COMPLETENESS-GATE-V2-FIX-FORWARD` | P1 OPEN (no ticket-home file) | macchina-verifica `tools/verify_completeness_gate_v2_linux.sh` exit 0 / N.3 gating wire-in status; per `dd37f28e` prior push-block. | (open as Phase-2 cat-1 with strict macchina-verifica) |
| `TICKET-125-TEST-AGGREGATOR` | P0 OPEN (no ticket-home file) | aggregator catalog (`docs/tickets/TICKET-125.md`) Tests 8-18 status macchina-verifica per-row. | (open as Phase-2 cat-1 — already-cited precedent row in CURRENT_STATUS.md) |
| `TICKET-CERT-SEQUENCE-WBH-PROTOCOL` | P1 OPEN (no ticket-home file) | CURRENT_STATUS.md already cites `WBH-DEFERRED`; macchina-verifica per `docs/cert_sequence_wbh_protocol.md` execution on working-build-host. WBH-only, NOT this-VPS. | (open as Phase-2 cat-1 — keep §Open Blockers P1 OPEN until WBH execute) |
| `TICKET-NODE-MEMORY-METRICS` | P1 OPEN (ticket-home exists, Stato TODO OPEN) | macchina-verifica: ticket-home canonical Status + chrono-immutability. The ticket-home is the canonical state-tracking mechanism per AGENTS.md §`## Discipline di aggiornamento dei canonici` (forward-point: ADR-026 prerequisite). | (open as Phase-2 cat-1 + ADR-026 wire-in) |
| `TICKET-PERSISTENT-CACHE-ADR-GAP` | P1 OPEN (no ticket-home file) | Per CHANGELOG §`### chore(cache): scope-realignment ...` (2026-07-14 entry in §2026-07-14): bridge already removed upstream by PR2-cleanup lineage. macchina-verifica via `rg -nE 'FrameInvariantPersistent\|static_persistent_cache\|node-cache-policy.*persistent' include/chronon3d/cache/` → if 0 matches, ticket premise OBSOLETE → closure candidate. | (open as Phase-2 cat-1 vacuous-truth audit) |
| `TICKET-SIMD-REGISTRY-CANONICAL` | P1 NEW (ticket-home exists per basher) | ADR-025 prerequisite (per CURRENT_STATUS.md). Per AGENTS.md v0.1 NO new SDK API without ADR. macchina-verifica: `docs/adr/ADR-025-*.md` exists OR forward-point ADR creation. | (open as Phase-2 cat-1 tied to ADR-025) |
| `TICKET-BENCHMARK-CORPUS-OFFICIAL` | P1 OPEN (no ticket-home file) | 12 YAML scene descriptors B00–B11 in `configs/benchmarks/corpus/`. Per CURRENT_STATUS.md cited as Test #15 forward-point — substantial work, NOT a closure candidate. | (open as Phase-2 cat-1 — substantive chore, NOT closure-candidate) |
| `TICKET-P1E-CPU-BUDGET-MEASUREMENT` | P1 HARNESS-COMPLETE (ticket-home exists) | Per CURRENT_STATUS.md: HARNESS-COMPLETE on this VPS; macchina-verifica DEFERRED-WBH. Move to §Recently Closed Cita-Only if WBH executes (forward-point tracked elsewhere). | (open as Phase-2 cat-1 — keep §Open Blockers P1 OPEN UNTIL WBH macchina-verifica executed) |
| `TICKET-SHAPEDGLYPHLINE-PUB-SURFACE-REMOVAL` | P2 TRACKED (just added, ticket-home exists) | Status `TRACKED` = mid-cycle; preserves §HONEST discipline while signalizing forward intent. Closure criterion: post-V0.2 cut (per ticket-home §Criteri di accettazione). | (NO Phase-2 action — TRACKED is correct mid-cycle state) |
| `TICKET-PUB-DEPRECATE-REMOVAL` | P2 TRACKED (just added, ticket-home exists) | Status `TRACKED` = mid-cycle; closure criterion: ≥10/12 SDK consumer tests pass without the bridge 6-arg at V1 cut (per ticket-home). | (NO Phase-2 action — TRACKED is correct mid-cycle state) |

**Phase-2 budget**: per ticket-home macro-verifica alone is 11 distinct macchina-verifica steps
(11 cats-1 chore cycles per AGENTS.md §"Fare PR piccole e mirate"). Aggregate to ~3–4 Phase-2 chores:
**Phase-2-A** (closure audits: `TICKET-NODE-CACHE-KEY-COLLAPSE-ROT` + `TICKET-PERSISTENT-CACHE-ADR-GAP`) +
**Phase-2-B** (P0 critical evidence: `TICKET-CHANGELOG-UPSTREAM-MARKERS-FIX` + `TICKET-125-TEST-AGGREGATOR`)
+ **Phase-2-C** (P1 evidence: `TICKET-COMPLETENESS-GATE-V2-FIX-FORWARD` + `TICKET-NODE-MEMORY-METRICS` +
`TICKET-SIMD-REGISTRY-CANONICAL`) +
**Phase-2-D** (P1 defer controls: `TICKET-CERT-SEQUENCE-WBH-PROTOCOL` + `TICKET-BENCHMARK-CORPUS-OFFICIAL` +
`TICKET-P1E-CPU-BUDGET-MEASUREMENT`). Total Phase-2 budget: 13 → ≤10 target reachable across ~3–4 chores.

## Cat-3 anti-dup cronaca-discipline verification

- **ZERO source touched in this chore** (this audit catena is purely meta on `docs/FOLLOWUP_TICKETS.md` + this NEW ticket file + `docs/CHANGELOG.md` cite-only entry).
- Zero new SDK API symbol in `include/chronon3d/` (Cat-2 freeze compliant).
- Zero new singleton/registry/resolver/cache (AGENTS.md §regole "non introdurre ... GUI, browser o dipendenze GPU" + §`ANTI_DUPLICATION_RULES.md` deny-everywhere preserved).
- Zero `#include <msdfgen>/<libtess2>/<unicode[/...]>` (Gate 5 Check 11 deny-everywhere preserved).
- ZERO chapter block in this ticket — cronaca lives here in the canonical ticket-home per AGENTS.md
  §`## Discipline di aggiornamento dei canonici` + §Cat-3 anti-dup rule.

## Cross-link

- AGENTS.md §`## Discipline di aggiornamento dei canonici` (canonical escalation table — when to edit which canonical).
- AGENTS.md §`## Regole di lavoro` (HONEST baseline preservation).
- AGENTS.md §"Fare PR piccole e mirate" (Phase-2 decomposition rationale).
- AGENTS.md §`## GATE-MNT-01 — main-sync fail-on-dirty gate` (canonical wrapper for follow-up Phase-2 commits).
- Sibling chaser-chore ticket precedent: `TICKET-FOLLOWUP-DISCIPLINE-FORMALIZATION.md` (the prior 14-row
  catena-edit taxonomy doc; this ticket is the 2026-07-14 second-pass at structural residue).
- Companion ticket per user-directive: NONE (single ticket orchestrator).
- reschedule: per Phase-2-A/B/C/D budget above; expected Phase-2 closure cycle ≤ 2026-07-21.

## Forward-points (canonical siblings)

1. `TICKET-NODE-CACHE-KEY-COLLAPSE-ROT-MACHINE-VERIFY` (P2 cat-1 forward-point), `7e53791`+
2. `TICKET-PERSISTENT-CACHE-ADR-GAP-VACUOUS-AUDIT` (P1 cat-1 forward-point), `7e53791`+
3. `TICKET-CHANGELOG-UPSTREAM-MARKERS-FIX-MACHINE-VERIFY` (P0 cat-1 forward-point), `7e53791`+
4. `TICKET-125-TEST-AGGREGATOR-CATALOG-AUDIT` (P0 cat-1 forward-point), `7e53791`+
5. `TICKET-COMPLETENESS-GATE-V2-FIX-FORWARD-RESOLVE` (P1 cat-1 forward-point), `7e53791`+
6. `TICKET-CERT-SEQUENCE-WBH-PROTOCOL-WBH-PROMOTE` (P1 cat-1 forward-point), `7e53791`+
7. `TICKET-NODE-MEMORY-METRICS-ADR-026-WIRE-IN` (P1 cat-1 forward-point), `7e53791`+
8. `TICKET-SIMD-REGISTRY-CANONICAL-ADR-025-CREATE` (P1 cat-1 forward-point), `7e53791`+
9. `TICKET-BENCHMARK-CORPUS-OFFICIAL-FORWARD-POINT` (P1 cat-1 forward-point), `7e53791`+
10. `TICKET-P1E-CPU-BUDGET-MEASUREMENT-WBH-MACHINE-VERIFY` (P1 cat-1 forward-point), `7e53791`+

**Total forward-points**: 10 (Phase-2 budget).

## Forward-points (cross-link catena)

- [AGENTS.md](../../AGENTS.md) §HONEST-discipline rule
- [docs/FOLLOWUP_TICKETS.md](../FOLLOWUP_TICKETS.md) §"Open Blockers (≤10)" header current state (post-`7e53791+`, 13 rows)
- [docs/CHANGELOG.md](../CHANGELOG.md) `## 2026-07-14` cite-only entry (this chore, prepended at TOP per Cat-5 newer-at-top convention)
