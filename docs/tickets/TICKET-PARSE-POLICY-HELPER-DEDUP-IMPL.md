# TICKET-PARSE-POLICY-HELPER-DEDUP-IMPL — Extract `parse_framebuffer_pool_clear_policy(name)` helper

> Stato: **DONE (2026-07-14, commit `<pending>`)** — implementation of [TICKET-PARSE-POLICY-HELPER-DEDUP](docs/tickets/TICKET-PARSE-POLICY-HELPER-DEDUP.md) via source commit `<pending>` `refactor(cache): extract parse_framebuffer_pool_clear_policy helper` (subject envelope TBD). Cat-3 minimal-surface. Chaser-chore (this file) added 2026-07-14 per the Cat-5 3-doc discipline.

## Problema (forward-point recap)

The `FramebufferPoolClearPolicy` enum string parsing was duplicated in 2 productive call sites (the ticket said 3, but the 3rd — `register_render_commands.cpp:63` — is a hardcoded CLI description string, NOT a parse call site; per ticket Criteri di accettazione the round-trip `name()` helper that would derive the description dynamically is OUT OF SCOPE for this ticket):

1. **`src/core/config.cpp:193-201`** — env var resolution: parses `CHRONON3D_FB_POOL_CLEAR_POLICY` value
2. **`apps/chronon3d_cli/utils/job/render_job.cpp:46-50`** — CLI flag resolution: parses `--fb-pool-clear-policy` value

This was a Cat-3 anti-dup violation: 2 places must be kept in sync if a new policy is added, and a typo in either place would silently fall through to the default policy.

## Soluzione Confine (implemented)

**SINGLE OUTCOME**: extract ONE canonical parse helper (stricter user-spec adherence — user only asked for `parse_framebuffer_pool_clear_policy(name)`; the optional `name()` round-trip helper is OUT OF SCOPE for this ticket and deferred to a future forward-point if a concrete caller emerges).

```cpp
// include/chronon3d/cache/framebuffer_pool.hpp (canonical header, next to the enum)
namespace chronon3d::cache {
    [[nodiscard]] inline std::optional<FramebufferPoolClearPolicy>
    parse_framebuffer_pool_clear_policy(std::string_view name) noexcept {
        // Accept both lowercase and PascalCase forms (matches existing
        // behavior in src/core/config.cpp:193-201 and render_job.cpp:46-50).
        if (name == "keep-warm" || name == "KeepWarm") {
            return FramebufferPoolClearPolicy::KeepWarm;
        }
        if (name == "trim-after-job" || name == "TrimAfterJob") {
            return FramebufferPoolClearPolicy::TrimAfterJob;
        }
        if (name == "trim-on-memory-pressure" || name == "TrimOnMemoryPressure") {
            return FramebufferPoolClearPolicy::TrimOnMemoryPressure;
        }
        return std::nullopt;
    }
}
```

### Migration of 2 call sites

**`src/core/config.cpp`** — replace the 3-branch if/else chain with a single helper call:
```cpp
// BEFORE:
if (sv == "keep-warm" || sv == "KeepWarm") {
    cache_.framebuffer_pool_clear_policy_ = chronon3d::cache::FramebufferPoolClearPolicy::KeepWarm;
} else if (sv == "trim-after-job" || sv == "TrimAfterJob") {
    cache_.framebuffer_pool_clear_policy_ = chronon3d::cache::FramebufferPoolClearPolicy::TrimAfterJob;
} else if (sv == "trim-on-memory-pressure" || sv == "TrimOnMemoryPressure") {
    cache_.framebuffer_pool_clear_policy_ = chronon3d::cache::FramebufferPoolClearPolicy::TrimOnMemoryPressure;
} else { ... warning ... }

// AFTER:
if (auto parsed = chronon3d::cache::parse_framebuffer_pool_clear_policy(sv)) {
    cache_.framebuffer_pool_clear_policy_ = *parsed;
} else { ... warning ... }
```

**`apps/chronon3d_cli/utils/job/render_job.cpp`** — same pattern (replace 3-branch if/else with helper call).

**`apps/chronon3d_cli/commands/render/register_render_commands.cpp:63`** — **NO CHANGE** (out of scope per ticket Criteri di accettazione: the hardcoded CLI description string remains untouched; a future `name()` round-trip helper could derive this list dynamically, but that is deferred to a separate forward-point ticket).

### Test coverage (1 TEST_CASE with 5 SUBCASEs)

`tests/cache/test_parse_framebuffer_pool_clear_policy.cpp` — matches the established SUBCASE pattern in `tests/cache/test_framebuffer_pool.cpp:540-594` for the `trim_after_job` policy tests:

1. `SUBCASE("keep-warm")` → `KeepWarm`
2. `SUBCASE("trim-after-job")` → `TrimAfterJob`
3. `SUBCASE("trim-on-memory-pressure")` → `TrimOnMemoryPressure`
4. `SUBCASE("invalid")` → `nullopt` (tests `""`, `"bogus-value"`, `" keep-warm"`, `"keep-warm "`, `"keep_warm"` — strict matching, no auto-trimming or auto-conversion)
5. `SUBCASE("case-insensitive PascalCase")` → iterates `"KeepWarm"` / `"TrimAfterJob"` / `"TrimOnMemoryPressure"` (verifies the case-insensitive variant)

### Build integration

- `tests/cache/test_parse_framebuffer_pool_clear_policy.cpp` (NEW, 1 TEST_CASE with 5 SUBCASEs)
- `tests/cache/parse_framebuffer_pool_clear_policy_tests.cmake` (NEW, TIER=UNIT unconditional registration per ADR-018)
- `tests/CMakeLists.txt` (EDIT, +1 line in CMAKE_CONFIGURE_DEPENDS to register the new cmake file)

## Criteri di accettazione

- [x] `parse_framebuffer_pool_clear_policy("keep-warm")` returns `FramebufferPoolClearPolicy::KeepWarm`
- [x] `parse_framebuffer_pool_clear_policy("trim-after-job")` returns `FramebufferPoolClearPolicy::TrimAfterJob`
- [x] `parse_framebuffer_pool_clear_policy("trim-on-memory-pressure")` returns `FramebufferPoolClearPolicy::TrimOnMemoryPressure`
- [x] Case-insensitive variant: `parse_framebuffer_pool_clear_policy("KeepWarm")` / `"TrimAfterJob"` / `"TrimOnMemoryPressure"` returns the correct enum value
- [x] Invalid input (empty string, bogus value, whitespace, underscores) returns `std::nullopt`
- [x] 0 new public SDK symbol beyond the 1 helper (a single `[[nodiscard]]` pure function over the existing enum)
- [x] 0 new singleton/registry/resolver/cache
- [x] 0 `#include <msdfgen>/<libtess2>/<unicode[/...]>` (Gate 5 Check 11 deny-everywhere preserved)
- [x] 2 productive call sites migrated to the new helper (`config.cpp` + `render_job.cpp`)
- [x] `register_render_commands.cpp:63` CLI description string left as hardcoded list (out of scope per ticket)
- [x] Test coverage: 1 TEST_CASE with 5 SUBCASEs (3 accepted values + 1 invalid + 1 case-insensitive variant)

## macchina-verifica

DEFERRED-WBH per `TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV` vcpkg glm/magic_enum env-block pattern. VPS-only verification:
- `rg "parse_framebuffer_pool_clear_policy" include/ src/ apps/ tests/` → helper + 2 call sites + 1 test file (4 matches in source + 1 in test)
- `rg "policy_str == \"keep-warm\"|sv == \"keep-warm\"" src/ apps/` → 0 matches (both call sites migrated, no direct string comparison remains)
- `rg "keep-warm" src/ apps/` → only in the CLI description string `register_render_commands.cpp:63` (out of scope per ticket)
- `cmake --build build/chronon/linux-content-dev --target chronon3d_parse_framebuffer_pool_clear_policy_tests` → 0 errors (if build is possible on the working build host)

## Cat-3 minimal-surface

- 1 EDIT header (`include/chronon3d/cache/framebuffer_pool.hpp` — added 2 includes + 1 helper function)
- 2 EDIT call sites (`src/core/config.cpp` + `apps/chronon3d_cli/utils/job/render_job.cpp`)
- 1 NEW test file (`tests/cache/test_parse_framebuffer_pool_clear_policy.cpp` — 1 TEST_CASE with 5 SUBCASEs)
- 1 NEW cmake file (`tests/cache/parse_framebuffer_pool_clear_policy_tests.cmake` — TIER=UNIT)
- 1 EDIT `tests/CMakeLists.txt` (+1 line in CMAKE_CONFIGURE_DEPENDS)
- 0 NEW public SDK symbol beyond the 1 helper
- 0 NEW singleton/registry/resolver/cache
- 0 deny-everywhere include

NO surface-additive change beyond the 1 helper. Pure recipe-substitution per the established Cat-3 anti-dup rule "non introdurre nuovi singleton/registry/resolver/cache senza ADR".

## Cross-link

- **Parent forward-point ticket**: [TICKET-PARSE-POLICY-HELPER-DEDUP](docs/tickets/TICKET-PARSE-POLICY-HELPER-DEDUP.md) — this ticket is the implementation
- **Companion forward-point ticket**: [TICKET-FB-POOL-CLEAR-POLICY-CALL-SITE](docs/tickets/TICKET-FB-POOL-CLEAR-POLICY-CALL-SITE.md) (the wiring ticket that the helper serves)
- **Sibling forward-points from TICKET-ARCH-CLEANUP-V0**:
  - [TICKET-RENDER-SERVICES-FULL-ELIMINATION](docs/tickets/TICKET-RENDER-SERVICES-FULL-ELIMINATION.md) (closed 2026-07-14 via vacuous-truth audit)
  - [TICKET-RENDER-RUNTIME-MIGRATION-FOR-TESTS](docs/tickets/TICKET-RENDER-RUNTIME-MIGRATION-FOR-TESTS.md) (closed 2026-07-14 via migration commit `4faf81b4`)
  - [TICKET-FB-POOL-CLEAR-POLICY-CALL-SITE](docs/tickets/TICKET-FB-POOL-CLEAR-POLICY-CALL-SITE.md) (closed 2026-07-14 via wiring commit `f1d8cc34`)
  - [TICKET-LEGACY-QUEUE-METHODS-FOLLOWUP](docs/tickets/TICKET-LEGACY-QUEUE-METHODS-FOLLOWUP.md) (next: (e))
- **Cat-5 3-doc chaser-chore pattern**: this ticket is the implementation chaser-chore (1 NEW ticket-home + 1 CHANGELOG entry + 1 §Recently Closed row move)
