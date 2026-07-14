# TICKET-RENDER-RUNTIME-MIGRATION-FOR-TESTS — Migrate ~12 test sites to `RenderRuntime::create(RuntimeConfig)`

> Stato: **OPEN (2026-07-14)** — forward-point from [TICKET-ARCH-CLEANUP-V0](docs/tickets/TICKET-ARCH-CLEANUP-V0.md) §Forward-points (3rd item). Cat-3 minimal-surface.

## Problema

The P1-14 cleanup collapsed `RenderRuntime`'s public surface to a single entry point: `RenderRuntime::create(RuntimeConfig)`. The other entry paths (`RenderRuntime()` default ctor + `RenderRuntime(Config)` direct ctor + `populate()` + `attach_backend()`) are now private (or moved to an internal builder).

**However**, ~12 test sites in `tests/` still call the legacy entry paths directly:

```cpp
// Legacy patterns (TO BE MIGRATED):
RenderRuntime rt;
RenderRuntime rt2(config);
rt.populate();
rt.attach_backend(my_backend);
rt.attach_backend(other_backend);

// Canonical pattern (TARGET):
auto rt = RenderRuntime::create(RuntimeConfig{...});
```

This is a Cat-3 anti-dup violation: tests are using legacy entry paths that no production code uses, so the public surface is asymmetric (production = `create()` only; tests = `create()` + legacy ctors). A future maintainer might silently remove the legacy ctors (or break the build) without realizing tests depend on them.

## Soluzione Confine

**SINGLE OUTCOME**: migrate ALL ~12 test sites to the canonical `RenderRuntime::create(RuntimeConfig)` entry point. Do NOT keep the legacy ctors as test-only backdoors (no test-specific API surface).

Steps:
1. **Audit**: `rg "RenderRuntime\\(" tests/ -l` to enumerate all ~12 sites
2. **Migrate** each site: replace legacy ctor calls with `RenderRuntime::create(RuntimeConfig{...})`
3. **Verify** the legacy ctors are now unreachable from `tests/` (no test backdoor)
4. **Optionally**: make the legacy ctors `[[deprecated]]` to surface any future accidental re-introduction (the deprecation marker is acceptable for the migration window; full removal is a follow-up)

If the legacy ctors are already private (per P1-14), the audit step will find compile errors that pinpoint every test site that needs migration.

## Criteri di accettazione

- [ ] `rg "RenderRuntime\\(" tests/ -l | wc -l` → 0 (or matches only inside helper infrastructure that returns `RenderRuntime&` from a factory, not direct ctor calls)
- [ ] `rg "RenderRuntime::create\\(" tests/ -l | wc -l` → ≥ 12 (all sites migrated)
- [ ] No test file calls `RenderRuntime()` default ctor (or `RenderRuntime(Config)` direct ctor)
- [ ] No test file calls `.populate()` or `.attach_backend()` on a `RenderRuntime` (these are private post-P1-14)
- [ ] If legacy ctors are still public: they have `[[deprecated("use RenderRuntime::create(RuntimeConfig)")]]` markers
- [ ] 0 new public SDK symbol (migration is recipe-substitution, not surface-additive)
- [ ] 0 new singleton/registry/resolver/cache
- [ ] 0 `#include <msdfgen>/<libtess2>/<unicode[/...]>` (Gate 5 Check 11 deny-everywhere preserved)
- [ ] No test-only backdoor: production surface == test surface (single canonical entry point)

## macchina-verifica

DEFERRED-WBH per `TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV` vcpkg glm/magic_enum env-block pattern + `TICKET-BUILD-ROT-CASCADE-CAMERA` 409-error rot. VPS-only verification:
- `rg "RenderRuntime\\(" tests/ -l` → enumerate the ~12 sites
- After migration: `rg "RenderRuntime::create\\(" tests/ -l | wc -l` → ≥ 12
- `cmake --build build/chronon/linux-content-dev --target chronon3d_core_tests` → 0 errors (if build is possible on the working build host)

## Cat-3 minimal-surface

- ~12 EDIT test files (one per migration site)
- 0 NEW files
- 0 NEW public SDK symbol (the canonical `create()` already exists; migration is recipe-substitution)
- 0 NEW singleton/registry/resolver/cache

NO surface-additive change. Pure recipe-substitution per the established Cat-3 anti-dup rule "non introdurre nuovi singleton/registry/resolver/cache senza ADR".

## Cross-link

- **Parent ticket**: [TICKET-ARCH-CLEANUP-V0](docs/tickets/TICKET-ARCH-CLEANUP-V0.md) §Forward-points (3rd item)
- **Prior chore**: commit (P1-14 `refactor(runtime): collapse RenderRuntime public surface to create(RuntimeConfig)`) — partial fix; this ticket completes it
- **Sibling forward-points from TICKET-ARCH-CLEANUP-V0**:
  - [TICKET-PARSE-POLICY-HELPER-DEDUP](docs/tickets/TICKET-PARSE-POLICY-HELPER-DEDUP.md)
  - [TICKET-RENDER-SERVICES-FULL-ELIMINATION](docs/tickets/TICKET-RENDER-SERVICES-FULL-ELIMINATION.md)
  - [TICKET-FB-POOL-CLEAR-POLICY-CALL-SITE](docs/tickets/TICKET-FB-POOL-CLEAR-POLICY-CALL-SITE.md)
  - [TICKET-LEGACY-QUEUE-METHODS-FOLLOWUP](docs/tickets/TICKET-LEGACY-QUEUE-METHODS-FOLLOWUP.md)
- **Cat-5 3-doc chaser-chore pattern**: this ticket is opened via the same pattern (1 NEW ticket + 1 CHANGELOG entry + 1 §Open Blockers row)
