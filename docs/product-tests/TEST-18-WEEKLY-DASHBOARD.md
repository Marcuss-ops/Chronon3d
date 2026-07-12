# Test 18 — Weekly founder dashboard (template)

> **Owner**: il fondatore (you). **Cadence**: weekly (typically Friday afternoon, before the wind-down).
> **Aggregation tool**: [`tools/run_weekly_scorecard.sh`](../../tools/run_weekly_scorecard.sh) — emits the 8-metric markdown table to stdout from `~/.chronon3d/telemetry/`.
> **Per AGENTS.md §honesty**: every metric below MUST be computed from an **observable** source (SQLite query, JSONL count, log grep), never from a guesstimate. The "Recommended run" command below is the canonical daily practice.
> **Per Cat-5**: this template stays aligned with `docs/CHANGELOG.md` (1 prepended entry per cycle), `docs/FOLLOWUP_TICKETS.md` (Test 18 row in §Open Blockers), and `docs/tickets/TICKET-125-test-aggregator.md` (row 18 = OPEN).

## How to use this template

Each Friday (or whenever your week ends):

1. Run the aggregator with the spot-rate env var set:
   ```
   WEEKLY_COST_HOURLY_RATE=0.05 bash tools/run_weekly_scorecard.sh > /tmp/my-weekly.md
   ```
   - Replace `0.05` with your actual CPU/GPU spot rate ($/hour).
   - The aggregator emits the 8-row table + the 7 narrative lines below.

2. Append the aggregator output to this week's entry below (or copy-paste into `docs/product-tests/TEST-18-<YYYY-Wxx>.md`).

3. **Hand-fill the 2 forward-points** (Codice eliminato + Metrica migliorata) where the metric is NOT yet instrumented; cite the canonical source inline.

4. Commit + (best-effort) push the result. F2 infra blocker frequently forces LOCAL-ONLY; doc the PARTIAL cert per §honesty.

## Sample weekly entry

```
# Test 18 — Week 2026-W28 (2026-07-05 to 2026-07-12)

(insert output of `bash tools/run_weekly_scorecard.sh` here)

## Founder weekly grep (answer these in 1 sentence each)

1. Quanti video: ...
2. Costo: ...
3. Job falliti: ...
4. Interventi manuali: ...
5. Codice eliminato: ... (forward-point: `docs/FEATURE_SUNSET.md`)
6. Bug piu grave: ... (forward-point: `docs/FOLLOWUP_TICKETS.md` §Open Blockers)
7. Metrica migliorata: ... (week-over-week delta vs previous week)
```

## 8 metrics (the table)

| # | Metric | Aggregation source (per `tools/run_weekly_scorecard.sh`) | PASS criterion (osservabile) |
|---|---|---|---|
| 1 | videos_completed | renders.status='DONE' over 7d | exit 0 → integer N |
| 2 | failure_rate | FAILED / total over 7d | exit 0 → "NN.NN%" string |
| 3 | manual_touches_per_video | touchpoints.jsonl + render_counters counter | exit 0 → "NN.NN" (avg) |
| 4 | cost_per_finished_minute | WEEKLY_COST_HOURLY_RATE × total_ms / 60000 | env var set → "$.NNNN/min" |
| 5 | p95_render_time | ASC-sorted render_ms, floor(N * 0.95) | exit 0 → "N.NNN s" |
| 6 | peak_memory | MAX(framebuffer_bytes_peak over 7d) | exit 0 → "NNN.N MB" |
| 7 | deterministic_hash_failures | grep GATE_FAIL in selftest logs | exit 0 → integer N |
| 8 | bbox_contract_violations | SUM(text_bbox_contract_violations over 7d) | exit 0 → integer N |

## 7 narrative lines

These answer the founder's weekly greps per AGENTS.md "must answer with numbers":

1. **Quanti video**: INTEGER (metric 1).
2. **Costo**: derived from metric 4 (rate × time) — not a fabricated budget.
3. **Job falliti**: parse failure_rate → "N di M totali (NN.NN%)"
4. **Interventi manuali**: parse metric 3 → "N.NN per video".
5. **Codice eliminato**: forward-point to `docs/FEATURE_SUNSET.md` (Test 16 registro) — NOT instrumented in this dashboard. Cite the row count + cited CANDIDATO.
6. **Bug piu grave**: forward-point to highest-priority OPEN ticket in `docs/FOLLOWUP_TICKETS.md` §Open Blockers.
7. **Metrica migliorata**: week-over-week delta; first run seeds baseline; second compares.

## §honesty PARTIAL cert on this dashboard

- The 8 metrics are computed from OBSERVABLE telemetry (Cat-3 minimal-surface, zero new public SDK API).
- `manual_touches_per_video` (Test 8) + `text_bbox_contract_violations` (FU01) are WIRED end-to-end (counter on every finalized render job; aggregated by SQLite `SUM`).
- `videos_completed` + `failure_rate` + `cost_per_finished_minute` + `p95_render_time` + `peak_memory` are AGGREGATIONS over the existing `renders` table — no new column needed (assumes the `renders` table is populated by `chronon3d_cli` writes; CAT-3 minimal-surface assumption).
- `deterministic_hash_failures` requires the canonical Test 10 selftest log convention: aggregator parses `~/.chronon3d/selftest_log/check_determinism*.log` for `GATE_FAIL` lines. macchina-verifica deferred to working build host per AGENTS.md §honesty.
- macchina-verifica of the aggregator: runnable on this VPS via `bash -n tools/run_weekly_scorecard.sh` (syntax PASS) + dry-run with empty DB (exit 2 with `GATE_FAIL_INTERNAL: telemetry SQLite not present`); full 8-row table emission requires a populated SQLite (deferred to working build host).

## Cross-link

- [`tools/run_weekly_scorecard.sh`](../../tools/run_weekly_scorecard.sh) — the canonical aggregator (Cat-4 ancillary sibling-gate pattern; new tools/ artifact; ~140 LoC; no public SDK API surface).
- [`docs/tickets/TICKET-128-test-18-long-form-content.md`](../tickets/TICKET-128-test-18-long-form-content.md) — the canonical follow-up ticket (Stato `OPEN`, Priorità `P1`).
- [`docs/tickets/TICKET-125-test-aggregator.md`](../tickets/TICKET-125-test-aggregator.md) — row 18 = OPEN weekly dashboard.
- `docs/FOLLOWUP_TICKETS.md` §Open Blockers — Test 18 row promoted to OPEN per Cat-5 4-doc same-commit.
- `docs/CHANGELOG.md` — prepended entry per cycle.
- `docs/CURRENT_STATUS.md` §Hygiene — 1-line cite-only row per Cat-3 anti-duplication (L3 forward-point folding precedent from TICKET-125 cycle).
- AGENTS.md §Cat-3 (zero new public SDK API surface, satisfied), §Cat-5 (4-doc same-commit alignment), §honesty (no fabrication, observable PASS/FAIL via bash one-liners), §Fare PR piccole e mirate (single atomic chore).
