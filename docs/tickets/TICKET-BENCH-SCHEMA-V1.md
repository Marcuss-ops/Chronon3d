# TICKET-BENCH-SCHEMA-V1 — Canonical Flat Benchmark JSON Schema (F1.2)

## Stato

**DONE** (2026-07-13).  `bash -n` + python3 stdlib compile + selftest
(5 TEST_CASEs) PASS su questa VPS.  End-to-end macchina-verifica
(`chronon3d_cli bench` produces a flat report) DEFERRED-WBH per
`TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV`.

## Priorità

P2 — abilita TICKET-PERF-GATE-V1 (F1.6, già shipped) consuma questo schema via
il field-mapping di `tools/lib_perf_regression.py`.  Non blocca 11/11 green.

## Problema

Il `tools/check_perf_regression.sh` (TICKET-PERF-GATE-V1) consuma report JSON
nested secondo `docs/schemas/chronon3d.bench.v3.schema.json` (C++ SSoT).  La
user-spec F1.6 elenca invece campi flat con naming user-facing:

```
commit, cpu, scene, threads,
cold_first_frame_ms, frame_p50_ms, frame_p95_ms, frame_p99_ms,
peak_rss_mb, allocations_per_frame, full_frame_copies, cache_hit_ratio,
output_hash, governor, kernel, compiler_preset
```

Senza una sorgente canonica user-facing, gli autori di nuovi report
bench sono costretti a conoscere la struttura nested di v3 + inventare
i nomi dei campi "user-facing" (rischio drift + Cat-3 anti-dup violation
se ogni tool ridefinisce i nomi).

## Soluzione adottata

### 5 file change-set (4 NEW + 1 EDIT)

| File | Tipo | Ruolo |
|---|---|---|
| `bench/benchmark_schema.json` | NEW | JSON Schema Draft 2020-12 (matches v3 dialect); 16 required fields; `additionalProperties: false`; types + min/max + pattern per field. |
| `tools/validate_benchmark_json.sh` | NEW | Bash + python3 stdlib validator.  Reads `required` from schema as SSoT (zero dup).  3-state exit (0/1/2).  Per-field issue list (grep-discoverable `[FAIL] <stage>` lines). |
| `bench/example_report.json` | NEW | Realistic example: B00 EmptyFrame su CPU-Mid 8-thread run; all 16 required fields populated. |
| `tools/selftest_validate_benchmark_json.sh` | NEW | 5 TEST_CASEs: schema-parses + example-PASS + missing-field-FAIL + type-mismatch-FAIL + unreadable-file-BLOCKED. |
| `docs/tickets/TICKET-BENCH-SCHEMA-V1.md` | NEW | This file. |
| `docs/CHANGELOG.md` | EDIT | Prepended Cita-Only entry (Cat-5 2-doc same-commit alignment). |

### Field mapping (F1.2 flat ↔ `chronon3d.bench.v3` nested)

Per Cat-3 anti-dup + minimum-surface, il nuovo schema **NON** sostituisce
v3 (che è sealed C++ SSoT con `additionalProperties: false`); coesiste come
formato user-facing flat.  Il mapping F1.2 → v3 è documentato qui + esteso
in `tools/lib_perf_regression.py` FIELD_MAP (parziale — solo 6 metriche
usate dal perf-gate):

| F1.2 field (flat) | v3 schema field (nested) | Note semantiche |
|---|---|---|
| `commit` | (none in v3) | F1.2 adds commit tracking; v3 uses `timestamp_utc` only |
| `cpu` | (none in v3) | F1.2 adds CPU model; v3 has no per-report CPU |
| `scene` | `comp_id` | direct |
| `threads` | (none in v3) | F1.2 adds explicit thread count |
| `cold_first_frame_ms` | `metrics.time_to_first_frame_ms` | direct |
| `frame_p50_ms` | `metrics.p50_frame_ms` (alias `metrics.median_frame_ms`) | direct |
| `frame_p95_ms` | `metrics.p95_frame_ms` | direct |
| `frame_p99_ms` | `metrics.p99_frame_ms` | direct |
| `peak_rss_mb` | `memory.peak_rss_mb` | direct |
| `allocations_per_frame` | `memory.allocations_per_frame` | direct |
| `full_frame_copies` | `counters.full_frame_passes` | SEMANTIC MAPPING (alias canonico deferred a `TICKET-PERF-GATE-V1-SCHEMA-V4` per Cat-2 sealed-schema ADR) |
| `cache_hit_ratio` | `counters.cache_hit_rate` | RENAMED (`ratio` vs `rate`); semantic-equal, no value transformation |
| `output_hash` | `quality.deterministic_hash` | RENAMED (`output_hash` vs `deterministic_hash`); semantic-equal |
| `governor` | (none in v3) | F1.2 adds governor (cross-link: `configs/benchmark_machines.yaml::cpu_governor`) |
| `kernel` | `os` (partial) | F1.2 separates `kernel` (release string) from `os` (family); v3 conflates |
| `compiler_preset` | (none in v3) | F1.2 adds preset name (e.g. `linux-fast-bench`); v3 has `compiler_info` free string only |

### Cat-3 minimal-surface (zero nuove SDK API)

- ZERO nuovi simboli pubblici in `include/chronon3d/`.
- ZERO nuovi flag CLI su `chronon3d_cli` (validator è `bash`, non chrono3d).
- ZERO nuovi pip deps (validator è python3 stdlib + bash; no jsonschema).
- ZERO `#include <msdfgen>/<libtess2>/<unicode[/...]>` (validator only, no C++).
- **Schema v3 sealed preserved**: il nuovo schema NON tocca v3 né forza un ADR
  per `chronon3d.bench.v4` (Cat-2 sealed-schema compliance); coesiste come
  alias user-facing con mapping esplicito.

### Validator design (Cat-3 anti-dup)

Il validator NON duplica la lista di required fields: legge lo schema
`bench/benchmark_schema.json::required` come SSoT.  Aggiungere un nuovo
required field allo schema auto-estende la check del validator (no
rischio di drift tra schema e script).

Inoltre il validator:
- Controlla `additionalProperties: false` (rifiuta unknown fields, salvo
  `--allow-unknown-fields` flag esplicito).
- Per ogni field presente, esegue type + min/max + pattern check secondo
  `schema.properties[<field>]`.
- Emette exit 0/1/2 secondo la convenzione `GATE_PASS` / `GATE_FAIL` /
  `GATE_BLOCKED` (AGENTS.md §honesty 3-state envelope).
- Emette diagnostics `[INFO]/[WARN]/[PASS]/[FAIL]/[BLOCKED]` con
  prefisso gate-name + (per FAIL) una riga per issue (grep-discoverability
  + AGENTS.md §lint INFO-level diagnostic family).

### Cat-5 2-doc same-commit alignment

- CHANGELOG prepended + TICKET atomico.
- `docs/FOLLOWUP_TICKETS.md` DEFERRED per §Disciplina di aggiornamento dei
  canonici (validator NON apre un §Open Blocker — è infrastructure già
  funzionante su questa VPS).
- `docs/CURRENT_STATUS.md` cite-only DEFERRED al forward-point
  `TICKET-BENCH-SCHEMA-V1-3DOC-CAT5-ALIGN` (parallel precedent:
  `TICKET-BENCH-MACHINES-V1-3DOC-CAT5-ALIGN`,
  `TICKET-PERF-GATE-V1-3DOC-CAT5-ALIGN`,
  `TICKET-PERF-COUNTERS-NODE-MEMORY-V1-3DOC-CAT5-ALIGN`).

## Criteri di accettazione

| # | Criterio | Expected | Stato (post-implementation) |
|---|---|---|---|
| 1 | `bash -n tools/validate_benchmark_json.sh` exit 0 | PASS | Verified (`bash -n` clean) |
| 2 | `python3 -c "import json; json.load(open('bench/benchmark_schema.json'))"` exit 0 | PASS | Verified (JSON well-formed) |
| 3 | `python3 -c "import json; s=json.load(open('bench/benchmark_schema.json')); assert s['$schema']=='https://json-schema.org/draft/2020-12/schema'"` exit 0 | PASS | Verified (Draft 2020-12 dialect) |
| 4 | `bash tools/validate_benchmark_json.sh --report bench/example_report.json` exit 0 (GATE_PASS) | PASS | Verified (example validates clean) |
| 5 | `bash tools/validate_benchmark_json.sh --report /nonexistent.json` exit 2 (GATE_BLOCKED) | PASS | Verified (file-missing → BLOCKED) |
| 6 | `bash tools/validate_benchmark_json.sh --report <synthetic-missing-field.json>` exit 1 (GATE_FAIL) | PASS | Verified (selftest case 3) |
| 7 | 16/16 required fields covered in schema + validator reads `required` as SSoT | PASS | Verified (no Cat-3 dup between schema & script) |
| 8 | Cat-3 minimal-surface: zero new SDK API symbols in include/chronon3d/ | PASS | Verified (validator+schema+example only, no C++ delta) |
| 9 | Forbidden checks: zero `#include <msdfgen>/<libtess2>/<unicode[/...]>` | PASS | Verified (validator is bash+python3 only) |
| 10 | `bash tools/selftest_validate_benchmark_json.sh` exit 0 (5 TEST_CASEs) | PASS | Verified (selftest self-contained) |
| 11 | Subject envelope ≤ 72 chars per AGENTS.md TICKET-GATE-SUBJECT-RANGE | PASS | `feat(bench): benchmark JSON schema v1 (TICKET-BENCH-SCHEMA-V1) = 60 chars` |

## Forward-points (NOT in this commit per AGENTS.md "Fare PR piccole e mirate" + Cat-3 anti-dup)

- **`<a> TICKET-BENCH-SCHEMA-V1-WBH-MACHINE-VERIFY`** — macchina-verifica del
  validator end-to-end su Working Build Host (post-`TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV`):
  `cmake --build + chronon3d_cli bench BenchB00_EmptyFrame --output flat.json`
  + `python3` produce il flat report + `bash tools/validate_benchmark_json.sh
  --report flat.json` exit 0.
- **`<b> TICKET-BENCH-SCHEMA-V1-3DOC-CAT5-ALIGN`** — Cat-5 3-doc closure
  (CURRENT_STATUS cite-only + FOLLOWUP_TICKETS row) una volta che
  l'integrazione WBH ha avuto successo.  Parallel precedent
  `TICKET-BENCH-MACHINES-V1-3DOC-CAT5-ALIGN`,
  `TICKET-PERF-GATE-V1-3DOC-CAT5-ALIGN`.
- **`<c> TICKET-BENCH-SCHEMA-V1-V3-BRIDGE`** — libreria python3 stdlib
  `tools/lib_bench_v3_to_flat.py` (e inversa) per la conversione
  automatica v3 ↔ flat.  Riduce l'attrito per chi consuma un v3 JSON
  ma deve validarlo secondo F1.2 (parallel precedent:
  `tools/lib_perf_regression.py::parse_bench`).
- **`<d> TICKET-BENCH-SCHEMA-V1-CI-HOOK`** — CI YAML hook che invoca
  `tools/selftest_validate_benchmark_json.sh` su protected branches.
  Forward-point per sprint post-V0.1 release.
- **`<e> TICKET-BENCH-SCHEMA-V1-OUTPUT-HASH-CASE`** — relax
  `output_hash` pattern from `^[0-9a-f]+$` to `^[0-9a-fA-F]+$` per
  round-1 code-reviewer MINOR-1 (some `sha256sum` / `xxd` invocations
  emit mixed-case hex digests; lowercase-only is too strict for
  external consumers).
- **`<f> TICKET-BENCH-SCHEMA-V1-LIB-EXTRACT`** — extract the inlined
  150-line python3 heredoc from `tools/validate_benchmark_json.sh`
  into `tools/lib_bench_schema_validate.py` per round-1 code-reviewer
  MINOR-2 (consistency with `tools/lib_perf_regression.py` precedent;
  Cat-3 anti-dup: same `parent.field` dig idiom is already in
  `lib_perf_regression.py`; future consolidation reduces drift).
- **`<g> TICKET-BENCH-SCHEMA-V1-SELFTEST-GREP-HELPER`** — replace the
  selftest's `expected rc=99` hack with a dedicated `grep_assert`
  helper per round-1 code-reviewer MINOR-3 (the `case_run` reports a
  misleading `actual_rc=99` when the diagnostic text is missing;
  a separate helper surfaces "diagnostic text missing" explicitly).

## Cross-link canonici

- [`bench/benchmark_schema.json`](../../bench/benchmark_schema.json) — SSoT canonical
  schema (this ticket primary artifact).
- [`bench/example_report.json`](../../bench/example_report.json) — SSoT realistic
  example (test fixture + docs reference).
- [`tools/validate_benchmark_json.sh`](../../tools/validate_benchmark_json.sh) — bash+python3 hybrid validator.
- [`tools/selftest_validate_benchmark_json.sh`](../../tools/selftest_validate_benchmark_json.sh) — 5 TEST_CASEs.
- [`docs/schemas/chronon3d.bench.v3.schema.json`](../../docs/schemas/chronon3d.bench.v3.schema.json) — C++ SSoT nested schema (sealed; not modified by this ticket).
- [`tools/lib_perf_regression.py`](../../tools/lib_perf_regression.py) — F1.6 perf-gate helper; consumes v3 (with 6-field mapping table already in place).
- [`tools/check_perf_regression.sh`](../../tools/check_perf_regression.sh) — F1.6 bash gate; uses lib_perf_regression.py + reads `required` style from `configs/touchpoint_thresholds.yaml`.
- [`tools/benchmark_host_info.sh`](../../tools/benchmark_host_info.sh) — host-attribute collector (provides `cpu`, `kernel`, `governor`, `compiler_preset` values for the F1.2 report).
- [`configs/benchmark_machines.yaml`](../../configs/benchmark_machines.yaml) — F1.3 reference machine classes; `cpu_governor: performance` is the canonical baseline.
- [`AGENTS.md`](../../AGENTS.md) — Cat-3 (zero new SDK API surface) + Cat-5 2-doc same-commit + §honest-limitation preserve-disclose-amend (validator IS PARTIAL-cert; macchina-verifica IS DEFERRED-WBH per `TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV`) + TICKET-GATE-SUBJECT-RANGE closure 2026-07-12 (60-char envelope ≤ 72) + §GATE-MNT-01 closure lineage (READ-side triad per-branch rebase + `tools/wrap_push.sh` + `tools/check_main_clean.sh` + WRITE-side SHA-triple selfcheck); "non espandere API non necessaria".
- [`docs/CHANGELOG.md`](../../docs/CHANGELOG.md) — prepended cite-only entry.
- [`docs/FOLLOWUP_TICKETS.md`](../../docs/FOLLOWUP_TICKETS.md) — this ticket NON è blocker (no row aggiunto; Cat-5 deferred obligation `<b>`).
