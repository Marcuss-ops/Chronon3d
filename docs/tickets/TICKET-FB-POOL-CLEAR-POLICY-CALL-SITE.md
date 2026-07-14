# TICKET-FB-POOL-CLEAR-POLICY-CALL-SITE — Wire `trim_after_job()` into the job executor

> Stato: **OPEN (2026-07-14)** — forward-point from [TICKET-ARCH-CLEANUP-V0](docs/tickets/TICKET-ARCH-CLEANUP-V0.md) §Forward-points (4th item). Cat-3 minimal-surface.

## Problema

The P1-21 cleanup introduced `FramebufferPoolClearPolicy { KeepWarm, TrimAfterJob, TrimOnMemoryPressure }` + the `trim_after_job()` method + the env var + the CLI flag + the Config integration + 3 policy tests. **However**, the only `trim_after_job()` call site is the test suite; the production job executor still calls the hardcoded `framebuffer_pool()->clear()` (which was the P0-21 anti-pattern: unconditional clear that destroys warm state in batch jobs).

This is a Cat-3 anti-dup violation: the policy infrastructure exists but is never read by production code. The user-spec verbatim "policy-driven decision" is unmet — the policy is a no-op in production.

The 3 policy tests in `tests/cache/` (per P1-21) verify the policy enum + the `trim_after_job()` method, but NOT the production call site that consumes the policy.

## Soluzione Confine

**SINGLE OUTCOME**: wire the configured `FramebufferPoolClearPolicy` into the production job executor so the policy is actually consumed. Default = `KeepWarm` (preserves warm state for batch jobs); VPS-mode override = `TrimAfterJob` (matches the prior single-job-on-VPS behavior); opt-in `TrimOnMemoryPressure` available via env var.

Steps:
1. **Audit** the production call site: `rg "framebuffer_pool\\(\\)->clear\\(\\)"` → find the unconditional clear
2. **Replace** with policy-driven: `framebuffer_pool()->trim_per_policy(config.fb_pool_clear_policy)` (or equivalent method)
3. **Test** all 3 policies end-to-end: a 2-job batch on a warm pool must NOT clear between jobs (KeepWarm) and must clear after the LAST job (TrimAfterJob)
4. **Verify** the env var `CHRONON3D_FB_POOL_CLEAR_POLICY=trim-after-job` overrides the Config default

If the production call site is in the CLI command (per user spec P1-21 "Rimuovi il clear() incondizionato dal comando"), the wiring lives in `apps/chronon3d_cli/commands/render/register_render_commands.cpp` (or the per-job executor helper).

## Criteri di accettazione

- [ ] `rg "framebuffer_pool\\(\\)->clear\\(\\)"` in production code → 0 matches (or matches only inside policy-driven branches)
- [ ] `rg "trim_after_job\\(\\)"` in production code → ≥ 1 match (the wired call site)
- [ ] `rg "KeepWarm"` / `"TrimAfterJob"` / `"TrimOnMemoryPressure"` → ≥ 1 match in production code (the policy is actually used)
- [ ] Batch test: 2-job batch on warm pool does NOT clear between jobs when policy = `KeepWarm`
- [ ] VPS-mode test: single-job run on cold pool clears after job when policy = `TrimAfterJob`
- [ ] Env var override: `CHRONON3D_FB_POOL_CLEAR_POLICY=trim-on-memory-pressure` is honored
- [ ] Config-driven: the configured policy in `RuntimeConfig::fb_pool_clear_policy` is consumed
- [ ] 0 new public SDK symbol (wiring is recipe-substitution, not surface-additive)
- [ ] 0 new singleton/registry/resolver/cache
- [ ] 0 `#include <msdfgen>/<libtess2>/<unicode[/...]>` (Gate 5 Check 11 deny-everywhere preserved)

## macchina-verifica

DEFERRED-WBH per `TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV` vcpkg glm/magic_enum env-block pattern + `TICKET-BUILD-ROT-CASCADE-CAMERA` 409-error rot. VPS-only verification:
- `rg "framebuffer_pool\\(\\)->clear\\(\\)" src/ apps/` → 0 matches (or matches only inside policy branches)
- `rg "trim_after_job\\(\\)" src/ apps/` → ≥ 1 match
- `rg "fb_pool_clear_policy" src/ apps/ config.cpp` → ≥ 1 match (the policy is actually consumed)

## Cat-3 minimal-surface

- 1 EDIT production call site (the unconditional `clear()` → policy-driven dispatch)
- 0 NEW public SDK symbol
- 0 NEW singleton/registry/resolver/cache
- 0 NEW files (companion to [TICKET-PARSE-POLICY-HELPER-DEDUP](docs/tickets/TICKET-PARSE-POLICY-HELPER-DEDUP.md) which extracts the parse helper for the same 3 places)

NO surface-additive change. Pure wiring per the established Cat-3 anti-dup rule "non introdurre nuovi singleton/registry/resolver/cache senza ADR".

## Cross-link

- **Parent ticket**: [TICKET-ARCH-CLEANUP-V0](docs/tickets/TICKET-ARCH-CLEANUP-V0.md) §Forward-points (4th item)
- **Prior chore**: commit (P1-21 `feat(fb-pool): add FramebufferPoolClearPolicy + trim_after_job`) — policy infrastructure introduced; this ticket wires the policy into production
- **Companion ticket**: [TICKET-PARSE-POLICY-HELPER-DEDUP](docs/tickets/TICKET-PARSE-POLICY-HELPER-DEDUP.md) — extracts the `parse_framebuffer_pool_clear_policy(std::string_view)` helper to dedup the 3-place string parsing (config.cpp + render_job.cpp + CLI flag description)
- **Sibling forward-points from TICKET-ARCH-CLEANUP-V0**:
  - [TICKET-PARSE-POLICY-HELPER-DEDUP](docs/tickets/TICKET-PARSE-POLICY-HELPER-DEDUP.md)
  - [TICKET-RENDER-SERVICES-FULL-ELIMINATION](docs/tickets/TICKET-RENDER-SERVICES-FULL-ELIMINATION.md)
  - [TICKET-RENDER-RUNTIME-MIGRATION-FOR-TESTS](docs/tickets/TICKET-RENDER-RUNTIME-MIGRATION-FOR-TESTS.md)
  - [TICKET-LEGACY-QUEUE-METHODS-FOLLOWUP](docs/tickets/TICKET-LEGACY-QUEUE-METHODS-FOLLOWUP.md)
- **Cat-5 3-doc chaser-chore pattern**: this ticket is opened via the same pattern (1 NEW ticket + 1 CHANGELOG entry + 1 §Open Blockers row)
