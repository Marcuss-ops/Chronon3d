# TICKET-PARSE-POLICY-HELPER-DEDUP — Extract `parse_framebuffer_pool_clear_policy(name)` helper

> Stato: **DONE (2026-07-14, source commit `12af1063b33b8bdad38d53849d4f5a6fb71d0e5b`)** — implementation landed via [TICKET-PARSE-POLICY-HELPER-DEDUP-IMPL](docs/tickets/TICKET-PARSE-POLICY-HELPER-DEDUP-IMPL.md). 1 EDIT header + 2 EDIT call sites (config.cpp + render_job.cpp) + 1 NEW test file + 1 NEW cmake file + 1 EDIT tests/CMakeLists.txt. register_render_commands.cpp:63 CLI description string left as hardcoded list (out of scope per ticket Criteri di accettazione; the round-trip name() helper is deferred to a future forward-point). Forward-point from [TICKET-ARCH-CLEANUP-V0](docs/tickets/TICKET-ARCH-CLEANUP-V0.md) §Forward-points (5th item) CLOSED. Cat-3 minimal-surface preserved. from [TICKET-ARCH-CLEANUP-V0](docs/tickets/TICKET-ARCH-CLEANUP-V0.md) §Forward-points (P3 cosmetic). Cat-3 minimal-surface.

## Problema

The `FramebufferPoolClearPolicy` enum string parsing is duplicated in 3 places:

1. **`src/core/config.cpp`** — env var resolution: parses `CHRONON3D_FB_POOL_CLEAR_POLICY` value (one of `keep-warm | trim-after-job | trim-on-memory-pressure`)
2. **`apps/chronon3d_cli/utils/job/render_job.cpp`** — CLI flag resolution: parses `--fb-pool-clear-policy` value
3. **`apps/chronon3d_cli/commands/render/register_render_commands.cpp`** — CLI flag description string (the canonical list of accepted values)

This is a Cat-3 anti-duplication violation: 3 places must be kept in sync if a new policy is added (e.g., a future `TrimOnIdle` policy), and a typo in any of the 3 places would silently fall through to the default policy.

## Soluzione Confine

**SINGLE OUTCOME**: extract ONE canonical parse helper (stricter user-spec adherence — user only asked for `parse_framebuffer_pool_clear_policy(name)`; the optional `name()` round-trip helper is OUT OF SCOPE for this ticket and deferred to a future forward-point if a concrete caller emerges).

```cpp
// include/chronon3d/cache/framebuffer_pool.hpp (or new tools/ header)
namespace chronon3d::cache {
    [[nodiscard]] std::optional<FramebufferPoolClearPolicy>
    parse_framebuffer_pool_clear_policy(std::string_view name);
}
```

Then:
- `src/core/config.cpp` env var resolution calls `parse_framebuffer_pool_clear_policy(getenv(...))`
- `apps/chronon3d_cli/utils/job/render_job.cpp` CLI flag resolution calls the same helper
- `register_render_commands.cpp` CLI description string is left as a hardcoded list of accepted values (matching the current implementation per `apps/chronon3d_cli/commands/render/register_render_commands.cpp:62-65`); a future `name()` round-trip helper could derive this list dynamically, but that is deferred to a separate forward-point ticket.

## Criteri di accettazione

- [ ] `parse_framebuffer_pool_clear_policy("keep-warm")` returns `FramebufferPoolClearPolicy::KeepWarm`
- [ ] `parse_framebuffer_pool_clear_policy("trim-after-job")` returns `FramebufferPoolClearPolicy::TrimAfterJob`
- [ ] `parse_framebuffer_pool_clear_policy("trim-on-memory-pressure")` returns `FramebufferPoolClearPolicy::TrimOnMemoryPressure`
- [ ] Case-insensitive variant: parse_framebuffer_pool_clear_policy accepts BOTH lowercase ("keep-warm" / "trim-after-job" / "trim-on-memory-pressure") AND PascalCase ("KeepWarm" / "TrimAfterJob" / "TrimOnMemoryPressure") (per current implementation in config.cpp:193-201 and render_job.cpp:46-50)
- [ ] 0 new public SDK symbol beyond the 1 helper (Cat-3 minimal-surface)
- [ ] 0 new singleton/registry/resolver/cache (per AGENTS.md deny-everywhere)
- [ ] 0 `#include <msdfgen>/<libtess2>/<unicode[/...]>` (Gate 5 Check 11 deny-everywhere preserved)
- [ ] All 3 prior call sites migrated to the new helper
- [ ] Test coverage: 1 TEST_CASE with 5 SUBCASEs (3 accepted values + 1 invalid input + 1 case-insensitive variant) = 1 TEST_CASE total (matches the established SUBCASE pattern in `tests/cache/test_framebuffer_pool.cpp:540-594` for the trim_after_job policy tests; the case-insensitive SUBCASE iterates the 3 accepted values in PascalCase form; no round-trip — deferred to future forward-point if a concrete caller emerges)

## macchina-verifica

DEFERRED-WBH per `TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV` vcpkg glm/magic_enum env-block pattern + `TICKET-BUILD-ROT-CASCADE-CAMERA` 409-error rot. VPS-only verification: `rg -n 'parse_framebuffer_pool_clear_policy|trim-after-job|keep-warm|trim-on-memory-pressure' src/ apps/` to confirm 3 call sites + 0 pre-existing helper.

## Cat-3 minimal-surface

- 1 NEW function in canonical header (`include/chronon3d/cache/framebuffer_pool.hpp` or new tools/ header)
- 3 EDIT call sites: `src/core/config.cpp` + `apps/chronon3d_cli/utils/job/render_job.cpp` + `apps/chronon3d_cli/commands/render/register_render_commands.cpp`
- 1 NEW test file: `tests/cache/test_parse_framebuffer_pool_clear_policy.cpp` (1 TEST_CASE with 5 SUBCASEs)

NO new public SDK symbol beyond the 1 helper (a single `[[nodiscard]]` pure function over the existing enum). NO new singleton/registry/resolver/cache. NO deny-everywhere include.

## Cross-link

- **Parent ticket**: [TICKET-ARCH-CLEANUP-V0](docs/tickets/TICKET-ARCH-CLEANUP-V0.md) §Forward-points (5th item)
- **Related chore**: [TICKET-FB-POOL-CLEAR-POLICY-CALL-SITE](docs/tickets/TICKET-FB-POOL-CLEAR-POLICY-CALL-SITE.md) (companion forward-point: wire the policy into the job executor)
- **Cat-3 anti-dup discipline**: 1 helper + 3 call sites + 1 test = 4 file touches total (the optional `name()` round-trip helper is deferred to a future forward-point if a concrete caller emerges — e.g., if `register_render_commands.cpp` CLI help text is migrated to dynamic generation)
- **Sibling forward-points from TICKET-ARCH-CLEANUP-V0**:
  - [TICKET-RENDER-SERVICES-FULL-ELIMINATION](docs/tickets/TICKET-RENDER-SERVICES-FULL-ELIMINATION.md)
  - [TICKET-RENDER-RUNTIME-MIGRATION-FOR-TESTS](docs/tickets/TICKET-RENDER-RUNTIME-MIGRATION-FOR-TESTS.md)
  - [TICKET-FB-POOL-CLEAR-POLICY-CALL-SITE](docs/tickets/TICKET-FB-POOL-CLEAR-POLICY-CALL-SITE.md)
  - [TICKET-LEGACY-QUEUE-METHODS-FOLLOWUP](docs/tickets/TICKET-LEGACY-QUEUE-METHODS-FOLLOWUP.md)
- **Cat-5 3-doc chaser-chore pattern**: this ticket is opened via the same pattern (1 NEW ticket + 1 CHANGELOG entry + 1 §Open Blockers row)
