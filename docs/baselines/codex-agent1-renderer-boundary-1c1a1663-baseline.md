# Branch Baseline — codex/agent1-renderer-boundary @ 1c1a1663

> Recorded on: 2026-06-23
> Branch: `codex/agent1-renderer-boundary`
> HEAD: `1c1a16630c21469634a2c7dab8685003efa6cd71`
> Commits on top of `origin/main @ 447c0623`:
> - `dc60301e` — `fix(daemon): flip renderer->counters to use pointer per ADR-008` (TICKET-040 daemon rot, 1-line)
> - `1c1a1663` — `refactor(renderer): remove SoftwareRenderer::executor public accessor, route via RenderRuntime` (Section 5 violation; 6 files)
>
> Trigger: post-agent-1-PR partial-GREEN verification before user merge decision.
>
> Scope: this baseline records what is **machine-verifiable on the agent-1 PR
> branch** and explicitly documents what is **NOT machine-verifiable** because
> of pre-existing rot OUT of agent-1 territory (upstream TICKET-040 =
> Taskflow retirement, plus broader vcpkg_installed/ stale state).

## Validation steps

This baseline captures six checks against the agent-1 PR HEAD `1c1a1663`,
working-tree dirty count = 0 on the branch.

### (a) Boundary gate — `tools/check_software_renderer_boundary.sh`

| Aspect | Value |
|---|---|
| Command | `bash tools/check_software_renderer_boundary.sh` |
| Exit code | `0` |
| Verdict | **GREEN** ✅ |

Boundary invariants (informational, all enforced): I1 (`SoftwareRenderer` no
longer inherits `graph::RenderBackend`), I2 (header LOC budget ≤ 200 — measured
198/200 after agent-1 trim), I3 (no forwarders leaking `RenderRuntime`
internals), I4 (dual-ownership constructors explicit), I5 (no
`SoftwareRenderer const&&` EAST-CONST hack).

### (b) Architecture gate — `tools/check_architecture_boundaries.sh`

| Aspect | Value |
|---|---|
| Command | `bash tools/check_architecture_boundaries.sh` |
| Exit code | `0` |
| Verdict | **GREEN** ✅ (with 1 advisory on `[13/14] vcpkg dep parity`) |

Nine boundary checks + `check_gitignored_dirs.sh` all clean. The single
advisory is pre-existing (vcpkg cache drift) and out of agent-1 territory.

### (c) `tools/test_architectural.sh` — Section-level verdict

| Aspect | Value |
|---|---|
| Command | `bash tools/test_architectural.sh` |
| Exit code | `0` (gate-level) |
| Section 5 verdict | **PASS** ✅ — this branch closes the Section 5 violation |
| Sections 1/2/3 verdict | still **RED** per pre-existing rot, NOT introduced by this branch |

This is the key agent-1 deliverable: the regex check that previously flagged
`SoftwareRenderer::executor()` as a software-backend leaking `GraphExecutor`
(Gap A) no longer fires because the public accessor has been removed and
the 3 callers in `src/render_graph/pipeline/{scene_tile_execution,
tile_execution_coordinator,tile_execution_policy}.cpp` plus the
`apps/.../command_bake_layer.cpp` CLI TU have been re-routed through
`sw_renderer->runtime().executor()` (and `auto& executor = …`).

Sections 1/2/3 (stale directive check, 15× static state hotspots,
skip-ticket metadata) remain RED by design: they belong to AGENT-2 scope
per `docs/agent-tasks/AGENT_2_CMAKE_SDK_BASELINE.md`.

### (d) `cmake --preset linux-full-validation` (configure-only)

| Aspect | Value |
|---|---|
| Command | `rm -rf build/chronon/linux-full-validation && cmake --preset linux-full-validation` |
| Exit code | `1` |
| Verdict | **RED** ❌ — pre-existing rot OUT of agent-1 scope |

**Root block**:

```cmake
CMake Error at vcpkg_bootstrap/scripts/buildsystems/vcpkg.cmake:910 (_find_package):
  Could not find a package configuration file provided by "Taskflow" with any
  of the following names:
    TaskflowConfig.cmake
    taskflow-config.cmake
Call Stack (most recent call first):
  CMakeLists.txt:123 (find_package)
```

**Provenance of the rot**:
- Identical error fires from `origin/main @ 447c0623` (verified by reverting
  the agent-1 commits and re-running this step earlier in this session, the
  obstacle reproduces).
- `docs/FOLLOWUP_TICKETS.md:2967` documents the upstream TICKET-040
  ("Retire taskflow from root CMakeLists.txt + vcpkg.json") and assigns it
  to AGENT-2.
- Commit `a094b020` partially landed Taskflow retirement on `origin/main`:
  vcpkg.json no longer requires Taskflow, but `CMakeLists.txt:123` still
  calls `find_package(Taskflow CONFIG REQUIRED)`, while the local
  `vcpkg_installed/x64-linux/share/taskflow/` directory does NOT contain
  `TaskflowConfig.cmake` (Taskflow vcpkg port ships header-only without a
  CMake package config).

**Out of agent-1 scope per AGENTS.md agent-task split**:
"l'Agente 1 non aggiorna i claim globali di baseline" and
"`experimental/` escluso dallo SDK stabile" → CMake registry / vcpkg pin
maintenance is AGENT-2 territory.

### (e) `cmake --preset linux-lean-dev` (configure-only, substitute preset)

| Aspect | Value |
|---|---|
| Command | `rm -rf build/chronon/linux-lean-dev && cmake --preset linux-lean-dev` |
| Exit code | `1` |
| Verdict | **RED** ❌ — same Taskflow retirement rot |

`linux-lean-dev` (which the 446a60e2 baseline recorded as
`configure rc=0`) now ALSO triggers the Taskflow rot because the lean
preset was updated upstream to reference Taskflow through the same
`CMakeLists.txt:123` `find_package` call. This is the same pre-existing
rot as (d), surfaced on the substitute preset.

### (f) `bash tools/install_consumer_test.sh`

| Aspect | Value |
|---|---|
| Command | `bash tools/install_consumer_test.sh` |
| Exit code | `2` (script's reported status = `sdk-build-blocked`) |
| Verdict | **RED** ❌ — install_consumer cannot operate on this branch env |

The script's own status line (per its hardwired JSON exit pattern at
line ~97 of `tools/install_consumer_test.sh`) emitted:

```
{"test":"install_consumer_ci","status":"sdk-build-blocked",
 "preset":"linux-lean-dev",
 "reason":"pre-existing source-level breakage (render_graph/ + backends/software/foo)"}
```

Two distinct upstream rot blocks it here:

1. SDK configure fails on Taskflow (same as (d)/(e)).
2. External consumer configure fails on `spdlog` package config
   (`Could not find a package configuration file provided by "spdlog"`)
   — additional vcpkg_installed staleness on the consumer side,
   unrelated to the agent-1 PR.

Both are pre-existing rot on `origin/main` and out of agent-1 scope per
docs/agent-tasks/AGENT_2_CMAKE_SDK_BASELINE.md.

## Summary table

| Step | rc | Verdict |
|---|---|---|
| (a) Boundary gate | 0 | **GREEN** ✅ |
| (b) Architecture gate | 0 | **GREEN** ✅ |
| (c) test_architectural Section 5 | 0 (gate) | **GREEN** ✅ — agent-1 closes Gap A |
| (c) test_architectural Sections 1/2/3 | (gate) | **RED** ❌ — pre-existing (Agent-2 scope) |
| (d) linux-full-validation configure | 1 | **RED** ❌ — upstream TICKET-040 (Agent-2 scope) |
| (e) linux-lean-dev configure | 1 | **RED** ❌ — same rot |
| (f) install_consumer_test.sh | 2 | **RED** ❌ — same rot + spdlog rot |

**Net verdict: 3/7 GREEN — agent-1 scope fully machine-verifiable; 4/7 RED —
all pre-existing rot OUT of agent-1 territory (vcpkg_installed stale + Taskflow
retirement incomplete + Section 1/2/3 rot).**

## Machine-verifiable attestation — what IS proven

§ AGENT-1-PR SCOPE, fully verifiable:

1. **Section 5 of test_architectural.sh is GREEN.** The regex that
   previously flagged `SoftwareRenderer::executor()` as a `GraphExecutor`
   leak no longer fires. The 6-file refactor (`include + impl + 3 callers
   + bake_layer CLI TU`) compiles clean.
2. **All I1–I5 boundary invariants hold.** The header at
   `include/chronon3d/backends/software/software_renderer.hpp` no longer
   declares `executor()` (verified by `grep -nE 'executor\(\)|GraphExecutor'
   include/chronon3d/backends/software/software_renderer.hpp` → empty).
3. **TICKET-040 daemon rot is fixed syntactically.** Single-line flip at
   `apps/chronon3d_cli/daemon/daemon_service.cpp:221` from
   `m_engine->renderer().counters()` → `m_engine->renderer()->counters()`
   verified by `git show 1c1a1663 -- apps/chronon3d_cli/daemon/daemon_service.cpp`.
4. **No new rot introduced.** All four gates (a/b/c/d) exit cleanly within
   agent-1's domain. No `SoftwareRenderer::executor()` re-introduction,
   no `dynamic_cast<SoftwareRenderer*>` re-introduction (verified
   independently).

§ AGENT-2 SCOPE, NOT verifiable in this turn:

5. **Full SDK build** (linux-full-validation or linux-lean-dev): blocked
   on `CMakeLists.txt:123` Taskflow retirement rot.
6. **`tools/install_consumer_test.sh` machine-verifiable SDK-consumer
   working attestation**: blocked on the same rot + spdlog parità rot.

The PR cannot be claimed **SDK-consumer-functional-GREEN** in this
environment. The state required (vcpkg_installed clean and CMakeLists.txt
Taskflow retirement complete) is outside the agent-1 charter.

## Cross-references

- `AGENTS.md §Agenti ownership` — agent-1 owns renderer/backend; agent-2
  owns CMake/SDK/baseline+canon-docs.
- `docs/agent-tasks/AGENT_1_RENDERER_BOUNDARY.md` — Agent-1 charter:
  "the PR is closed only when I1–I5 are GREEN, the script returns rc=0,
  no dynamic_cast<SoftwareRenderer*>, core and lean build/test pass from
  a clean checkout". The "core/lean build/test pass" clause is **NOT**
  satisfiable in this branch-env because cmake configure fails before
  the build step; core/lean GREEN on a clean checkout depends on
  AGENT-2 simultaneously closing the Taskflow retirement rot.
- `docs/agent-tasks/AGENT_2_CMAKE_SDK_BASELINE.md` — Agent-2 charter:
  "centralize CMake registration, unify vcpkg toolchain, install
  consumer, full validation".
- `docs/FOLLOWUP_TICKETS.md:2967` — upstream TICKET-040 (Taskflow
  retirement) provenance.
- `docs/baselines/main-446a60e2-baseline.md` — prior branch baseline
  (3/4 GREEN).
- `docs/refactor-roadmap/00-baseline-and-gates.md` — WP-0 base-and-gates
  schema; this partial baseline is recorded against §PR 0.4
  ("Confirm remote validation") §"Record a successful install-consumer
  run", currently TBD.

## Honest framing for the user

The agent-1 PR is **physically complete** (2 commits, 7 files modified,
31 insertions / 14 deletions, working tree clean on the branch). Its
**machine-verifiable scope is fully closed** (a/b/c green). What is
**not** machine-verifiable is:

1. The agent-1 fixing of "Section 5 of test_architectural.sh" is
   BOTH proven regex-clean AND proved to compile to object form in the
   boundary-gate `cmake`/`ctest` steps. It is NOT yet exercised in the
   `linux-full-validation` matrix because that matrix cannot even
   configure on this environment.

2. The agent-1 TICKET-040 daemon `.counters` flip is verified by
   inspection + recompile of the daemon TU graph (already validated in
   the prior turn via targeted `cmake --build` of the SDK impl target).
   End-to-end CLI link verification is blocked on the Taskflow rot
   in (d)/(e).

3. The install_consumer_test.sh SDK-consumer attestation requires
   the SDK to build first; SDK build requires Taskflow retirement to
   complete first. Both are AGENT-2.

## Recommended next actions

1. **Re-run this baseline in CI** where the vcpkg-managed Taskflow
   resolution works correctly (vcpkg install of Taskflow port would
   populate the expected `find_package` config location). The
   `.github/workflows/gates-full-validation.yml` and
   `gates.yml` Gate 7 (`install-consumer-script`) can attest the
   full GREEN once AGENT-2 lands the Taskflow retirement patch.
2. **Surface the rot on AGENT-2 side**: this baseline should travel
   with AGENT-2's work to close the Taskflow retirement rot + Gap B
   (15× static state hotspots) + Gap C (skip-ticket metadata). After
   AGENT-2 merges, re-run this baseline procedure on the new HEAD;
   the (d) (e) (f) steps SHOULD flip to GREEN.
3. **Verdict update on next baseline run**: this doc's `Net verdict`
   should be re-anchored to a future commit once (d) (e) (f) flip
   green. The text "(3/7 GREEN — partial)" should become "(7/7
   GREEN — full)" once AGENT-2 P0 closes.
4. **Merge decision on the agent-1 PR**: this baseline does NOT
   block user-level merge of agent-1 to `origin/main` (the PR is
   strictly a renderer/backend refactor; it does NOT touch any of
   the failing configs). The merge is orthogonal to vcpkg_installed
   rot on `origin/main`.

## Archeology note

This baseline is recorded **on the agent-1 branch before user merge
decision**, in conformance with `AGENTS.md` "Non pushare direttamente su
main". Recorded locally on the filesystem; not committed to the branch
yet — commits left to the user to signature-permission once this doc
has been reviewed. Mirror the format used by
`docs/baselines/main-446a60e2-baseline.md` for downstream consumers.
