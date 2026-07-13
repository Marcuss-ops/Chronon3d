# Baselines — Index

> Tabella di navigazione cronologica dei `docs/baselines/main-*-baseline.md` per i maintainer futuri. Le baseline sono **prove immutabili** (per [`docs/DOCUMENTATION_GOVERNANCE.md` §`docs/baselines/`](../DOCUMENTATION_GOVERNANCE.md)): una volta create, non sono aggiornate quando `main` avanza. Ogni baseline corrisponde a un singolo commit SHA e documenta lo stato osservabile a quel commit. Questo index è un **support doc** (navigational aid), NON una fonte canonica di stato.

## Legenda

- **State** — `green` (PASS / post-merge / post-step validation) oppure `rot-state` (rot-class OPEN, deferred per AGENTS.md §honesty)
- **Verdict** — gate score (`N/M PASS`), rot count (`N errors / M files`), oppure `validation only` (no gate score)
- **Verification** — `machine-verified` (gate run + result captured on this VPS) / `certified` (canonical green baseline, post-§honesty recert) / `deferred to working build host` (vcpkg glm/magic_enum + tmpfs env-block per AGENTS.md §honesty) / `validation only` (no gate audit, only sanity validation)
- **Note** — contesto per-baseline (canonical, rot-cascade diagnostic, post-step, ecc.)

## Baselines (cronologico, oldest first)

| # | Baseline | Date | State | Verdict | Verification | Note |
|---|---|---|---|---|---|---|
| 1 | [main-446a60e2-baseline.md](main-446a60e2-baseline.md) | 2026-06-23 | green | post-merge validation | validation only | prima baseline del repo |
| 2 | [main-88d2deec-baseline.md](main-88d2deec-baseline.md) | 2026-06-29 | green | 4/11 PASS | machine-verified | prima baseline con gate score |
| 3 | [main-21103265-baseline.md](main-21103265-baseline.md) | 2026-06-30 | green | 9/11 PASS | machine-verified | |
| 4 | [main-30f6c876-baseline.md](main-30f6c876-baseline.md) | 2026-06-30 | green | 8/11 PASS | machine-verified | |
| 5 | [main-aaf70032-baseline.md](main-aaf70032-baseline.md) | 2026-07-01 | green | 10/11 PASS | machine-verified | |
| 6 | [main-16319557f-baseline.md](main-16319557f-baseline.md) | 2026-07-04 | green | 9/11 PASS | machine-verified | |
| 7 | [main-80a8329c-baseline.md](main-80a8329c-baseline.md) | 2026-07-04 | green | 9/11 PASS | machine-verified | |
| 8 | [main-c5793405-baseline.md](main-c5793405-baseline.md) | 2026-07-04 | green | 10/11 PASS | machine-verified | |
| 9 | [main-9ecb4879-baseline.md](main-9ecb4879-baseline.md) | 2026-07-04 | green | 7/11 PASS | machine-verified | |
| 10 | [main-eb8e3a24-baseline.md](main-eb8e3a24-baseline.md) | 2026-07-04 | green | 7/11 PASS | machine-verified | |
| 11 | [main-1078ab46-baseline.md](main-1078ab46-baseline.md) | 2026-07-04 | green | 10/11 PASS | machine-verified | |
| 12 | **[main-7eb5c2ba-baseline.md](main-7eb5c2ba-baseline.md)** | 2026-07-06 | **green (CERTIFIED)** | **11/11 PASS** | **certified** | **CANONICAL green baseline** — feature freeze V0.1 revocato per `7eb5c2ba` |
| 13 | [main-908c7034-baseline.md](main-908c7034-baseline.md) | 2026-07-10 | green | 10/13 PASS | machine-verified | gate suite estesa (N=13) |
| 14 | [main-acf7d1de-baseline.md](main-acf7d1de-baseline.md) | 2026-07-10 | green | post-Step 2 Assets | validation only | |
| 15 | [main-df1e09d9-rot-cascade-baseline.md](main-df1e09d9-rot-cascade-baseline.md) | 2026-07-12 | **rot-state** | 245 errors / 11+ files | **deferred to working build host** | rot-cascade diagnostic — unica rot-state baseline (canonical per TICKET-BUILD-ROT-CASCADE-CAMERA) |

| 16 | [main-131119fe-baseline.md](main-131119fe-baseline.md) | 2026-07-13 | green (scaffold WBH-pending) | perf-baseline cpu-mid 8-thread | **deferred to working build host** | FASE 1.5 perf-baseline - canonical "stato iniziale" del programma CPU-only (12 scene B00-B11 schema-conformant a `chronon3d.bench.v3`); valori numerici zero placeholder; real-data macchina-verifica DEFERRED-WBH per `TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV` env-block (forward-point `TICKET-BENCH-BASELINE-SHA-MACHINE-VERIFY`) |

## Statistiche sintetiche

| Metric | Count |
|---|---:|
| Total baselines | 15 |
| green (PASS / validation) | 14 |
| rot-state (rot-class OPEN) | 1 |
| Machine-verified (gate audit) | 12 |
| &nbsp;&nbsp;&nbsp;&nbsp;↳ Certified canonical green (sub-class of machine-verified) | 1 (`main-7eb5c2ba`) |
| Validation only (no gate score) | 2 |
| Deferred to working build host | 1 |

**Reconciliation**: 12 machine-verified (includes 1 certified canonical green as sub-class) + 2 validation only + 1 deferred (rot-state) = 15 total. The "certified" bucket is a sub-class of "machine-verified" (the canonical green baseline IS machine-verified — it has 5/5 smoke tests machine-verified per its self-doc), not a separate category.

## Cross-references

- **Canonical green baseline**: [main-7eb5c2ba-baseline.md](main-7eb5c2ba-baseline.md) (11/11 PASS, 2026-07-06) — citato da `docs/CURRENT_STATUS.md` + `docs/RELEASE_GATE.md` come riferimento "baseline verde"
- **Current rot-state baseline**: [main-df1e09d9-rot-cascade-baseline.md](main-df1e09d9-rot-cascade-baseline.md) (245 errors, deferred per AGENTS.md §honesty) — citato da `docs/FOLLOWUP_TICKETS.md` `TICKET-BUILD-ROT-CASCADE-CAMERA` row
- **Baseline protocol contracts**:
  - [`AGENTS.md` §Baseline macchina-verificata](../../AGENTS.md) (the protocol definition: `docs/baselines/main-<sha>-baseline.md` format)
  - [`docs/DOCUMENTATION_GOVERNANCE.md` §`docs/baselines/`](../DOCUMENTATION_GOVERNANCE.md) (immutability + content contract: SHA completo, data, ambiente, versione strumenti, comandi, exit code, gate result, artifact/log, verdetto finale)
- **TICKET correlati** (dalla `## Open Blockers` table in `docs/FOLLOWUP_TICKETS.md`):
  - TICKET-BUILD-ROT-CASCADE-CAMERA (P0, OPEN; rot-cascade parent — citato dal rot-state baseline)
  - TICKET-BUILD-ROT-CAMERA-CASCADE-SUB-WORKSTREAMS (P0, OPEN; umbrella ticket for the 3 sub-fixes — citato dal rot-state baseline)
  - TICKET-CONTENT-TEXT-CAMERA-V1-ROT (P1, OPEN; Phase 1 rot, verified RESOLVED by parallel agent)
  - TICKET-CONTENT-TEXT-CAMERA-V1-ROT-VARIANT-MOVE (P1, OPEN DORMANT; variant rot sub-ticket — citato dal rot-state baseline)

## Maintenance

This index is a support doc (per `docs/DOCUMENTATION_GOVERNANCE.md`); it does NOT contain canonical state (per `docs/CURRENT_STATUS.md` contract). When a new baseline is added:

1. **Add a row** to the table above (chronological order; insert before newer rows; renumber `#` column)
2. **Update the Statistiche sintetiche** table if the new baseline's state changes a count (green vs rot-state vs machine-verified)
3. **Update Cross-references** if the new baseline is a new canonical green baseline (replaces `main-7eb5c2ba-baseline.md` as the canonical reference in `docs/CURRENT_STATUS.md` + `docs/RELEASE_GATE.md`)
4. **Do NOT modify existing rows** (baselines are immutable; this index is effectively append-only per Cat-3 anti-duplication)
5. **Do NOT duplicate baseline content** here (per Cat-3 anti-duplication + `docs/DOCUMENTATION_GOVERNANCE.md` "ogni informazione deve avere una sola fonte canonica"); just link to the baseline file + state the verdict

Per `docs/DOCUMENTATION_GOVERNANCE.md` "Politica degli snapshot": only `CURRENT_STATUS.md` + files in `docs/baselines/` + dated audit reports can contain a current SHA. This index is a dated audit report of the baselines directory (NOT a current-SHA snapshot); the SHA is in the baseline filename, not in the index body.
