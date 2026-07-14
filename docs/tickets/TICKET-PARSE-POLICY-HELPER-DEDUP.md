# TICKET-PARSE-POLICY-HELPER-DEDUP — Extract `parse_framebuffer_pool_clear_policy(name)` helper

> Stato: **OPEN (2026-07-14)** — forward-point from [TICKET-ARCH-CLEANUP-V0](docs/tickets/TICKET-ARCH-CLEANUP-V0.md) §Forward-points (P3 cosmetic). Cat-3 minimal-surface.

## Problema

The `FramebufferPoolClearPolicy` enum string parsing is duplicated in 3 places:

1. **`src/core/config.cpp`** — env var resolution: parses `CHRONON3D_FB_POOL_CLEAR_POLICY` value (one of `keep-warm | trim-after-job | trim-on-memory-pressure`)
2. **`apps/chronon3d_cli/utils/job/render_job.cpp`** — CLI flag resolution: parses `--fb-pool-clear-policy` value
3. **`apps/chronon3d_cli/commands/render/register_render_commands.cpp`** — CLI flag description string (the canonical list of accepted values)

This is a Cat-3 anti-duplication violation: 3 places must be kept in sync if a new policy is added (e.g., a future `TrimOnIdle` policy), and a typo in any of the 3 places would silently fall through to the default policy.

## Soluzione Confine

Extract a single canonical helper:

```cpp
// include/chronon3d/cache/framebuffer_pool.hpp (or new tools/ header)
namespace chronon3d::cache {
    [[nodiscard]] std::optional<FramebufferPoolClearPolicy>
    parse_framebuffer_pool_clear_policy(std::string_view name);

    [[nodiscard]] std::string_view
    framebuffer_pool_clear_policy_name(FramebufferPoolClearPolicy p);  // canonical round-trip
}
```

Then:
- `src/core/config.cpp` env var resolution calls `parse_framebuffer_pool_clear_policy(getenv(...))`
- `apps/chronon3d_cli/utils/job/render_job.cpp` CLI flag resolution calls the same helper
- `register_render_commands.cpp` CLI description string is built from the helper's accepted-values list (or a static constexpr array of `policy_name()` results)

The `name()` round-trip function ensures the enum → string conversion is single-source too.

## Criteri di accettazione

- [ ] `parse_framebuffer_pool_clear_policy("keep-warm")` returns `FramebufferPoolClearPolicy::KeepWarm`
- [ ] `parse_framebuffer_pool_clear_policy("trim-after-job")` returns `FramebufferPoolClearPolicy::TrimAfterJob`
- [ ] `parse_framebuffer_pool_clear_policy("trim-on-memory-pressure")` returns `FramebufferPoolClearPolicy::TrimOnMemoryPressure`
- [ ] `parse_framebuffer_pool_clear_policy("invalid")` returns `std::nullopt` (and the caller falls back to the default policy)
- [ ] `framebuffer_pool_clear_policy_name(FramebufferPoolClearPolicy::TrimAfterJob) == "trim-after-job"` (round-trip)
- [ ] 0 new public SDK symbol beyond the 2 functions (Cat-3 minimal-surface)
- [ ] 0 new singleton/registry/resolver/cache (per AGENTS.md deny-everywhere)
- [ ] 0 `#include <msdfgen>/<libtess2>/<unicode[/...]>` (Gate 5 Check 11 deny-everywhere preserved)
- [ ] All 3 prior call sites migrated to the new helper
- [ ] Test coverage: 1 TEST_CASE per accepted value + 1 for invalid input + 1 for round-trip

## macchina-verifica

DEFERRED-WBH per `TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV` vcpkg glm/magic_enum env-block pattern + `TICKET-BUILD-ROT-CASCADE-CAMERA` 409-error rot. VPS-only verification: `rg -n 'parse_framebuffer_pool_clear_policy|trim-after-job|keep-warm|trim-on-memory-pressure' src/ apps/` to confirm 3 call sites + 0 pre-existing helper.

## Cat-3 minimal-surface

- 2 NEW functions in canonical header (`include/chronon3d/cache/framebuffer_pool.hpp` or new tools/ header)
- 3 EDIT call sites: `src/core/config.cpp` + `apps/chronon3d_cli/utils/job/render_job.cpp` + `apps/chronon3d_cli/commands/render/register_render_commands.cpp`
- 1 NEW test file: `tests/cache/test_parse_framebuffer_pool_clear_policy.cpp` (~5 TEST_CASEs)

NO new public SDK symbol beyond the 2 helpers (both `[[nodiscard]]` pure functions over the existing enum). NO new singleton/registry/resolver/cache. NO deny-everywhere include.

## Cross-link

- **Parent ticket**: [TICKET-ARCH-CLEANUP-V0](docs/tickets/TICKET-ARCH-CLEANUP-V0.md) §Forward-points (5th item)
- **Related chore**: [TICKET-FB-POOL-CLEAR-POLICY-CALL-SITE](docs/tickets/TICKET-FB-POOL-CLEAR-POLICY-CALL-SITE.md) (companion forward-point: wire the policy into the job executor)
- **Cat-3 anti-dup discipline**: 1 helper + 1 round-trip + 3 call sites + 1 test = 5 file touches total
- **Sibling forward-points from TICKET-ARCH-CLEANUP-V0**:
  - [TICKET-RENDER-SERVICES-FULL-ELIMINATION](docs/tickets/TICKET-RENDER-SERVICES-FULL-ELIMINATION.md)
  - [TICKET-RENDER-RUNTIME-MIGRATION-FOR-TESTS](docs/tickets/TICKET-RENDER-RUNTIME-MIGRATION-FOR-TESTS.md)
  - [TICKET-FB-POOL-CLEAR-POLICY-CALL-SITE](docs/tickets/TICKET-FB-POOL-CLEAR-POLICY-CALL-SITE.md)
  - [TICKET-LEGACY-QUEUE-METHODS-FOLLOWUP](docs/tickets/TICKET-LEGACY-QUEUE-METHODS-FOLLOWUP.md)
- **Cat-5 3-doc chaser-chore pattern**: this ticket is opened via the same pattern (1 NEW ticket + 1 CHANGELOG entry + 1 §Open Blockers row)
