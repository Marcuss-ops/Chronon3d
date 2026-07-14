# Follow-up Tickets — Open Blockers Index

> Stato corrente: [`docs/CURRENT_STATUS.md`](docs/CURRENT_STATUS.md).
> Dettaglio completo: [`docs/tickets/`](docs/tickets/).
> Cronologia ticket chiusi: [`docs/CHANGELOG.md`](docs/CHANGELOG.md).
> Cronologia estesa: [`docs/ARCHIVE/FOLLOWUP_TICKETS_HISTORY.md`](docs/ARCHIVE/FOLLOWUP_TICKETS_HISTORY.md).

## Open Blockers (≤10)

| Ticket | Pri | Status | Description |
| TICKET-TOOLS-ORPHAN-AUDIT | P2 | NEW | tools/ script audit: 95 classified, 0 removed. Removals deferred to 4 forward-points per Cat-3 minimal surface. | [ticket](docs/tickets/TICKET-TOOLS-ORPHAN-AUDIT.md) |
| TICKET-TEXT-SPEC-FORWARDER-REMOVAL | P2 | FORWARD-POINT REGISTER | 2×-in-one-chore orphan-intent register: LayerBuilder::text(name,TextSpec) overload REMOVED; cronaca in canonical ticket-home. | [ticket](docs/tickets/TICKET-TEXT-SPEC-FORWARDER-REMOVAL.md) |
|---|---|---|---|
| TICKET-TOOLS-ORPHAN-AUDIT | P2 | NEW | tools/ script audit: 95 classified, 0 removed. Removals deferred to 4 forward-points per Cat-3 minimal surface. | [ticket](docs/tickets/TICKET-TOOLS-ORPHAN-AUDIT.md) |
| TICKET-TEXT-SPEC-FORWARDER-REMOVAL | P2 | FORWARD-POINT REGISTER | 2×-in-one-chore orphan-intent register: LayerBuilder::text(name,TextSpec) overload REMOVED; cronaca in canonical ticket-home. | [ticket](docs/tickets/TICKET-TEXT-SPEC-FORWARDER-REMOVAL.md) |
| TICKET-PREMULT-TEST-SWEEP | P2 | OPEN | Uniform Premult-invariant canonicalization across the 4 other currently-passing TEST_CASEs (`alpha=1` + `midpoint` + `pixel counts {0..1024}` + AVX2 parity) in `tests/simd/test_simd_parity_blend.cpp`. [ticket](docs/tickets/TICKET-PREMULT-TEST-SWEEP.md) |
| TICKET-PREMULT-CALLER-AUDIT | P2 | OPEN | Production-side Premult invariant audit in `src/backends/software/*` + `src/render_graph/nodes/*` callers of `scalar_blend` + composite_normal_premul. FORWARD-POINT from `b16ad302` per code-reviewer-minimax-m3 MINOR #3. [ticket](docs/tickets/TICKET-PREMULT-CALLER-AUDIT.md) |
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
| TICKET-TEST-GREP-TO-FUNCTIONAL-MIGRATION | P2 | NEW | Test(text)+(tools): 92+ functional-test sites baseline; 4-Phase migration plan per cat-5 3-doc; cronaca in canonical ticket-home. | [ticket](docs/tickets/TICKET-TEST-GREP-TO-FUNCTIONAL-MIGRATION.md) |
| TICKET-PROCESS-WIDE-STATE-MIGRATION | P2 | NEW | Refactor(session): Fase B2+B3 already-completed migration; vacuous-truth audit; cronaca in canonical ticket-home. | [ticket](docs/tickets/TICKET-PROCESS-WIDE-STATE-MIGRATION.md) |
| TICKET-GRAPH-EXECUTION-REQUEST-REFACTOR | P2 | NEW | Refactor(render_graph): spec-vs-reality audit + 5-Phase migration; cronaca in canonical ticket-home. | [ticket](docs/tickets/TICKET-GRAPH-EXECUTION-REQUEST-REFACTOR.md) |
| TICKET-TEXT-DEFINITION-ADAPTER-SPLIT | P2 | NEW | Refactor(text): 3-file split + 4-Phase migration + F2.D LOSSY removal; cronaca in canonical ticket-home. | [ticket](docs/tickets/TICKET-TEXT-DEFINITION-ADAPTER-SPLIT.md) |
| TICKET-CHRONON-LINUX-SCRIPT-PRESET-MISMATCH | P2 | OPEN | `tools/chronon-linux.sh` hardcodes `linux-release` preset which doesn't exist. |
| TICKET-CONCURRENT-AGENT-RACE-LIFECYCLE | P2 | OPEN | Process-ticket documenting concurrent-agent race windows (3+ upstream advancements per chore cycle). |
| TICKET-PUSH-CADENCE-OPTIMIZATION | P2 | NEW | Monitor if 4-attempt race-cycle pattern recurs; decision rule: >3 iterations → escalate. |
| TICKET-PUSH-DEFERRED-96-BEHIND | P2 | OPEN | 5 un-pushed commits on local main; deferred per WBH session. |
| TICKET-LOST-COMMIT-WORKSPACE-RESCUE | P2 | OPEN | `tools/recover_workspace_rescue.sh` cat-4 tool. [ticket](docs/tickets/TICKET-LOST-COMMIT-WORKSPACE-RESCUE.md) |
| TICKET-129-CLOSURE-TRACE-PATTERN-SWEEP | P2 | NEW | Bidir round-trip closure-trace pattern sweep for DONE/OPEN rows with ticket files. |
| TICKET-CLI-ISOLATE-RUNTIME-DEV | P2 | NEW | CLI 3-layer split: production + content bridge + DEV-gated. | [ticket](docs/tickets/TICKET-CLI-ISOLATE-RUNTIME-DEV.md) |
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
| TICKET-CAMERA-LEGACY-ADAPTER-REMOVAL | P2 | OPEN | Refactor(camera): 279 callsites migration to canonical V1 + physical legacy class removal; cronaca in canonical ticket-home. | [ticket](docs/tickets/TICKET-CAMERA-LEGACY-ADAPTER-REMOVAL.md) |
| TICKET-MOTIONTIMELINE-MIGRATION | P2 | IN-PROGRESS | Phase 1 DONE 2026-07-14; 11 motion presets migrated to AnimationTrack\<T\>; Phase 2+3 deferred; cronaca in canonical ticket-home. | [ticket](docs/tickets/TICKET-MOTIONTIMELINE-MIGRATION.md) |
| TICKET-COMPOSITIONDESCRIPTOR-MIGRATION | P2 | NEW | Re-add [[deprecated]] on `CompositionRegistry::add(name, factory)` + migrate ~265 call sites to `add(CompositionDescriptor{.id, .factory, ...})`. Forward-point of recovery-chore deprecation reversal. [ticket](docs/tickets/TICKET-COMPOSITIONDESCRIPTOR-MIGRATION.md) |

## Recently Closed

| Ticket | Description |
|---|---|
| TICKET-LEGACY-QUEUE-METHODS-FOLLOWUP-IMPL | P3 | CLOSED 2026-07-14 | chore(queue): audit external consumers for legacy try_dequeue/enqueue (vacuous-truth: 0 productive callers in examples/ + downstream consumers + docs/); cronaca in canonical ticket-home. | [ticket](docs/tickets/TICKET-LEGACY-QUEUE-METHODS-FOLLOWUP-IMPL.md) |
| TICKET-PARSE-POLICY-HELPER-DEDUP-IMPL | P3 | CLOSED 2026-07-14 | refactor(cache): extract parse_framebuffer_pool_clear_policy helper; 2-place dedup (config.cpp + render_job.cpp); cronaca in canonical ticket-home. | [ticket](docs/tickets/TICKET-PARSE-POLICY-HELPER-DEDUP-IMPL.md) |
| TICKET-RENDER-SERVICES-FULL-ELIMINATION-IMPL | P3 | CLOSED 2026-07-14 | vacuous-truth audit: runtime RenderServices already removed in P1-15; graph::RenderServices is distinct per-frame bundle (out of scope); cronaca in canonical ticket-home. | [ticket](docs/tickets/TICKET-RENDER-SERVICES-FULL-ELIMINATION-IMPL.md) |
| TICKET-RENDER-RUNTIME-MIGRATION-FOR-TESTS-IMPL | P3 | CLOSED 2026-07-14 | refactor(tests): migrate RenderRuntime ctor to create factory at 4faf81b4; 19 test files / ~228 sites; cronaca in canonical ticket-home. | [ticket](docs/tickets/TICKET-RENDER-RUNTIME-MIGRATION-FOR-TESTS-IMPL.md) |
| TICKET-FB-POOL-CLEAR-POLICY-CALL-SITE-IMPL | P2 | CLOSED 2026-07-14 | feat(fb-pool): wire trim_after_job (default TrimAfterJob) at f1d8cc34; cronaca in canonical ticket-home. | [ticket](docs/tickets/TICKET-FB-POOL-CLEAR-POLICY-CALL-SITE-IMPL.md) |
| TICKET-RENDERGRAPH-CACHE-PERSISTENT-DEAD-FLAG-SCOPE-REALIGNMENT | P2 | CLOSED 2026-07-14 | chore(cache): scope-realignment audit; 3 target files already-clean per PR2-cleanup; cronaca in canonical ticket-home. | [ticket](docs/tickets/TICKET-RENDERGRAPH-CACHE-PERSISTENT-DEAD-FLAG-SCOPE-REALIGNMENT.md) |
| TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV-VPS-3DOC-CAT5-ALIGN | P2 | CLOSED 2026-07-14 | chore docs(ticket): vcpkg bootstrap honesty 3-doc alignment; VPS env-block audit; cronaca in canonical ticket-home. | [ticket](docs/tickets/TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV-VPS-3DOC-CAT5-ALIGN.md) |
| TICKET-FIX-TEXT-SHAPING-DEDUP-V1-3DOC-CAT5-ALIGN | P2 | CLOSED 2026-07-14 | chore docs(text): shape-dedup counter 3-doc closure; cronaca in canonical ticket-home. | [ticket](docs/tickets/TICKET-FIX-TEXT-SHAPING-DEDUP-V1-3DOC-CAT5-ALIGN.md) |
| TICKET-CAMERA-OVERLAY-PANEL-CONSTRAINTS | P2 | CLOSED 2026-07-14 | chore feat(diagnostics): camera overlay panel layout helper; cronaca in canonical ticket-home. | [ticket](docs/tickets/TICKET-CAMERA-OVERLAY-PANEL-CONSTRAINTS.md) |
| TILE-PRUNE-SKIP-UNIFICATION lineage (4 cronache) | CLOSED 2026-07-14: atomic chore `refactor(executor): unify tile_prune into commit_transparent_skip` (57 chars ≤ 72) unifies the 3-site skip-block pattern into single `commit_transparent_skip(...)`. 3 source changed (`node_skip_policy.hpp` + `.cpp` + `node_runner.cpp`). SkipReason enum extended `EarlyExit` + `OpacityThreshold` + **`TilePruned`** (tail-extension, ABI safe). `bbox_override: std::optional<raster::BBox> = std::nullopt` added to signature (default-nullopt preserves EarlyExit/OpacityThreshold byte-equivalence). TilePruned branch reuses `state.shared_transparent` (no fresh 64×64 alloc), bumps `nodes_skipped` (NOT `layers_culled`), preserves `predicted_bbox`. Cat-3 minimal-surface (zero new SDK API). macchina-verifica DEFERRED-WBH per `TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV` (vcpkg glm/magic_enum env-blocked) + §honest-limitation. Cronaca chiusura nelle 4 schede ticket-home ([TICKET-EXECUTOR-TILE-PRUNE-SKIP-UNIFICATION](docs/tickets/TICKET-EXECUTOR-TILE-PRUNE-SKIP-UNIFICATION.md), [FIX](docs/tickets/TICKET-TILE-PRUNE-SKIP-UNIFICATION-FIX.md), [3DOC-CAT5-ALIGN](docs/tickets/TICKET-TILE-PRUNE-SKIP-UNIFICATION-3DOC-CAT5-ALIGN.md), [SKIP-POLICY-PUSH-BLOCKED](docs/tickets/TICKET-EXECUTOR-SKIP-POLICY-PUSH-BLOCKED.md)) per AGENTS.md "Docs canonical update discipline rule" Cat-3 anti-dup. |
| TICKET-RESIDUAL-BUILD-ROT-RECOVERY | P2 | CLOSED 2026-07-14 | chore lineage 988e6c26 load-bearing layer_builder.hpp dedup; cronaca in canonical ticket-home. | [ticket](docs/tickets/TICKET-RESIDUAL-BUILD-ROT-RECOVERY.md) |
| TICKET-SIMD-PRECISION-DRIFT | P2 | CLOSED 2026-07-14 | hypothesis-(d) Premult alpha=0 fixture + SweepN regression; 6/6 PASS; cronaca in canonical ticket-home. | [ticket](docs/tickets/TICKET-SIMD-PRECISION-DRIFT.md) |
| TICKET-ALPHA-BBOX-SCANNER-DEDUP-EXECUTOR | **OBSOLETE** — executor bbox path canonically routes via `reconcile_text_bbox_after_render()` (node_runner.cpp:303) → canonical `chrono3d::alpha_bbox_scan()` (text_bbox_reconcile.cpp:59); zero inline legacy scanners. Closure witness: `TICKET-FIX-ALPHA-SCANNER-DUP-V1` (resolution of the scanner-side rot at commit `4791e98b`). |
| TICKET-PIPELINE-FAST-BUILD-RFC-2026-06-22 | Fast-build CMake hygiene gates; close-out via `docs/FAST_BUILD.md` + tooling pipeline. |
| TICKET-091 | disassembler-decomposition refactor merged; surgical closure link to commit `5649a2bf`. |
| TICKET-TEXT-PRESET-REGISTRY-PARITY | TextPresetDescriptor resolver/registrar equivalence lock; pure resolve contract verified at HEAD `main`. |
| TICKET-077 | golden-state RG gate (lock-grep discipline) survives `.md` recursion; diagnostic threshold detected. |
| TICKET-KERNEL-TILING-V1-F4.3-SCAFFOLD | Upstream scaffold landed F4.3 (tile_size/for_each_tile/execution_scheduler); the implied `bbox.hpp` provider landed in the next upstream commit (now `c83d8527`); scaffold-debt closure line. |
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
