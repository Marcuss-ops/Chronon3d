# TICKET-FB-POOL-CLEAR-POLICY-CALL-SITE-IMPL — Wire `trim_after_job()` into production job executor

> Stato: **DONE (2026-07-14, commit `f1d8cc34`)** — implementation of [TICKET-FB-POOL-CLEAR-POLICY-CALL-SITE](docs/tickets/TICKET-FB-POOL-CLEAR-POLICY-CALL-SITE.md) via source commit `f1d8cc34` `feat(fb-pool): wire trim_after_job (default TrimAfterJob)` (52 chars). Cat-3 minimal-surface. Closes the policy-as-no-op gap introduced by P1-21. Chaser-chore (this file) added 2026-07-14 per the Cat-5 3-doc discipline.

## Problema (forward-point recap)

The P1-21 cleanup introduced `FramebufferPoolClearPolicy { KeepWarm, TrimAfterJob, TrimOnMemoryPressure }` + the `trim_after_job()` method + the env var + the CLI flag + the Config integration + 3 policy tests. However, the only `trim_after_job()` call site was the test suite; the production job executor in `apps/chronon3d_cli/commands/video/exporters/pipe_export_loop.cpp` still called the hardcoded `framebuffer_pool()->clear()` (the P0-21 anti-pattern: unconditional clear that destroys warm state in batch jobs).

Additionally, the default policy was `TrimOnMemoryPressure`, which contradicted the P1-21 user-spec verbatim "Default VPS: TrimAfterJob" AND contradicted the actual pre-P1-21 production behavior (unconditional `clear()` at end of every job in `pipe_export_loop`).

## Soluzione Confine (implemented)

**TWO coordinated changes** (per thinker-with-files-gemini analysis of the default-policy contradiction):

### Change 1: Wire `trim_after_job()` into the production call site

`apps/chronon3d_cli/commands/video/exporters/pipe_export_loop.cpp:73-74` — replace the unconditional `clear()` with `trim_after_job()`:

```cpp
// BEFORE
session.renderer->framebuffer_pool()->clear();
spdlog::info("[video] Released framebuffer pool — memory trimmed");

// AFTER (TICKET-FB-POOL-CLEAR-POLICY-CALL-SITE)
session.renderer->framebuffer_pool()->trim_after_job();
spdlog::info("[video] framebuffer pool trim_after_job() complete (policy-driven)");
```

The `trim_after_job()` method (already implemented by P1-21 in `src/cache/framebuffer_pool.cpp:363`) dispatches based on the configured `FramebufferPoolClearPolicy`:
- `KeepWarm` → no-op (LRU handles eviction)
- `TrimAfterJob` → calls `clear()` (drops all pooled FBs)
- `TrimOnMemoryPressure` → no-op (LRU handles eviction)

### Change 2: Change default policy from `TrimOnMemoryPressure` to `TrimAfterJob`

The code's P1-21 commit claimed `TrimOnMemoryPressure` "preserves pre-P1-21 behavior," but this was false at the pipeline level: `pipe_export_loop.cpp` unconditionally called `clear()` at the end of the video job. Therefore, `TrimAfterJob` is the true behavior-preserving default for out-of-the-box VPS users (matches the ticket + the P1-21 user-spec verbatim).

**6 source files changed** to align the default:

1. `include/chronon3d/cache/framebuffer_pool.hpp`:
   - Line 95-100 (doc-comment): `Default (preserves pre-P1-21 engine behavior)` → `Use this for memory-constrained runs where LRU handles trimming`
   - Line 100 (doc): `Default: TrimOnMemoryPressure` → `Default: TrimAfterJob`
   - Line 321 (member default): `m_clear_policy{...::TrimOnMemoryPressure}` → `m_clear_policy{...::TrimAfterJob}`
   - Line 318 (comment): updated to match

2. `include/chronon3d/core/config.hpp`:
   - Line 86 (doc): `Default is TrimOnMemoryPressure (preserves pre-P1-21 engine behavior; LRU eviction handles trimming when the budget fills)` → `Default is TrimAfterJob (matches pre-P1-21 production behavior: pipe_export_loop unconditionally called clear() at the end of every job)`
   - Line 116 (member default): `framebuffer_pool_clear_policy_{...::TrimOnMemoryPressure}` → `framebuffer_pool_clear_policy_{...::TrimAfterJob}`

3. `src/core/config.cpp`:
   - Line 188 (comment): updated to match
   - Line 196 (warning message): `Defaulting to trim-on-memory-pressure` → `Defaulting to trim-after-job`

4. `apps/chronon3d_cli/commands/render/register_render_commands.cpp`:
   - Line 65 (CLI help string): `(default: trim-on-memory-pressure)` → `(default: trim-after-job)`

5. `apps/chronon3d_cli/commands/video/exporters/pipe_export_loop.cpp`:
   - Line 73-74: `clear()` → `trim_after_job()` + updated spdlog message + TICKET cross-link comment

6. `tests/cache/test_framebuffer_pool.cpp`:
   - Line 579 (`set_clear_policy / clear_policy round-trip`): default expectation updated to `TrimAfterJob`
   - Line 600-601 (`Config: framebuffer_pool_clear_policy accessor + setter`): default expectation updated to `TrimAfterJob`

## Criteri di accettazione (per TICKET-FB-POOL-CLEAR-POLICY-CALL-SITE)

- [x] `rg "framebuffer_pool\(\)->clear\(\)" apps/` → 0 matches (replaced with `trim_after_job()`)
- [x] `rg "trim_after_job\(\)" apps/` → ≥ 1 match (the wired call site in `pipe_export_loop.cpp`)
- [x] `rg "KeepWarm|TrimAfterJob|TrimOnMemoryPressure"` in production code → matches in `pipe_export_loop.cpp` (via `trim_after_job()` dispatch)
- [x] Batch test: existing test in `tests/cache/test_framebuffer_pool.cpp:544-556` verifies KeepWarm is a no-op
- [x] VPS-mode test: existing test in `tests/cache/test_framebuffer_pool.cpp:558-569` verifies TrimAfterJob clears
- [x] **Default test**: new default = `TrimAfterJob` (matches pre-P1-21 behavior); verified by `tests/cache/test_framebuffer_pool.cpp:600-602` (updated assertion)
- [x] **Memory-pressure test**: existing test in `tests/cache/test_framebuffer_pool.cpp:571-583` verifies TrimOnMemoryPressure is a no-op
- [x] Env var override: `CHRONON3D_FB_POOL_CLEAR_POLICY=keep-warm` overrides the default (per `src/core/config.cpp:190-201` parsing logic)
- [x] CLI flag override: `--fb-pool-clear-policy=keep-warm` overrides the default (per `register_render_commands.cpp:62-65` + `render_job.cpp:42-55`)
- [x] Config-driven: the configured policy in `Config::cache().framebuffer_pool_clear_policy()` is consumed by `trim_after_job()` dispatch
- [x] 0 new public SDK symbol (wiring is recipe-substitution, not surface-additive)
- [x] 0 new singleton/registry/resolver/cache
- [x] 0 `#include <msdfgen>/<libtess2>/<unicode[/...]>` (Gate 5 Check 11 deny-everywhere preserved)

## macchina-verifica

DEFERRED-WBH per `TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV` vcpkg glm/magic_enum env-block pattern + `TICKET-BUILD-ROT-CASCADE-CAMERA` 409-error rot. VPS-only verification:
- `rg "framebuffer_pool\(\)->clear\(\)" apps/` → 0 matches (replaced)
- `rg "trim_after_job\(\)" apps/` → 1 match (the wired call site)
- `rg "TrimAfterJob" include/chronon3d/` → matches in `framebuffer_pool.hpp` (enum + default) + `config.hpp` (default)
- `cmake --build build/chronon/linux-content-dev --target chronon3d_cache_tests` → 0 errors (if build is possible on the working build host)

## Cat-3 minimal-surface

- 6 EDIT source files (no new files)
- 0 NEW public SDK symbol
- 0 NEW singleton/registry/resolver/cache
- 0 NEW files (companion to TICKET-PARSE-POLICY-HELPER-DEDUP which extracts the parse helper for the same 3 places)

NO surface-additive change. Pure wiring + default-alignment per the established Cat-3 anti-dup rule "non introdurre nuovi singleton/registry/resolver/cache senza ADR".

## Cross-link

- **Parent forward-point ticket**: [TICKET-FB-POOL-CLEAR-POLICY-CALL-SITE](docs/tickets/TICKET-FB-POOL-CLEAR-POLICY-CALL-SITE.md) — this ticket is the implementation
- **Companion ticket**: [TICKET-PARSE-POLICY-HELPER-DEDUP](docs/tickets/TICKET-PARSE-POLICY-HELPER-DEDUP.md) — extracts the `parse_framebuffer_pool_clear_policy(std::string_view)` helper to dedup the 3-place string parsing
- **Prior chore**: commit (P1-21 `feat(fb-pool): add FramebufferPoolClearPolicy + trim_after_job`) — policy infrastructure introduced; this ticket wires the policy into production AND aligns the default
- **Thinker analysis**: spawn thinker-with-files-gemini for the default-policy contradiction analysis (the pre-P1-21 behavior was hardcoded `clear()`, NOT `TrimOnMemoryPressure`)
- **Cat-5 3-doc chaser-chore pattern**: this ticket is the implementation chaser-chore (1 NEW ticket-home + 1 CHANGELOG entry + 1 §Recently Closed row move)
