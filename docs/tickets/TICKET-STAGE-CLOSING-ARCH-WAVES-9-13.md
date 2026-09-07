# TICKET — Stage closing: architecture waves 9–13 (CLI/IPC parity → certification)

| | |
|---|---|
| **Status** | IMPLEMENTED (campaign closing pass) |
| **Scope** | Chronon3d `src/` + `apps/chronon3d_cli/` + `tools/` + RenderingGen (`renderinggen`, `queue`) |
| **Principle** | After this stage the *old* paths must be hard to recreate, not just absent: the gates below make each reintroduction a GATE FAIL. |

## Wave 9 — CLI/IPC parity (single parse, single normalize, single execute)

* Typed dispatcher: `RenderJobDispatcher` carries `NormalizedRenderJob` (daemon boundary), never a JSON `std::string`.
* Canonical IPC decode: `decode_render_job_payload()` in `apps/chronon3d_cli/daemon/daemon_render_job_payload.hpp/.cpp` — the *only* RENDER_JOB JSON parse + normalize in the daemon (`daemon_service_ipc.cpp` pinned to exactly the two parse sites: RENDER_JOB decode + ASSEMBLE_SEGMENTS).
* JSON re-forward deleted: no `forward_payload = request.dump()` anywhere; execution layer receives the immutable normalized job.
* Second normalization deleted: `execute_render_plan` hard-requires a `NormalizedRenderJob` (error branch, no fallback normalize).
* Warm/non-warm: warm renderer is execution state passed through the single `execute_normalized_render_job` authority (no second dispatcher registry).
* Parity gate: `tests/cli/test_render_job_planning.cpp` compares the **normalized fingerprint** (CLI boundary vs IPC wire decode) across the request matrix (range / audio / fps rational / codec / NVENC / direct-YUV / receipt / CRF-QP-bitrate / fallback / trace).
* Enforcement: `tools/architecture_rules_wave9.toml` (forbidden symbols `forward_payload`, `WarmRenderJobDispatcher`; `nlohmann::json::parse` confined to the codec file; `normalize_render_job` call sites confined to the two canonical boundary files; contract on the canonical payload decoder declaration).

## Wave 10 — Observability ownership

* Chronon owns frame/graph/GPU/decode/encode measurements. New bounded **`chronon3d.render-telemetry-summary.v1`** sidecar (`pipe_timing_sidecar_summary_v1.cpp`) is distinct from the raw `*.timing.json` artifact.
* RenderingGen no longer parses/mutates the raw Chronon timing schema: `ReadTimingSidecar` JSON mutation and `frame_times_ms` dropping deleted; raw timing is stored opaquely (bytes → hash → object store → `RawTimingArtifactRef`).
* Queue artifact contract carries only `EngineTelemetrySummaryV1` (typed/bounded) + `RawTimingArtifactRef` (`storage_key`, `sha256`, `size_bytes`, `content_type`, `schema_hint`); `RawMessage` raw-telemetry transport removed from `queue/client`.
* Metric-name scraping on Chronon internals removed.

## Wave 11 — Architecture gates (doc → enforcement)

* One engine: `tools/check_architecture.py` reading `tools/architecture_rules.toml`. New fragments: `architecture_rules_wave9.toml` (above) and `architecture_rules_wave12.toml`.
* New rule kind **`cmake_dag`** in the same engine: `forbidden_target` (demolished targets must not be recreated, ALIAS included) and `forbidden_edge` (source target must not link forbidden aggregates). Source-of-truth over CMakeLists/*.cmake; CI re-runs on the configured codemodel.
* Certify the certifier: `tools/architecture_gate_selftest.sh` — 10 assertions (forbidden_symbol / occurrence_cap / contract / unique_owner / cmake_dag ×2 negatives, plus a compliant wave9 mirror that must PASS and three single-pattern reintroductions that must FAIL). Wired into `tools/gates/manifest.sh` developer gates.
* Runtime invariants (normalize_count==1, zero-copy counters) live in the certification tests / zero-copy certificate, not in the static checker.

## Wave 12 — CMake DAG demolition

* Census: every `chronon3d_software` consumer was in-tree only (definition + registry).
* `chronon3d_software` aggregate demolished — a target named "software" pulled in Vulkan + GPU video backends. Its members now sit directly on `chronon3d_pipeline` (semantic backend capability boundaries). Removed from `CHRONON3D_REGISTRY_INTERFACE_LIBS`. Recreation forbidden.
* `chronon3d_media_direct_yuv` (leaf/specialization) no longer links `chronon3d_pipeline`; it declares the media subsystem boundary (`chronon3d_media_interface`) + its native authorities. The edge is forbidden.

## Wave 13 — Final certification

* `tools/verify_zero_copy_end_to_end.sh`: strict sidecar authority (missing `*.timing.json` ⇒ `ZERO_COPY_BLOCKED`, **no log fallback**); 13 mandatory gates (8 zero counters, 2 positive native-identity counters, backend/encoder/surface-handoff identities). `docs/ZERO_COPY_GATES.md` updated to match.
* `tools/chronon_certification.py` assembles the signable **`chronon-certification.json`** from the existing authorities (architecture gate log, zero-copy verdict + report, correctness exit codes, optional perf summary). Not a new authority — an aggregate.

## Residual follow-ons (not regressions)

1. Absorb/delete the DirectYUV executor facade into the single execution authority once the `require_direct_yuv` hot-path migration completes (Wave 12E). Until then the capability is live and the CMake edge is forbidden, not the component.
2. Re-run the `cmake_dag` invariants against the **configured codemodel** (File API) in CI, not only the CMake sources.
3. Performance certificate corpus + repeated-run p50/p95 baseline budgets (requires GPU/NVENC bench host; harness contract documented in ZERO_COPY_GATES + verify script).
