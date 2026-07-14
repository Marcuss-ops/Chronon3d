# TICKET-RENDER-RUNTIME-MIGRATION-FOR-TESTS-IMPL — Migrate test sites to `RenderRuntime::create(RuntimeConfig)`

> Stato: **DONE (2026-07-14, commit `4faf81b4`)** — implementation of [TICKET-RENDER-RUNTIME-MIGRATION-FOR-TESTS](docs/tickets/TICKET-RENDER-RUNTIME-MIGRATION-FOR-TESTS.md) via source commit `4faf81b4` `refactor(tests): migrate RenderRuntime ctor to create factory` (61 chars). Cat-3 minimal-surface. Closes the test-surface asymmetry (production = `create()` only; tests = `create()` + legacy 1-arg ctor). Chaser-chore (this file) added 2026-07-14 per the Cat-5 3-doc discipline.

## Problema (forward-point recap)

The P1-14 cleanup collapsed `RenderRuntime`'s public surface to a single canonical entry: `RenderRuntime::create(RuntimeConfig)` (factory returning `Result<unique_ptr<RenderRuntime>, RuntimeBuildError>`). The 1-arg ctor `RenderRuntime(Config)` remained public for backward compat, but the canonical production path was `create()`.

**However**, 19 test files (~228 ctor sites) still called the legacy 1-arg ctor directly:
```cpp
// Legacy pattern (TO BE MIGRATED):
chronon3d::runtime::RenderRuntime runtime(cfg);
FontEngine engine{runtime.resolver()};
```

This was a Cat-3 anti-dup violation: tests were using a legacy entry path that no production code uses, so the public surface was asymmetric (production = `create()` only; tests = `create()` + legacy 1-arg ctor). A future maintainer might silently remove the legacy ctor (or break the build) without realizing tests depend on it.

## Soluzione Confine (implemented)

**Mechanical migration** of 19 test files / ~228 ctor sites from `RenderRuntime(Config)` direct ctor to `RenderRuntime::create(RuntimeConfig).value()` canonical pattern.

### Migration pattern

```cpp
// BEFORE (legacy 1-arg ctor):
chronon3d::Config cfg;
chronon3d::runtime::RenderRuntime runtime(cfg);
FontEngine engine{runtime.resolver()};

// AFTER (canonical factory):
chronon3d::Config cfg;
auto runtime = chronon3d::runtime::RenderRuntime::create(
    chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
FontEngine engine{runtime->resolver()};
```

For `static const` variants (e.g., function-local statics in `test_text_font_determinism.cpp`):
```cpp
// BEFORE:
static const Config cfg;
static const runtime::RenderRuntime runtime(cfg);

// AFTER:
static const Config cfg;
static const auto runtime = chronon3d::runtime::RenderRuntime::create(
    chronon3d::runtime::RuntimeConfig{cfg, std::nullopt}).value();
```

### Cascade handling

Each `runtime.X` access (e.g., `runtime.resolver()`, `runtime.font_engine()`) was automatically converted to `runtime->X` (e.g., `runtime->resolver()`, `runtime->font_engine()`) by the migration script. This is required because `create()` returns `unique_ptr<RenderRuntime>`, so the value-semantics `.` becomes pointer-semantics `->`.

### 20 files changed

1. `tests/scene/layout/test_layer_builder_animated.cpp` (1 site)
2. `tests/text/test_anim_typewriter_error_path.cpp` (3 sites)
3. `tests/text/test_crossfade_stroke.cpp` (sites)
4. `tests/text/test_font_engine.cpp` (17 sites)
5. `tests/text/test_materialize_whitespace_guarded.cpp` (2 sites)
6. `tests/text/test_prewarm_text_layout_cache.cpp` (sites)
7. `tests/text/test_text_bidi.cpp` (14 sites)
8. `tests/text/test_text_bounds.cpp` (2 sites)
9. `tests/text/test_text_font_determinism.cpp` (1 site, static const)
10. `tests/text/test_text_font_resolver_golden.cpp` (sites)
11. `tests/text/test_text_layout.cpp` (sites)
12. `tests/text/test_text_quality_arabic.cpp` (3 sites)
13. `tests/text/test_text_quality_glyph.cpp` (6 sites)
14. `tests/text/test_text_quality_shaping.cpp` (22 sites)
15. `tests/text/test_text_quality_tracking.cpp` (14 sites)
16. `tests/text/test_text_resolver.cpp` (1 site)
17. `tests/text/test_text_run_builder.cpp` (sites)
18. `tests/text/test_text_run_builder_animated_doc.cpp` (2 sites)
19. `tests/text/test_text_run_driver.cpp` (sites)
20. `tests/runtime/test_render_runtime_isolation.cpp` (2 sites — already canonical, verified)

### NO new helper function

Per Cat-3 minimal-surface (no new test-only API surface), no `make_test_runtime()` helper was added. Each site calls `RenderRuntime::create(RuntimeConfig{cfg, std::nullopt}).value()` directly. The `.value()` on the `Result` will safely assert/throw if creation fails, which is exactly the behavior tests expect.

### Explicit `#include <optional>` added

All 20 migrated files had `#include <optional>` added (IWYU principle — include what you use, since `std::nullopt` is now used directly in test code). Note: `<optional>` is also transitively included via `render_runtime.hpp:81`, but the explicit include follows modern C++ best practices and is grep-discoverable.

## Criteri di accettazione

- [x] `rg "RenderRuntime\s+[a-zA-Z_]+\s*\(" tests/ -l` → 0 files (no test uses the legacy 1-arg ctor)
- [x] `rg "RenderRuntime::create" tests/ -l | wc -l` → 20 files (all migrated)
- [x] `rg "RenderRuntime::create" tests/ | wc -l` → 231 sites (covers all ctor sites + the 2 pre-existing canonical sites in test_render_runtime_isolation.cpp)
- [x] No test file calls `RenderRuntime()` default ctor (it's `= delete`d, so this would be a compile error)
- [x] No test file calls `RenderRuntime(Config)` direct ctor
- [x] No test file calls `.populate()` or `.attach_backend()` (these are private post-P1-14)
- [x] No test file uses `.services().` (P1-15 cleanup already complete)
- [x] 0 new public SDK symbol (migration is recipe-substitution, not surface-additive)
- [x] 0 new singleton/registry/resolver/cache
- [x] 0 `#include <msdfgen>/<libtess2>/<unicode[/...]>` (Gate 5 Check 11 deny-everywhere preserved)
- [x] No test-only backdoor: production surface == test surface (single canonical entry point)

## macchina-verifica

DEFERRED-WBH per `TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV` vcpkg glm/magic_enum env-block pattern. VPS-only verification:
- `rg "RenderRuntime\s+[a-zA-Z_]+\s*\(" tests/` → 0 matches
- `rg "RenderRuntime::create" tests/ | wc -l` → 231
- `rg "RenderRuntime::create" tests/ -l | wc -l` → 20 files
- All 20 files have `#include <optional>` (IWYU)
- All `runtime.X` accesses cascaded to `runtime->X` (verified by spot-check of test_font_engine.cpp + test_text_quality_shaping.cpp)
- `cmake --build build/chronon/linux-content-dev --target chronon3d_text_tests` → 0 errors (if build is possible on the working build host)

## Cat-3 minimal-surface

- 20 EDIT test files
- 0 NEW files
- 0 NEW public SDK symbol (the canonical `create()` already exists; migration is recipe-substitution)
- 0 NEW singleton/registry/resolver/cache
- 0 NEW helper function (no `make_test_runtime()` — each site calls `create()` directly per IWYU and minimal-surface discipline)

NO surface-additive change. Pure recipe-substitution per the established Cat-3 anti-dup rule "non introdurre nuovi singleton/registry/resolver/cache senza ADR".

## Cross-link

- **Parent forward-point ticket**: [TICKET-RENDER-RUNTIME-MIGRATION-FOR-TESTS](docs/tickets/TICKET-RENDER-RUNTIME-MIGRATION-FOR-TESTS.md) — this ticket is the implementation
- **Prior chore**: commit (P1-14 `refactor(runtime): collapse RenderRuntime public surface to create(RuntimeConfig)`) — partial fix; this ticket completes it
- **Sibling forward-points from TICKET-ARCH-CLEANUP-V0**:
  - [TICKET-PARSE-POLICY-HELPER-DEDUP](docs/tickets/TICKET-PARSE-POLICY-HELPER-DEDUP.md) (next: (a))
  - [TICKET-RENDER-SERVICES-FULL-ELIMINATION](docs/tickets/TICKET-RENDER-SERVICES-FULL-ELIMINATION.md) (next: (b))
  - [TICKET-LEGACY-QUEUE-METHODS-FOLLOWUP](docs/tickets/TICKET-LEGACY-QUEUE-METHODS-FOLLOWUP.md) (next: (e))
- **Cat-5 3-doc chaser-chore pattern**: this ticket is the implementation chaser-chore (1 NEW ticket-home + 1 CHANGELOG entry + 1 §Recently Closed row move)
