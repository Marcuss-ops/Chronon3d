# Follow-up Tickets — Open Blockers Index

> Stato corrente: [`docs/CURRENT_STATUS.md`](docs/CURRENT_STATUS.md).
> Dettaglio completo: [`docs/tickets/`](docs/tickets/).
> Cronologia ticket chiusi: [`docs/CHANGELOG.md`](docs/CHANGELOG.md).
> Cronologia estesa: [`docs/ARCHIVE/FOLLOWUP_TICKETS_HISTORY.md`](docs/ARCHIVE/FOLLOWUP_TICKETS_HISTORY.md).

## Open Blockers (≤10)

| Ticket | Pri | Status | Description |
|---|---|---|---|
| TICKET-NODE-CACHE-KEY-COLLAPSE-ROT | P2 | OPEN | NodeCacheKey not recognized in node_executor.cpp — build rot blocking macchina-verifica. [ticket](docs/tickets/TICKET-NODE-CACHE-KEY-COLLAPSE-ROT.md) |
| TICKET-CHANGELOG-UPSTREAM-MARKERS-FIX | P0 | OPEN | Audit-trail: upstream conflict markers in CHANGELOG resolved by `52e48ddd`; forward-point: extend check to `.md` prose. |
| TICKET-COMPLETENESS-GATE-V2-FIX-FORWARD | P1 | OPEN | Re-apply §20 unified video gate wire-chore post-rot-fix; 3 BLOCKING issues from prior `dd37f28e` push-block. |
| TICKET-125-TEST-AGGREGATOR | P0 | OPEN | Aggregator catalog (Tests 8-18); pre-requisito: push iterativo funzionante. |
| TICKET-CERT-SEQUENCE-WBH-PROTOCOL | P1 | OPEN | WBH execution protocol for 5 remaining First-Principles tests + 11/11 machine-verification. |
| TICKET-NODE-MEMORY-METRICS | P1 | OPEN | Per-node memory accounting struct + popolamento in node_runner.cpp + fail-loud gate. ADR-026 prerequisite. [ticket](docs/tickets/TICKET-NODE-MEMORY-METRICS.md) |
| TICKET-PERSISTENT-CACHE-ADR-GAP | P1 | OPEN | Missing ADR for dead persistent cache bridge; prerequisite per P1-B (dead code removal). [ticket](docs/tickets/TICKET-PERSISTENT-CACHE-ADR-GAP.md) |
| TICKET-SIMD-REGISTRY-CANONICAL | P1 | NEW | SIMD registry SSoT: enum CpuIsa + PixelKernelSet vtable + resolve_pixel_kernels. ADR-025 prerequisite. [ticket](docs/tickets/TICKET-SIMD-REGISTRY-CANONICAL.md) |
| TICKET-BENCHMARK-CORPUS-OFFICIAL | P1 | OPEN | 12 YAML scene descriptors (B00-B11) in `configs/benchmarks/corpus/`. Cat-3 minimal-surface. |
| TICKET-P1E-CPU-BUDGET-MEASUREMENT | P1 | HARNESS-COMPLETE | CpuBudget render/encode timing script. Macchina-verifica DEFERRED-WBH. [ticket](docs/tickets/TICKET-P1E-CPU-BUDGET-MEASUREMENT.md) |

## Deferred / Lower Priority

| Ticket | Pri | Status | Description |
|---|---|---|---|
| TICKET-CHRONON-LINUX-SCRIPT-PRESET-MISMATCH | P2 | OPEN | `tools/chronon-linux.sh` hardcodes `linux-release` preset which doesn't exist. |
| TICKET-ALPHA-BBOX-SCANNER-DEDUP-EXECUTOR | P2 | OPEN | Deduplicate parallel `alpha_bbox_scan` copy in `node_runner.cpp:412` against canonical scanner. |
| TICKET-CONCURRENT-AGENT-RACE-LIFECYCLE | P2 | OPEN | Process-ticket documenting concurrent-agent race windows (3+ upstream advancements per chore cycle). |
| TICKET-PUSH-CADENCE-OPTIMIZATION | P2 | NEW | Monitor if 4-attempt race-cycle pattern recurs; decision rule: >3 iterations → escalate. |
| TICKET-PUSH-DEFERRED-96-BEHIND | P2 | OPEN | 5 un-pushed commits on local main; deferred per WBH session. |
| TICKET-LOST-COMMIT-WORKSPACE-RESCUE | P2 | OPEN | `tools/recover_workspace_rescue.sh` cat-4 tool. [ticket](docs/tickets/TICKET-LOST-COMMIT-WORKSPACE-RESCUE.md) |
| TICKET-EXECUTOR-TILE-PRUNE-SKIP-UNIFICATION | P2 | OPEN | Tile-skip unification rot in `node_runner.cpp:272-279`. [ticket](docs/tickets/TICKET-EXECUTOR-TILE-PRUNE-SKIP-UNIFICATION.md) |
| TICKET-EXECUTOR-SKIP-POLICY-PUSH-BLOCKED | P2 | OPEN | Push-block deferral cronaca; resolution witness `4be8d513`. [ticket](docs/tickets/TICKET-EXECUTOR-SKIP-POLICY-PUSH-BLOCKED.md) |
| TICKET-TILE-PRUNE-SKIP-UNIFICATION-FIX | P2 | OPEN | Forward-point FIX: extend SkipReason + generalize `commit_transparent_skip`. [ticket](docs/tickets/TICKET-TILE-PRUNE-SKIP-UNIFICATION-FIX.md) |
| TICKET-TILE-PRUNE-SKIP-UNIFICATION-3DOC-CAT5-ALIGN | P2 | OPEN | Cat-5 3-doc closure chaser-chore. [ticket](docs/tickets/TICKET-TILE-PRUNE-SKIP-UNIFICATION-3DOC-CAT5-ALIGN.md) |
| TICKET-129-CLOSURE-TRACE-PATTERN-SWEEP | P2 | NEW | Bidir round-trip closure-trace pattern sweep for DONE/OPEN rows with ticket files. |
| TICKET-CLI-ISOLATE-RUNTIME-DEV | P1 | DONE | CLI 3-layer split: production + content bridge + DEV-gated. Macchina-verifica DEFERRED-WBH. |
| TICKET-GLOW-FINAL-COMPOSITIONS-DOC-MIGRATION-3DOC-CAT5-ALIGN | P2 | OPEN | Cat-5 3-doc deferred for Step 7 glow file move. |
| TICKET-GLOW-FINAL-COMPOSITIONS-MOVE-LOST-EDGES | P2 | OPEN | Recovery race artifact from `93cf6748` glow move; fix-up `bf02ac0` landed. |
| TICKET-INSPECT-TEXT-3DOC-CAT5-ALIGN | P2 | OPEN | Cat-5 3-doc deferred for Step 3 inspect refactor. |
| TICKET-MON-3DOC-CAT5-ALIGN | P3 | OPEN | Cat-5 3-doc for `tools/monitor_push_divergence.sh`. |
| TICKET-TEXT-V1-CERT-STEP11-DEFERRED | P1 | DEFERRED | Text V1 cert Step 11 finale; blocked by build-rot cascade. |
| TICKET-VIDEO-REPEATABILITY | P1 | HARNESS-COMPLETE | §16 repeatability + §17 3×2×60 matrix. Macchina-verifica DEFERRED-WBH. |
| TICKET-128-TEST-18-WEEKLY-DASHBOARD | P1 | OPEN | Weekly founder dashboard (8 metrics). |
| TICKET-TEST-17-COMPARISON-VERIFY | P0 | OPEN | 8-metric workspace comparison Chronon3D vs Remotion on WBH. |
| TICKET-TEST-9-PILOT-7GG | P1 | OPEN | Test 9 pilot-protocol harness + feedback form. |
| TICKET-TEST-13-INDEXING | P0 | OPEN | Test 13 reconciliation: alias-for-Test-11 OR separate framework slot. |
| TICKET-SUNSET-VERIFY | P0 | OPEN | Test 16: workspace-level safe deletion of `content/examples/` + `content/ae_parity/`. |
| TICKET-SUNSET-GATE | P2 | PLANNED | Cat-4 gate `tools/check_feature_sunset.sh`; requires ADR for markdown AST-parse in bash. |

## Recently Closed

| Ticket | Description |
|---|---|
| TICKET-TEXT-HEADER-CMAKE-REWIRE-ROT | rot-fix cascade: alpha_bbox_scanner + CMake rewire + visibility headers. 4 atomic commits. |
| TICKET-PREFLIGHT-PATH-EXISTENCE-MAP-EXISTS-BODY | Implemented `PathExistenceMap::exists()` body — link rot fix. |
| TICKET-TEXT-INSPECTION-ALPHA-BBOX-VISIBILITY | 8 NEW files: alpha_bbox_scanner + text_visibility_reporting + text_inspection. |
| TICKET-RECOVERY-PATTERN-EXTRACT | `tools/recover_chore_push.sh` — WRITE-side SHA-triple belt-and-suspenders. |
| TICKET-REFACTOR-CONTENT-EXAMPLES-17 | 360 LoC text_animations.cpp split into 3 files; 10 inline registry.add() removed. |
| TICKET-REFACTOR-TESTS-SPLIT-18-19 | 717 LoC test split into 4 + harness; 723 LoC cmake split into 6 sub-files. |
| TICKET-SABOTAGE-FONT-REAL-ENGINE | Replaced false-green stub with 3 real TEST_CASEs using FontEngine::shape_text(). |
| TICKET-TEXT-V1-FUNCTIONAL-CERT | 20 TEST_CASEs + `tools/verify_text_functional_linux.sh` gate. Macchina-verifica DEFERRED-WBH. |
| TICKET-VERIFY-SDK-CONSUMER-FUNCTIONAL-LINUX | 7-section SDK consumer cert gate + `main_full.cpp` (6 surface + 6 isolation). |
| TICKET-MON-PUSH-DIVERGENCE-CRON | `tools/monitor_push_divergence.sh` cron-friendly wrapper; 8 scenarios macchina-verificati. |
| TICKET-CHRONON-GLOW-FINAL-DEDUP-PROPS | ChrononGlowProps simplified to 5 fields; 6 test files migrated. |
| TICKET-VIDEO-CONTRACTS-BULK | §14+§15+§17+§18+§19 regression lock. Landed in `31cdfba`. |
| TICKET-VIDEO-ANTI-FLICKER | §8 anti-flicker regression lock. Landed in `f3b10c6`. |
| TICKET-VIDEO-MULTI-FPS-EQUIVALENCE | §13 multi-fps regression lock. Landed in `f3b10c6`. |
| TICKET-ANIM-CURVES-REGRESSION | §9 animation curves regression lock. 3 TEST_CASEs. |
