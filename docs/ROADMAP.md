# Chronon3D — Active Roadmap

Current maturity: [`STATUS.md`](STATUS.md). Architecture packages:
[`refactor-roadmap/README.md`](refactor-roadmap/README.md).

> Ogni voce ha una delle tre categorie:
> - 🟡 **Partial** — parzialmente implementato; mancano pezzi significativi.
> - 🔵 **Planned** — non ancora implementato.
>
> Gli item completati (✅ Verified) sono in [`CHANGELOG.md`](CHANGELOG.md).
> Ultimo aggiornamento: 2026-06-21

## P0 — correctness and validation

| Work | State |
|---|---|
| Fix architecture-boundary script final exit handling | 🔴 Blocked |
| Migrate scheduler tests off `ExecutionPlanCache` and raw graphs | 🔴 Blocked |
| Synchronize `PrecompNode` and borrow the session executor | 🔴 Blocked |
| Move node identity assignment to cloned contexts | 🔴 Blocked |
| Add root/tile/child execution scopes | 🔵 Planned |
| Close install-consumer and global-state SDK boundaries | 🟡 Partial |
| Record clean required CI/build/test evidence | 🔴 Blocked |

P0 must complete before more architecture is added.

## Active feature and performance work

### Partial

- Motion-blur accumulation SIMD verification.
- Three-pass box-blur verification and benchmarks.
- Blend dispatcher specialization where benchmarks justify it.
- Temporary framebuffer aliasing ownership.
- `LayoutFlow` and `LayoutGrid`.
- Zero-copy encoder delivery.
- Pool miss-reason dashboard.
- SPSC queue only after correctness and benchmark proof.
- Dedicated tests for Shadow, Glow, Bloom, Gradient, DoF, and Mask nodes.

### Planned

- Parallel SIMD SSAA downsample.
- Adaptive framebuffer-pool preallocation.
- ISPC blur evaluation against Highway.
- Speculative multi-frame rendering.
- NUMA-aware framebuffer allocation.

## V3 tile-first

All ten pillars in [`V3_BLUEPRINT.md`](V3_BLUEPRINT.md) remain planned.

## Expressions V2

- Quarantine and default exclusion: complete.
- TICKET-003 and TICKET-004: closed historical fixes.
- Stable SDK export/install: not done.
- `AnimatedValue` integration: not done.
- Benchmark, replacement map, retirement deadline: not done.
- Quarantine removal: not done.

The authoritative promotion contract is
[`EXPRESSIONS_V2_PROMOTION.md`](EXPRESSIONS_V2_PROMOTION.md).

---

## 🟢 Recently Completed — cross-reference (lifecycle events)

These items landed recently and are recorded here so they appear in roadmap
scans; for full detail follow the cross-reference. (The "rec" prefix below is
a new convention introduced in 2026-06-20 for lifecycle events that did not
previously live under the N*/M*/L*/I* numbering scheme.)

| Prefix | Item | State | Cross-reference |
|---|---|---|---|
| rec-1 | `expressions/v2` engine merged on `main` via PR #23 (Experimental Zone) | ✅ Merged | `CHANGELOG.md` → "Expression System v2 — Lifecycle" |
| rec-2 | CMake guard `CHRONON3D_ENABLE_EXPERIMENTAL_EXPRESSIONS_V2` retired (deprecated option() retained) | ✅ Done | `CMakeLists.txt:200-237` |
| rec-3 | TICKET-005 Gap C — doc sweep recording that `expressions/v2` is now on `main` | ✅ Done | `FEATURES.md` + `CHANGELOG.md` + `ARCHITECTURE_EVOLUTION_PLAN.md` (this file's cross-reference) |
| rec-4 | WP-3 PR 3.4 close-out — legacy full-reset shim retired, `SoftwareRenderSession` consolidated to canonical header | ✅ Done | `CHANGELOG.md` → **R4 — WP-3 PR 3.4 close-out** |
| rec-5 | WP-8 close-out — `render_session.hpp` engine-generic again (scene_hasher + program_store relocated to `RenderRuntime`) | ✅ Done | `CHANGELOG.md` → **R5 — WP-8 close-out** |
| rec-6 | PR-2 rewire close-out — `ExecutionPlanCache` class deleted, `RenderGraph& execute()` overloads + `plan_cache` parameter retired | ✅ Done | `CHANGELOG.md` → **R6 — PR-2 rewire close-out** |
| rec-7 | TICKET-007 — process-wide `detail::g_debug_config` removed; per-instance `RenderGraphContext::debug_config` + `EffectExecutionContext::debug_cfg` forwarded | ✅ Done | commit `6d7306b7`; resolved in `FOLLOWUP_TICKETS.md` |

> **Open follow-ups (planned, tracked in `docs/FOLLOWUP_TICKETS.md`):**
>
> | Ticket | Title | Status | Compliance target |
> |---|---|---|---|
> | TICKET-002 | Pre-existing API rot in `chronon3d_diagnostics` target (~102 errors in `content/`) | 🔵 Planned | Full-build gate (402/402 targets) |
> | TICKET-005 | Post-cascade cleanup — revive `keyframes()` + discrepancy `expressions/v2` reconciliation | 🔵 Planned | Include-discipline + animation API |
> | TICKET-006 | Missing `chronon3d_backend_text` linkage in `chronon3d_tests_fast` (17 linker errors) | 🔵 Planned | `linux-ci` build rc=0 |
> | TICKET-008 | Wire `ctx.policy.graph_structure_unchanged` into `FrameGraphCompiler::compile` | 🔵 Planned | Refactor-roadmap §9.4 closure |
> | TICKET-EXP2-G3 | Path-A scalar parser delegation to Path B `compile()` (Gate 3 expressions/v2 promotion) | 🔵 Planned | `EXPRESSIONS_V2_PROMOTION.md` Gate 3 |

> The previously-listed TICKET-003 (`<chrono3d/...>` typo in `lexer.hpp`) and
> TICKET-004 (`PUBLIC ${CMAKE_SOURCE_DIR}` include bug on
> `chronon3d_expressions_v2`) are **🟢 Done** and no longer block the
> Experimental-Zone promotion path. The remaining blocker is TICKET-EXP2-G3
> (Gate 3 Path B migration).
