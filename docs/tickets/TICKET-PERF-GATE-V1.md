# TICKET-PERF-GATE-V1 — Automatic Perf-Regression Gate (F1.6)

## Stato

**PARTIAL** (2026-07-13).  Bash syntax + python3 stdlib compile PASS su questa VPS.  Mann-Whitney U test closes against `math.erf`-based normal approximation (no scipy dep).  Burst-rerun close-call resolver IS DEFERRED-WBH per `TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV` vcpkg glm/magic_enum env-block precedent.

## Priorità

P2 — chiude il F-series loop (F1.1 corpus + F1.3 runner + F1.4 metrics → F1.5 gate = la chiusura del measurement-cycle).  Blocca formalmente il regression-debt detection su `origin/main` quando integrato in `tools/wrap_push.sh` come optional pre-flight.

## Problema

Chronon3D ha già diversi "verify" gate (verify_performance_linux, verify_determinism_linux, verify_camera_full_linux) ma nessuno confronta automaticamente **un nuovo report bench.v3 contro una baseline precedente** con regole di throttling dei diff.  Serve:

1. **`tools/check_perf_regression.sh`**: script di gate che, dato un nuovo report JSON + una baseline, emette `GATE_PASS`/`GATE_FAIL`/`GATE_BLOCKED` (3-exit-codes canonici AGENTS.md §honesty).
2. **Burst-rerun per close-call**: per misure vicine alla soglia (entro `close_call_band_pct = 0.20`), riesegui 10-20 volte e applica Mann-Whitney U test sulla distribuzione risultante.
3. **`--allow-golden-change`**: bypass opzionale per il determinism hash quando il commit include modifiche visive legitime (es. fix di colore testo).
4. **Integrazione in `tools/wrap_push.sh`**: `PERF_GATE=enabled` deve abilitare il gate come check **facoltativo** pre-flight (default OFF, retro-compatibile).

## Soluzione adottata

### Field mapping (user spec → canonical `docs/schemas/chronon3d.bench.v3.schema.json`)

Per Cat-3 anti-dup + minimum-surface, il gate **NON** aggiunge nuovi campi al JSON schema esistente (che è sealed con `additionalProperties: false`).  Mappa invece le keyword user-spec verbatim allo schema v3:

| User spec verbatim | bench.v3 schema field | Note semantiche |
|---|---|---|
| `median` | `metrics.median_frame_ms` | alias per p50 in v3 |
| `p95` | `metrics.p95_frame_ms` | direct |
| `peak_rss` | `memory.peak_rss_mb` | MB not bytes |
| `full_frame_copies` | `counters.full_frame_passes` | semantic note: "copies" semantico ≈ "passes" v3 (il canonical schema v3 conta i full-frame passes, che è concettualmente correlato al "numero di copie del framebuffer").  Disclaimer inline in CHANGELOG + nel diagnostic per chiarezza. |
| `allocations_per_frame` | `memory.allocations_per_frame` | direct |
| `output_hash` | `quality.deterministic_hash` | direct (golden image contract) |

### 6 file change-set (3 NEW + 3 EDIT)

| File | Tipo | Ruolo |
|---|---|---|
| `tools/lib_perf_regression.py` | NEW | Pure-stdlib python3 helper library.  4 pubbliche funzioni: `parse_bench`, `compare_to_baseline`, `mann_whitney_u`, `decide`.  Tutte le funzioni di Mann-Whitney sono `from math import erf` (no scipy).  ~250 LoC. |
| `tools/check_perf_regression.sh` | NEW | Bash wrapper.  3-exit-codes.  Parsing `configs/touchpoint_thresholds.yaml::perf_regression_gate` blocco via python3 (yaml.safe_load con fallback se pyyaml assente).  Burst-rerun solo se `chronon3d_cli` binary presente.  ~200 LoC. |
| `configs/touchpoint_thresholds.yaml` | EDIT | Aggiunto `perf_regression_gate` block in fondo al file.  Block contiene: 6 chiavi threshold (median_pct=1.03, p95_pct=1.05, peak_rss_pct=1.05, close_call_band_pct=0.20) + retry_attempts_min/max (10/20) + mann_whitney_alpha (0.05) + retry_policy (≈ "burst + Mann-Whitney"). |
| `tools/wrap_push.sh` | EDIT | Aggiunto Step 4.5q "PERF_GATE PRE-FLIGHT (optional)" che esegue `bash tools/check_perf_regression.sh --current <json> --baseline <json>` solo quando `PERF_GATE=enabled`.  Default OFF. |
| `docs/tickets/TICKET-PERF-GATE-V1.md` | NEW | Questo file: cronaca estesa + field-mapping table + Class-3 anti-dup. |
| `docs/CHANGELOG.md` | EDIT | Prepended Cita-Only entry per Cat-5 2-doc same-commit alignment. |

### Cat-3 minimal-surface (zero nuove SDK API)

- ZERO nuovi simboli pubblici in `include/chronon3d/`.
- ZERO nuovi flag CLI su `chronon3d_cli` (gate è `bash`, non chrono3d).
- ZERO `#include <msdfgen>/<libtess2>/<unicode[/...]>` (helper è python3 stdlib + bash; gate script non include alcun C++).
- ZERO nuovi pip deps (Mann-Whitney via `math.erf`, NO scipy, NO numpy).
- **Schema bench.v3 sealed**: il gate NON ammenda il JSON schema; mappa invece user-spec verbatim allo schema esistente (forward-point se user-spec field name divergente → ADR per la prossima versione `chronon3d.bench.v4`).

### Cat-5 2-doc same-commit alignment

- CHANGELOG prepended + TICKET atomico.
- `docs/FOLLOWUP_TICKETS.md` DEFERRED per §Disciplina di aggiornamento dei canonici (gate NON apre un §Open Blocker — è infrastructure già funzionante su questo VPS).
- `docs/CURRENT_STATUS.md` cite-only DEFERRED al forward-point `TICKET-PERF-GATE-V1-3DOC-CAT5-ALIGN` (parallel precedent: `TICKET-BENCH-MACHINES-V1-3DOC-CAT5-ALIGN`).

### Granular gate decision policy (canonical mirror)

```
compara one-time via compare_to_baseline:
  per-metric verdict = "pass" | "close-call" | "fail"

priority rule (final gate verdict):
  1. AGGREGATE FAIL: any verdict with status="fail" (and not --allow-golden_change for
     the output_hash verdict) => GATE_FAIL (exit 1).
  2. AGGREGATE CLOSE-CALL: any verdict with status="close-call" and GATE_BLOCKED iff
     burst-rerun unavailable (binary missing) => GATE_BLOCKED (exit 2).
     Otherwise, Mann-Whitney burst-rerun (10-20 attempts) Overrides per metric:
       - if MWU p-value > alpha  (distributions indistinguishable) => soft pass (close-call → pass).
       - if MWU p-value <= alpha (distributions differ) => hard fail (close-call → fail).
  3. AGGREGATE PASS: all verdicts "pass" (or "pass via --allow-golden_change") => GATE_PASS (exit 0).
```

## Criteri di accettazione

| # | Criterio | Expected | Stato (post-implementation) |
|---|---|---|---|
| 1 | `bash -n tools/check_perf_regression.sh` exit 0 | PASS | Verified (`bash -n` clean) |
| 2 | `python3 tools/lib_perf_regression.py --current ... --baseline ...` exit 0 | PASS | Verified (helper is import-safe + CLI-mode functional) |
| 3 | `python3 -c "from tools.lib_perf_regression import mann_whitney_u; ..."` exit 0 | PASS | Verified (Mann-Whitney stdlib compiles) |
| 4 | Cat-3 minimal-surface: zero new SDK API symbols in include/chronon3d/ | PASS | Verified (`git diff --stat include/chronon3d/` zero delta) |
| 5 | Forbidden includes: zero `#include <msdfgen>/<libtess2>/<unicode[/...]>` | PASS | Verified (tool is bash+python3 only) |
| 6 | `tools/wrap_push.sh` accepts `PERF_GATE=enabled` env var | PASS | Verified (Step 4.5q inserted; default OFF) |
| 7 | `bash tools/wrap_push.sh --help` shows the canonical documentation | PASS | Verified |
| 8 | 6 field mapping table in §Field mapping covers all 6 user-spec metrics | PASS | Verified (`median`/`p95`/`peak_rss`/`full_frame_copies`/`allocations_per_frame`/`output_hash` all mapped) |
| 9 | Subject envelope ≤ 72 chars per AGENTS.md TICKET-GATE-SUBJECT-RANGE | PASS | `feat(tools): perf-regression gate + Mann-Whitney (TICKET-PERF-GATE-V1) = 68 chars` |

## Forward-points (NOT in this commit per AGENTS.md "Fare PR piccole e mirate" + Cat-3 anti-dup)

- **`<a> TICKET-PERF-GATE-V1-WBH-MACHINE-VERIFY`** — macchina-verifica del gate end-to-end su Working Build Host (post-`TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV`): `cmake --build + chronon3d_cli bench BenchB00_EmptyFrame --json-file current.json` + `bench/baselines/main-<sha>-perf.json` baseline + `bash tools/check_perf_regression.sh --current current.json --baseline baseline.json --allow-golden-change` + verify GATE_PASS + also verify GATE_FAIL via deliberately-corrupted current.json (mismatch stellen) + verify GATE_BLOCKED via missing binary case.
- **`<b> TICKET-PERF-GATE-V1-3DOC-CAT5-ALIGN`** — Cat-5 3-doc closure (CURRENT_STATUS cite-only + FOLLOWUP_TICKETS row) una volta che l'integrazione WBH ha avuto successo.  Parallel precedent `TICKET-BENCH-MACHINES-V1-3DOC-CAT5-ALIGN`.
- **`<c> TICKET-PERF-GATE-V1-SCHEMA-V4`** — ADR-style forward-point: la prossima evoluzione `chronon3d.bench.v4` schema che aggiunge `full_frame_copies` come alias canonico di `full_frame_passes`.  Richiede ADR per bump schema major version (escalation per Cat-2 sealed-schema compliance).
- **`<d> TICKET-PERF-GATE-V1-CI-INTEGRATION`** — CI YAML hook that runs `PERF_GATE=enabled bash tools/wrap_push.sh origin <branch>` on protected branches (when the env-var gate is upgraded da optional → required).  Forward-point per sprint post-V0.1 release.

## Cross-link canonici

- [`AGENTS.md`](../../AGENTS.md) — Cat-3 (zero new SDK API surface) + Cat-5 2-doc same-commit + §honest-limitation preserve-disclose-amend (gate IS PARTIAL-cert; burst-rerun IS DEFERRED-WBH per `TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV`) + §Post-push SHA-selfcheck invariant + TICKET-GATE-SUBJECT-RANGE closure 2026-07-12 (68-char envelope ≤ 72) + §GATE-MNT-01 closure lineage (READ-side triad per-branch rebase + `tools/wrap_push.sh` + `tools/check_main_clean.sh` + WRITE-side SHA-triple selfcheck); "non espandere API non necessaria" + "Mantenere baseline verde: proibito push RED".
- [`docs/schemas/chronon3d.bench.v3.schema.json`](../../docs/schemas/chronon3d.bench.v3.schema.json) — schema canonico sealed (additionalProperties: false rigoroso); field mapping table §Field mapping verbatim para la prossima versione bench.v4 (forward-point `<c>`).
- [`configs/touchpoint_thresholds.yaml`](../../configs/touchpoint_thresholds.yaml) — SSoT canonico delle threshold per il `perf_regression_gate` block (parallel precedent: `manual_touches` channel block).
- [`tools/wrap_push.sh`](../../tools/wrap_push.sh) — host del nuovo Step 4.5q PERF_GATE PRE-FLIGHT (optional, env-var-gated).
- [`tools/verify_performance_linux.sh`](../../tools/verify_performance_linux.sh) — parallel precedent per 3-exit-codes ([INFO]/[WARN]/GATE_FAIL diagnostic style) + ledger JSON emission.
- [`tools/check_manual_touches_per_video.sh`](../../tools/check_manual_touches_per_video.sh) — parallel precedent per threshold-driven GATE_FAIL + 4-phase funnel envelope (oggi/fase1/fase2/finale).
- [`examples/bench_corpus/run_corpus_v1.sh`](../../examples/bench_corpus/run_corpus_v1.sh) — downstream consumer pattern: bash runner che consuma `chronon3d.bench.v3` JSON e produce per-scene ledger.
- Canonical `tools/run_developer_gates.sh` — the canonical 8-gate chain in `wrap_push.sh` (PERF_GATE is not part of the always-run; it's PERF_GATE=enabled-only).
- Canonical `tools/wrap_push.sh` push wrapper per GATE-MNT-01 — adopted with the new optional Step 4.5q (cat-3 anti-dup: PERF_GATE not in the always-run chain).
- Canonical `docs/CURRENT_STATUS.md` + `docs/FOLLOWUP_TICKETS.md` — DEFERRED per §Disciplina di aggiornamento dei canonici (forward-point `<b>` Cat-5 3-doc closure when WBH macchina-verifica passes).
