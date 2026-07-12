# docs/pilots/2026-07-12-pilot-instagram.md
# ─────────────────────────────────────────────────────────────────────
# 7-day Instagram Reels pilot — report scaffold + framework documentation
#
# Created 2026-07-12 — Test 16 / First-Principles Product Check #16
# (Pilot setup per user spec verbatim: "Setup 7-day pilot su canale reale
# (scegli YT/IG/TT). Per 7gg produci video reali. Misura:
# video_posted/discarded/manual_corrections/time_saved/cost/bugs.")
#
# Status: HARNESS-COMPLETE (framework code-complete + dashboard wire-up
# forward-pointed); 7-day execution verdict DEFERRED to human runner.
# Per AGENTS.md §honesty "non inventare", the 7-day metric aggregates
# CANNOT be filled in this commit — they require ≥7 days of real pilot
# execution by a human operator on Instagram.
# ─────────────────────────────────────────────────────────────────────

## Pilot summary (header — populated at start of pilot)

| Field | Value |
|---|---|
| Channel | Instagram Reels (vertical 9:16 short-form) |
| Channel URL (planned) | `<runner inserts IG account URL here post-pilot>` |
| Start date | 2026-07-13 (planned) |
| End date | 2026-07-19 (planned, +7 days inclusive) |
| Duration | 7 days |
| Canonical config | [`configs/pilot.instagram.yaml`](../configs/pilot.instagram.yaml) |
| Driver script | [`tools/run_pilot.sh`](../tools/run_pilot.sh) |
| Metrics helper | [`tools/pilot_metrics.py`](../tools/pilot_metrics.py) |
| JSONL log | `~/.chronon3d/pilot/pilot.jsonl` (NOT git-tracked) |
| Runner | `<human insert name>` |
| Verdict | **PENDING** (filled post-execution) |
| Baseline comparator | Cloud AI video service (per `docs/product-tests/TEST-15-one-pager.md` 5-row table) |

## §honesty disclosure (mandatory at the top of any pilot report)

> Per AGENTS.md v0.1 §honesty ("non inventare" + "non segnare verde una suite
> che restituisce failure"), this report is published as a **HARNESS-COMPLETE
> scaffold**: the framework (YAML config + bash driver + Python helper) IS
> code-complete and ready for an honest runner to execute. The 6 metric
> aggregates (`video_posted / discarded / median_manual_corrections /
> median_time_saved / median_cost / bugs_count`) cannot be filled by this
> commit — they require ≥7 days of real execution. Future updates to this
> document will append ONE row per day to the §Daily log table below and
> ONCE the pilot completes, ONE summary row to the §Metrics table.
>
> This is the established pattern from Test #15 (First-Principles Product
> Check #8, `docs/product-tests/TEST-15-*.md`, 2026-07-12) — harness-compete
> on disk + execution-deferred-to-human + PASS verdict deferred. Per the
> Test #15 disclosure language, the 7-day metric verdict IS the real product
> evidence and CANNOT be auto-filled.

## Channel rationale

Instagram Reels chosen over YouTube / TikTok because:
1. **9:16 vertical short-form is Chronon3D-canonical** — the engine already
   supports a `width=1080, height=1920` recipe (visible in
   `content/showcases/cinematic/cinematic_showcase.cpp` + the
   `TICKET-AE-PARITY-CINEMATIC-14` "ae_multiline_9_16_safezone (TikTok/Reels
   safe-area)" test). Zero new render-graph or camera-API work required.
2. **Short-form + render-inexpensive** — IG Reels are 15-90s clips; per-video
   render cost is bounded by the existing ~2 s/frame anchor
   (`docs/PERFORMANCE_BOTTLENECKS.md`); N=2 reels/day × 7 days is comfortably
   within the on-prem CPU budget (`render_cost_usd ≈ 0.0` per the
   Test-15 one-pager anchors).
3. **Content-bias algorithm** — IG Reels' discovery algorithm tolerates lower
   polish than YT's; the `manual_corrections` signal is meaningful (low
   correction count → product is genuinely useful; high correction count →
   product needs more pre-flighting).
4. **NO channel API credentials in repo** — a per AGENTS.md §honesty
   precondition; YT/TT deferred to Test 17/18 follow-up tickets to keep
   this commit Cat-3 (zero new public SDK API) compliant.

## Metric schema (verbatim from user spec)

The 6 metrics measured (rows of `pilot.jsonl` schema):

| Metric | Type | Source |
|---|---|---|
| `video_posted` | bool (true/false per row) | runner records via `tools/run_pilot.sh log <slug> video_posted=true` after IG upload |
| `discarded`    | bool (true/false per row) | runner records via `tools/run_pilot.sh log <slug> discarded=true` when a slug is intentionally not posted |
| `manual_corrections` | int (count per row) | runner counts re-export iterations over the rendered MP4 |
| `time_saved_vs_baseline_min` | float (per row) | runner records estimated Δ minutes vs cloud AI video baseline per `docs/product-tests/TEST-15-one-pager.md` TEMPO row |
| `render_cost_usd` | float (per row, on-prem = 0) | auto-calculated; on-prem CPU host is `~$0` per the canonical Test-15 anchor (`docs/PERFORMANCE_BOTTLENECKS.md`) |
| `bugs` | string (free-text per row) | runner records rendering glitch / command failure / crash / sound sync issues |

## Daily log (planned)

Fill one row per day during the pilot. **Honest framings**:
- All `video_posted` / `manual_corrections` / `time_saved` numbers are
  runner-self-reported. The pilot protocol forbids retroactive fabrication
  per AGENTS.md §honesty.
- `render_cost_usd` is computed from on-prem CPU hours × local electricity
  rate (canonical value ≈ 0 per Test-15 anchor); the formula is documented
  in `configs/pilot.instagram.yaml` §render.

| day_index | date       | compositions rendered | videos posted | discarded | median manual_corrections | median time_saved (min) | bugs (count / detail) | notes |
|---|---|---|---|---|---|---|---|---|
| 0 | 2026-07-13 | _TBD_ | _TBD_ | _TBD_ | _TBD_ | _TBD_ | _TBD_ | _TBD_ |
| 1 | 2026-07-14 | _TBD_ | _TBD_ | _TBD_ | _TBD_ | _TBD_ | _TBD_ | _TBD_ |
| 2 | 2026-07-15 | _TBD_ | _TBD_ | _TBD_ | _TBD_ | _TBD_ | _TBD_ | _TBD_ |
| 3 | 2026-07-16 | _TBD_ | _TBD_ | _TBD_ | _TBD_ | _TBD_ | _TBD_ | _TBD_ |
| 4 | 2026-07-17 | _TBD_ | _TBD_ | _TBD_ | _TBD_ | _TBD_ | _TBD_ | _TBD_ |
| 5 | 2026-07-18 | _TBD_ | _TBD_ | _TBD_ | _TBD_ | _TBD_ | _TBD_ | _TBD_ |
| 6 | 2026-07-19 | _TBD_ | _TBD_ | _TBD_ | _TBD_ | _TBD_ | _TBD_ | _TBD_ |

## Metrics (filled POST-execution)

After pilot completion (≥7 days), copy the aggregate from
`python3 tools/pilot_metrics.py --json` into this table.

| Metric | Threshold (PASS) | Observed (POST-execution) | Status |
|---|---|---|---|
| `videos_posted` | ≥ 7 (≥1/day) | _TBD_ | _TBD_ |
| `videos_post_rate` | ≥ 50 % | _TBD_ | _TBD_ |
| `median_manual_corrections` | ≤ 1 / video | _TBD_ | _TBD_ |
| `median_time_saved_per_video_min` | ≥ 30 min | _TBD_ | _TBD_ |
| `bugs_critical` | 0 | _TBD_ | _TBD_ |
| `discarded_rate` | ≤ 50 % | _TBD_ | _TBD_ |

## Dashboard wiring (forward-point — `TICKET-PILOT-IG-DASHBOARD-WIREUP`)

> **Status**: dashboard endpoint `/api/pilot` (reading `~/.chronon3d/pilot/pilot.jsonl`)
> is NOT wired in this commit. The framework IS code-complete (YAML + bash + Python helper
> + JSONL log) but the catalog dashboard at `http://localhost:5005` (per
> [`docs/TELEMETRY_DASHBOARD.md` §11](../TELEMETRY_DASHBOARD.md)) does NOT yet surface the
> pilot aggregates. The forward-point ticket
> `TICKET-PILOT-IG-DASHBOARD-WIREUP` in `docs/FOLLOWUP_TICKETS.md` §Open Blockers is the
> canonical next-step.
>
> **Ad-hoc dashboard access** for this commit:
> 1. Render telemetry dashboard per `docs/TELEMETRY_DASHBOARD.md` §8:
>    `python3 tools/start_dashboard_shim.py 8000 </dev/null >/tmp/flask_backend.log 2>&1 &`
> 2. Run the pilot helper directly:
>    `python3 tools/pilot_metrics.py --json` (~ equivalent to the future `/api/pilot` JSON response)
> 3. Cross-reference the daily log entries against `/api/runs?since=<day_iso>` in the
>    main dashboard.

## Runbook (TL;DR for runners)

> **First time? Read these before starting.** Per-pilot manuscripts in the established
> Test #15 lineage (`docs/product-tests/TEST-15-pilot-protocol.md`) apply analogously.

```bash
# 1. (one-time) verify chronon3d_cli is on PATH and preflight is green
chronon3d_cli doctor
chronon3d_cli preflight cinematic_showcase

# 2. (per-slot) render + log one video
bash tools/run_pilot.sh render --slug my-shot-$(date +%Y%m%d-%H%M)

# 3. (per-slot, after IG post) record posted / discarded status
bash tools/run_pilot.sh log my-shot-XXXX video_posted=true notes="Posted at HH:MM, <caption>"
# OR
bash tools/run_pilot.sh log my-shot-XXXX discarded=true   notes="Skipped because <reason>"

# 4. (end-of-day) print aggregate
bash tools/run_pilot.sh summary
python3 tools/pilot_metrics.py --json   # machine-readable, for dashboard

# 5. (end-of-pilot) fill Metrics table above via:
python3 tools/pilot_metrics.py --json | jq '{videos_posted: .video_posted_count, ...}'
```

## Forward-points (deferred per AGENTS.md "Fare PR piccole e mirate")

1. **`TICKET-PILOT-IG-DASHBOARD-WIREUP`** — add a `/api/pilot` GET endpoint to
   `tools/telemetry_dashboard/telemetry_server/flask_app.py` (the active Flask
   backend) that reads the JSONL pilot log + emits the same JSON shape as
   `python3 tools/pilot_metrics.py --json`. This commit deliberately does NOT
   modify the Flask backend to keep Cat-3 (zero new public SDK API) + minimal
   atomic-chore scope. The helper is the canonical aggregation kernel; the
   endpoint is a thin Flask re-export.
2. **`TICKET-PILOT-IG-CROSS-REF-RUN-DASHBOARD`** — once the dashboard endpoint
   exists, surface in the existing `App.jsx` runs list a per-day count of
   pilot-aggregated videos (linking `composition_id` ↔ `slug` so a runner can
   drill from a render_run row into the matching pilot.jsonl row).
3. **YouTube + TikTok pilot configs** (Test 17 / Test 18) — clone the IG config
   under `configs/pilot.youtube.yaml` + `configs/pilot.tiktok.yaml`. YT/TT
   each have separate OAuth + posting workflows; the test-18 closes the
   cross-platform surface.
4. **`OAuth credential storage`** (Cat-3 ADR-gated) — if/when IG OAuth credentials
   land in the repo (likely via `~/.chronon3d/credentials.json` OR a secret-store
   integration), `tools/run_pilot.sh` gets a `--auto-post` mode + the
   `video_posted` field flips from manual-record to auto-record. ADR required
   before any secret lands in any tracked file (per AGENTS.md §Cat-3).

## Cross-references

- [`configs/pilot.instagram.yaml`](../configs/pilot.instagram.yaml) — canonical config
- [`tools/run_pilot.sh`](../tools/run_pilot.sh) — driver script
- [`tools/pilot_metrics.py`](../tools/pilot_metrics.py) — Python helper (no PyYAML/pandas dep)
- [`docs/CHANGELOG.md`](../CHANGELOG.md) — pilot setup entry (prepended at top on this commit)
- [`docs/FOLLOWUP_TICKETS.md`](../FOLLOWUP_TICKETS.md) — `TICKET-PILOT-IG-7D` row (this commit's Cat-5 alignment) + `TICKET-PILOT-IG-DASHBOARD-WIREUP` row (forward-point, §Open Blockers)
- [`docs/TELEMETRY_DASHBOARD.md`](../TELEMETRY_DASHBOARD.md) — canonical dashboard guide (Flask backend port 8000 / 5005 per §11)
- [`docs/CLI_REFERENCE.md`](../CLI_REFERENCE.md) — `chronon3d_cli video <id>` subcommand surface
- [`docs/PERFORMANCE_BOTTLENECKS.md`](../PERFORMANCE_BOTTLENECKS.md) — canonical performance anchor (≈2 s/frame on-prem)
- [`docs/product-tests/TEST-15-one-pager.md`](../product-tests/TEST-15-one-pager.md) — the comparable harness-complete pattern (Test #15 honest-disclosure language)
- AGENTS.md §honesty ("non inventare" — execution not fabrication) + §Cat-3 (zero new public SDK API) + §Cat-5 (CHANGELOG + FOLLOWUP same-commit alignment)
