# Pilot 7 giorni — TikTok (Test #18 / First-Principles Product Check #18)

> **Stato corrente**: HARNESS-COMPLETE (config + questo report scaffold), 7-day execution DEFERRED al human runner per **AGENTS.md §honesty "non inventare"** (questa VPS non ha OAuth TikTok né il workflow di posting manuale non può essere simulato).
>
> **Compagno**: [`configs/pilot.tiktok.yaml`](../configs/pilot.tiktok.yaml) (canonical 7-day config).
> **Ticket**: `TICKET-PILOT-TT-7D-SETUP` (chiusura setup) + `TICKET-PILOT-TT-DASHBOARD-WIREUP` (open, forward-point /api/pilot/tiktok).
> **Sibling precedent**: `TICKET-PILOT-IG-7D-SETUP` (Test #16 IG, prior §Recently Closed row in [`docs/FOLLOWUP_TICKETS.md`](../docs/FOLLOWUP_TICKETS.md)) — questo setup è il forward-point (a) "TikTok pilot config" di Test #16.

---

## TL;DR per il runner (5 righe)

1. Esegui `tools/run_pilot.sh --channel tiktok render --output ~/.chronon3d/pilot/tiktok/renders/<day>-<timestamp>.mp4 --composition ChrononGlowFinalAE` ogni giorno alle 09:00 UTC per 7 giorni.
2. Carica manualmente il MP4 su TikTok Creator Studio (https://www.tiktok.com/upload) entro l'ora successiva — **NO OAuth** (AGENTS.md §honesty non-inventare discipline).
3. Dopo che TikTok conferma l'upload, registra `tools/run_pilot.sh --channel tiktok log --row '{ts_iso,...,notes}'` (vedi sotto) per scrivere la riga JSONL in `~/.chronon3d/pilot/tiktok/pilot.jsonl`.
4. A fine giornata, `python3 tools/pilot_metrics.py --log ~/.chronon3d/pilot/tiktok/pilot.jsonl --table` per controllare il daily aggregate.
5. Dopo 7 giorni, aggrega il `pilot.jsonl` con `python3 tools/pilot_metrics.py --json > ~/.chronon3d/pilot/tiktok/final-summary.json` e popola questo report scaffold con i row del `## Daily log` + `## Metrics` qui sotto.

---

## §Honesty: cosa NON è verificato su questa VPS

Per **AGENTS.md §honesty §13 honest-limitation pattern** (questa VPS non ha vcpkg `glm` + `magic_enum` + tmpfs sufficiency per il full project build + non ha OAuth TikTok né il browser umano per il posting manual):

- `chronon3d_cli` non è linkable su questa VPS (pre-existing rot TICKET-BUILD-ROT-CASCADE-CAMERA + TICKET-TEXT-LEGACY-POSITION-ROT da chiudere prima): le 7 righe della `## Daily log` qui sotto sono **VUOTE** — saranno popolate dal human runner su working build host.
- Le 6 metriche (`video_posted`, `discarded`, `manual_corrections`, `time_saved`, `cost`, `bugs`) nella tabella `## Metrics` sono **VUOTE** — i valori arriveranno dal aggregation su `pilot.jsonl` al termine del 7-day execution.
- Il PASS / FAIL verdict del pilot è **OPEN** nel `TICKET-PILOT-TT-7D-SETUP` §Recently Closed — diventa `DONE` solo dopo che il runner compila le 7 righe E il `## Metrics` aggregate soddisfa `success_criteria.pilot_pass` dal YAML.
- Il dashboard `/api/pilot/tiktok` endpoint (`TICKET-PILOT-TT-DASHBOARD-WIREUP`) NON è implementato in questo commit — il runner usa il `python3 tools/pilot_metrics.py --log/--json/--table/--row` ad hoc (UI workflow differs from IG dashboard wireup, deferred).

---

## Daily log (7 rows, schema canonico da `configs/pilot.tiktok.yaml §log.jsonl_append_mode`)

| Day | Date (target) | ts_iso | composition_id | render_ms | video_posted | discarded | manual_corrections | time_saved_min | cost_eur | bugs | notes |
|-----|---------------|--------|----------------|-----------|--------------|-----------|--------------------|----------------|----------|------|-------|
| 1 | 2026-07-13 Mon | _TBD_ | _ChrononGlowFinalAE_ | _TBD_ | _TBD_ | _TBD_ | _TBD_ | _TBD_ | _TBD_ | _TBD_ | _runner fills per day_ |
| 2 | 2026-07-14 Tue | _TBD_ | _ChrononGlowFinalAE_ | _TBD_ | _TBD_ | _TBD_ | _TBD_ | _TBD_ | _TBD_ | _TBD_ | _runner fills per day_ |
| 3 | 2026-07-15 Wed | _TBD_ | _ChrononGlowFinalAE_ | _TBD_ | _TBD_ | _TBD_ | _TBD_ | _TBD_ | _TBD_ | _TBD_ | _runner fills per day_ |
| 4 | 2026-07-16 Thu | _TBD_ | _ChrononGlowFinalAE_ | _TBD_ | _TBD_ | _TBD_ | _TBD_ | _TBD_ | _TBD_ | _TBD_ | _runner fills per day_ |
| 5 | 2026-07-17 Fri | _TBD_ | _ChrononGlowFinalAE_ | _TBD_ | _TBD_ | _TBD_ | _TBD_ | _TBD_ | _TBD_ | _TBD_ | _runner fills per day_ |
| 6 | 2026-07-18 Sat | _TBD_ | _ChrononGlowFinalAE_ | _TBD_ | _TBD_ | _TBD_ | _TBD_ | _TBD_ | _TBD_ | _TBD_ | _runner fills per day_ |
| 7 | 2026-07-19 Sun | _TBD_ | _ChrononGlowFinalAE_ | _TBD_ | _TBD_ | _TBD_ | _TBD_ | _TBD_ | _TBD_ | _TBD_ | _runner fills per day_ |

**Append protocol**: `tools/run_pilot.sh --channel tiktok log --row '{ts_iso,day,composition_id,render_ms,video_posted,discarded,manual_corrections,time_saved_min,cost_eur,bugs,notes}'` (append-only, NON overwrite).

---

## Metrics (6-row aggregator output, post-7-day `tools/pilot_metrics.py --json`)

| Metric | Target | Floor | Final Value | Margin | Status |
|--------|--------|-------|-------------|--------|--------|
| `video_posted` | 7 | ≥ 5 | _TBD_ | _TBD_ | _TBD per runner_ |
| `discarded` | ≤ 1 | ≤ 2 | _TBD_ | _TBD_ | _TBD per runner_ |
| `manual_corrections` | ≤ 3 | ≤ 5 | _TBD_ | _TBD_ | _TBD per runner_ |
| `time_saved` (min/video) | ≥ 15 | ≥ 10 | _TBD_ | _TBD_ | _TBD per runner_ |
| `cost` (EUR/video) | ≤ 0.10 | ≤ 0.20 | _TBD_ | _TBD_ | _TBD per runner_ |
| `bugs` (count) | ≤ 2 | ≤ 5 | _TBD_ | _TBD_ | _TBD per runner_ |

**Final verdict**: _TBD per runner — verdict based on `success_criteria.pilot_pass.all_of` from `configs/pilot.tiktok.yaml`_.

---

## §Honesty: cosa il runner DEVE sapere prima di partire

- **TikTok ha cambiato la sua video upload API 2 volte dal 2026-01** (unauthenticated upload DELETED, then access-token-only, then baked-creator-studio-only workflow). Il workflow "manual upload via Creator Studio" è l'unico path NON-API — confermalo prima di partire (la pagina TikTok Creator potrebbe richiedere login dell'account con cooked creator tools).
- **I file MP4 generati da `chronon3d_cli` sono 1080×1920 @ 30 fps** = ~5 MB / 30s (compress-friendly con TikTok's H.264 re-encode). Niente surprises attese nel posting flow.
- **NO automated comment / like metrics scraping** (AGENTS.md §honesty): `video_posted` si riferisce SOLO al successful upload + keep-on-platform, NON al view-count / like-count / share-count. Se vuoi metriche di engagement, scraping richiede OAuth scope separato (Cat-3 gated — `TICKET-PILOT-TT-OAUTH` forward-point per uno scrape deterministic).
- **Il `tools/pilot_metrics.py --help` mostra 3 modes** (`--json`, `--table`, `--row`) confermato su questa VPS via dry-run (the IG pilot's Test #16 verification protocol applies identically to TT — Cat-3 zero-new-symbol).
- **TikTok account risk**: il pilot potrebbe triggereare TikTok's "is this a bot account?" risk-detection sul primo upload manual — pianifica `time_saved_min` con un budget "warm-up time" il giorno 1, oppure fai un upload manuale di warm-up fuori dal pilot prima di partire con day 1 del `## Daily log`.

---

## §Forward-points (NOT in this commit, deferred per AGENTS.md "Fare PR piccole e mirate")

1. **`TICKET-PILOT-TT-DASHBOARD-WIREUP`** — `/api/pilot/tiktok` endpoint on `tools/telemetry_dashboard/telemetry_server/flask_app.py` (mirrors `TICKET-PILOT-IG-DASHBOARD-WIREUP` precedent). Cat-3 minimal-surface (1 Flask route). UI workflow alternative al `tools/pilot_metrics.py --json` ad-hoc.
2. **`TICKET-PILOT-TT-OAUTH`** — OAuth credential storage + `--auto-post` mode in `tools/run_pilot.sh` (mirrors TICKET-PILOT-IG-7D-SETUP forward-point (b)). ADR-gated prima dell'implementazione per Cat-3 freeze.
3. **`TICKET-PILOT-TT-FAILURE-RCA`** — RCA post-mortem ticket che si apre se il 7-day execution finisce con `bugs >= 5` (catastrophic envelope) o `video_posted < 3` (under-floor). Forward-point si attiva con il daily aggregate check.
4. **`configs/pilot.youtube.yaml`** — YouTube Shorts pilot config (forward-point (a) "YouTube pilot config" del IG TICKET-PILOT-IG-7D-SETUP §Recently Closed row). Could pair con questo TT come `(Test #17 / YouTube + Test #18 / TikTok)` feedback cycle.

---

## Cross-references

- [`configs/pilot.tiktok.yaml`](../configs/pilot.tiktok.yaml) — canonical 7-day config (this commit)
- [`docs/FOLLOWUP_TICKETS.md`](../docs/FOLLOWUP_TICKETS.md) §Recently Closed `TICKET-PILOT-TT-7D-SETUP` row (this commit's closure lineage)
- [`docs/FOLLOWUP_TICKETS.md`](../docs/FOLLOWUP_TICKETS.md) §Open Blockers `TICKET-PILOT-TT-DASHBOARD-WIREUP` row (deferred forward-point)
- [`docs/FOLLOWUP_TICKETS.md`](../docs/FOLLOWUP_TICKETS.md) §Recently Closed `TICKET-PILOT-IG-7D-SETUP` row (Test #16 sibling precedent with forward-points (a) YT + (b) TikTok explicitly listed)
- AGENTS.md §honesty "non inventare" + §13 honest-limitation pattern (VPS-blocked execution deferral to working build host)
- AGENTS.md §Cat-3 (zero new public SDK API surface — pure configs/ + docs/ additions)
- AGENTS.md §regole "Fare PR piccole e mirate" (single atomic chore commit + forward-point deferral)
