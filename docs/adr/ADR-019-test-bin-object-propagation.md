# ADR-019 — Canonical CMake link pattern for in-tree test executables: OBJECT `.o` propagation and static-init symbol retention

| Field | Value |
|---|---|
| **Status** | Accepted |
| **Date** | 2026-06-23 |
| **Deciders** | `codex/fix-ticket038-lambda-capture` author, thinker-with-files-gemini |
| **Tags** | `cmake`, `linker`, `whole-archive`, `tests`, `static-init`, `object-propagation`, `txt-00` |
| **Related** | [ADR-018 — CMake `CMAKE_CONFIGURE_DEPENDS` for per-area `.cmake` staleness](./ADR-018-link-rot-text-visual.md), [docs/baselines/tcb-txt-00-f-c-blocked-4ab8cbb8.md](../baselines/tcb-txt-00-f-c-blocked-4ab8cbb8.md) (F-C empirical history), `tests/renderer_tests.cmake` (working reference), `tests/text_preset_visual_tests.cmake` (failing surface), `src/CMakeLists.txt` (sdk / sdk_impl definition), `cmake/Chronon3DRegistry.cmake` (OBJECT library registry) |

## Context

### The propagation gap

The project defines two consumer-facing targets in `src/CMakeLists.txt`:

1. **`chronon3d_sdk`** (INTERFACE) — build-time surface for in-tree consumers (CLI, tests). Propagates per-subsystem OBJECT `.o` files via:
   ```cmake
   foreach(_reg_obj IN LISTS CHRONON3D_REGISTRY_OBJECT_LIBS)
       if(TARGET ${_reg_obj})
           target_sources(chronon3d_sdk INTERFACE
               $<BUILD_INTERFACE:$<TARGET_OBJECTS:${_reg_obj}>>)
       endif()
   endforeach()
   ```
   This uses the `$<TARGET_OBJECTS:...>` generator expression to list `.o` files in the consuming target's link line. It also propagates `INTERFACE` include paths and public third-party dependencies (glm, fmt, spdlog, tbb, …).

2. **`chronon3d_sdk_impl`** (STATIC) — single aggregated archive (`libchronon3d_sdk_impl.a`) for install-time consumers (`find_package(Chronon3D)`). Bundles all OBJECT `.o` files via:
   ```cmake
   foreach(_reg_obj IN LISTS CHRONON3D_REGISTRY_OBJECT_LIBS)
       if(TARGET ${_reg_obj})
           target_link_libraries(chronon3d_sdk_impl PRIVATE ${_reg_obj})
       endif()
   endforeach()
   ```
   CMake 3.12+ aggregates OBJECT `.o` files into the STATIC archive when OBJECT libs are linked via `target_link_libraries`.

**The gap**: when a test executable links ONLY `chronon3d_sdk` (INTERFACE), the `$<TARGET_OBJECTS:...>` generator expression does not reliably propagate all `.o` files to the link line for every generator / linker combination. The result is link-time undefined references.

### Empirical evidence: TXT-00 F-C closure attempts

The `chronon3d_text_preset_visual_tests` target exhibited this gap across 7 iterations of the F-C closure attempt (documented in full at `docs/baselines/tcb-txt-00-f-c-blocked-4ab8cbb8.md`):

| Iter | Link surface | Build rc | Outcome |
|---|---|---|---|
| 1 | `chronon3d_sdk` only | 1 | 96 undefined refs |
| 2 | + 8 granular OBJECT libs | 1 | sprouted new misses (Config, FontEngine, …) |
| 3 | + `chronon3d_core_impl` + `chronon3d_backend_text` | 1 | sprouted ImageCache, StbImageBackend, … |
| 4 | Structural fix in `src/CMakeLists.txt` (D3) | 1 | 30 undefined refs; fix was install-unsafe |
| 5 | `chronon3d_sdk_impl` as FIRST PRIVATE dep (Option C) | 1 | 30 undefined refs — single-pass linker dropped static-init symbols |

Key finding from iteration 5 (Option C): even when `chronon3d_sdk_impl` (the STATIC archive containing ALL `.o` files) is linked, single-pass linkers (mold, GNU ld, lld) process static archives left-to-right and discard objects whose symbols are not yet referenced at that point in the link line. Symbols reachable only via static-initializer constructors (effect registries, layer builders, factory functions) are silently dropped.

### Working reference: `tests/renderer_tests.cmake`

The renderer test target links successfully with:
```cmake
target_link_libraries(chronon3d_renderer_tests
    PRIVATE
        chronon3d_sdk chronon3d_sdk_impl chronon3d_pipeline
        chronon3d_scene
        doctest::doctest
        $<$<TARGET_EXISTS:chronon3d_backend_text>:chronon3d_backend_text>
)
```

This works because:
1. `chronon3d_sdk` supplies INTERFACE include paths and public third-party deps transitively.
2. `chronon3d_sdk_impl` provides the STATIC archive with all `.o` files.
3. The renderer tests' own `.o` files reference the symbols in the archive directly (no static-init-only drop), so the single-pass linker resolves them without `--whole-archive`.

## Decision

### Canonical pattern (Rule 1)

**Every in-tree test executable MUST link BOTH targets**, in this order:

```cmake
target_link_libraries(<test_target> PRIVATE
    chronon3d_sdk
    chronon3d_sdk_impl
    doctest::doctest
)
```

**Roles:**
- `chronon3d_sdk` (INTERFACE): provides `$<BUILD_INTERFACE:...>` include paths and transitively propagates required public third-party dependencies (glm, fmt, spdlog, tbb, magic_enum, nlohmann_json, xxHash). Listed FIRST so its INTERFACE usage requirements are visible to the linker before the archive.
- `chronon3d_sdk_impl` (STATIC): bundles every registry OBJECT `.o` file into `libchronon3d_sdk_impl.a`. The linker resolves undefined symbols from the test's own `.o` files against this archive.

**Test-specific additional deps** (e.g., `chronon3d_scene`, `chronon3d_backend_text`) may be appended after `chronon3d_sdk_impl` when the test TU directly references symbols from subsystems that have additional non-transitive link requirements. The canonical pattern is:
```cmake
target_link_libraries(<test_target> PRIVATE
    chronon3d_sdk
    chronon3d_sdk_impl
    # test-specific additional deps here
    doctest::doctest
)
```

### When `--whole-archive` is needed (Rule 2)

If the canonical pattern still produces undefined references, the test target's `.o` files do not directly reference symbols that are reachable ONLY through static-initializer constructors (e.g., effect registries, factory-registration side effects, layer-builder vtables). In this case, wrap `chronon3d_sdk_impl` with `-Wl,--whole-archive`:

```cmake
target_link_libraries(<test_target> PRIVATE
    chronon3d_sdk
    "-Wl,--whole-archive" chronon3d_sdk_impl "-Wl,--no-whole-archive"
    # test-specific additional deps here
    doctest::doctest
)
```

**When to apply Rule 2:** apply it from the start for any test target that exercises symbols registered via static-initializer patterns (effect catalogs, presets, layer builders, registry auto-registration). If in doubt, apply it — the cost is a few kB of dead code in the binary; the benefit is no silent symbol drop.

**Platform note:** `-Wl,--whole-archive` is supported by GCC, Clang, and mold on Linux. For MSVC, the equivalent is `/WHOLEARCHIVE:<lib>` (not used in linux-ci / macos-dev presets). For Apple ld64, use `-Wl,-force_load,path/to/lib.a`. The linux-ci preset is the primary gate; platform-specific wrappers can be added as needed.

### Anti-patterns (forbidden)

1. **Granular OBJECT hunting.** Do NOT enumerate individual OBJECT libs (`chronon3d_software`, `chronon3d_scene`, `chronon3d_cache`, …) in test `target_link_libraries` to chase undefined references one-by-one. The F-C iteration history proves this does not converge — each new lib reveals symbols from another transitive dependency. Let `chronon3d_sdk_impl` (with `--whole-archive` if needed) resolve all symbols.

2. **Link-order hacking.** Do NOT attempt to fix symbol drops by reordering `chronon3d_sdk_impl` relative to other deps (e.g., "put it before `chronon3d_software`"). Single-pass linker discard makes this brittle across linkers and architectures. Use `--whole-archive` instead.

3. **Modifying `src/CMakeLists.txt` SDK propagation.** Do NOT edit the `foreach` loop that feeds `chronon3d_sdk` or `chronon3d_sdk_impl` to fix a downstream test linker issue. The `target_sources(INTERFACE $<TARGET_OBJECTS:...>)` pattern for `chronon3d_sdk` and the `target_link_libraries(PRIVATE obj)` pattern for `chronon3d_sdk_impl` are the correct split for BUILD vs INSTALL surfaces. Test fixes belong at the test surface.

4. **Linking ONLY `chronon3d_sdk` without `chronon3d_sdk_impl`.** This is the root cause of the TXT-00 rot. `chronon3d_sdk` is an INTERFACE target whose `$<TARGET_OBJECTS:...>` propagation is generator-dependent; relying on it alone for test executables is unreliable.

5. **Linking ONLY `chronon3d_sdk_impl` without `chronon3d_sdk`.** The STATIC archive provides `.o` files but does NOT propagate the INTERFACE include paths and public third-party deps that `chronon3d_sdk` carries. Both are needed.

## Consequences

### Positive

- **One pattern, all tests.** Any developer adding a new test target follows the same two-rule pattern (Rule 1: both SDK targets; Rule 2: `--whole-archive` if static-init symbols appear). No per-target archaeology needed.
- **Zero `src/CMakeLists.txt` changes.** The BUILD vs INSTALL split (INTERFACE `chronon3d_sdk` for in-tree, STATIC `chronon3d_sdk_impl` for installed consumers) stays intact.
- **Registry-driven.** When a new OBJECT library is added to `cmake/Chronon3DRegistry.cmake`, `chronon3d_sdk_impl` automatically picks it up — no test-target updates needed unless the test references the new subsystem's symbols directly.
- **Deterministic across linkers.** `--whole-archive` forces all `.o` files in regardless of linker implementation (mold, GNU ld, lld) or pass strategy (single-pass, multi-pass).

### Negative

- **Binary size increase with `--whole-archive`.** Forcing all objects from `libchronon3d_sdk_impl.a` into the test binary adds dead code (unreferenced subsystems). For a test binary this is acceptable (~hundreds of kB, not MB). Production binaries (CLI, daemon) are unaffected — they link via the canonical two-target pattern without `--whole-archive`.
- **Platform-specific `--whole-archive` syntax.** `-Wl,--whole-archive` is GCC/Clang/Linux-specific. Apple ld64 requires `-Wl,-force_load,<path>`. MSVC requires `/WHOLEARCHIVE:<lib>`. The linux-ci preset is the primary gate; cross-platform wrappers can be added when macos-dev / msvc-ci presets are activated.
- **Maintenance: two rules to remember.** Rule 1 always; Rule 2 when static-init symbols are dropped. The cost of forgetting Rule 2 is a link failure — the gate is self-enforcing.

### Neutral

- **No external consumer impact.** This ADR governs in-tree test target linkage only. Installed consumers use `find_package(Chronon3D)` → `Chronon3D::SDK` → `chronon3d_sdk_impl` STATIC archive via `$<INSTALL_INTERFACE:chronon3d_sdk_impl>` — unchanged.
- **No `src/CMakeLists.txt` change.** The SDK propagation loop is untouched.
- **`AGENTS.md` compliance.** This ADR adds a rule (canonical test link pattern), does not relax any existing rule.

## Migration path

### Existing test targets

Audit all test targets in `tests/*.cmake` against the canonical pattern. Current state:

| Test target | Links `chronon3d_sdk`? | Links `chronon3d_sdk_impl`? | Needs migration? |
|---|---|---|---|
| `chronon3d_renderer_tests` | ✅ | ✅ | No (canonical) |
| `chronon3d_text_preset_visual_tests` | ✅ | ✅ (Option C residual) | Rule 2 (`--whole-archive`) needed |
| `chronon3d_deterministic_tests` | ✅ (`chronon3d_sdk` + `chronon3d_graph` + `chronon3d_graph_pipeline` + `chronon3d_backend_software`) | ❌ | Add `chronon3d_sdk_impl` |
| Other test targets | TBD | TBD | Audit recommended |

Migration for each non-compliant target:
1. Add `chronon3d_sdk_impl` after `chronon3d_sdk` in `target_link_libraries`.
2. If the link step still fails with undefined references to static-init symbols, wrap `chronon3d_sdk_impl` with `-Wl,--whole-archive … -Wl,--no-whole-archive`.
3. Remove granular OBJECT libs that are now redundant (they are bundled in `chronon3d_sdk_impl`).
4. Keep test-specific deps that are NOT in the registry (e.g., `doctest::doctest`, conditional `chronon3d_backend_text`).

### New test targets

When adding a new test executable:
1. Start with the canonical two-target pattern (`chronon3d_sdk` + `chronon3d_sdk_impl`).
2. Build. If rc=0, done.
3. If undefined references to static-init symbols appear, apply Rule 2 (`--whole-archive`).
4. Add test-specific deps only if the test TU directly references symbols from a conditional subsystem (e.g., `chronon3d_backend_text` guarded by `$<TARGET_EXISTS:...>`).

## Alternatives considered

- **A. Fix `chronon3d_sdk` propagation directly (`target_sources` → `target_link_libraries(INTERFACE obj)`).** Rejected (D3 iteration, F-C history): this change is install-unsafe — it propagates OBJECT deps to `$<INSTALL_INTERFACE:...>` consumers without the `$<BUILD_INTERFACE:...>` wrapper. The existing `target_sources(INTERFACE $<TARGET_OBJECTS:...>)` is the correct BUILD-INTERFACE-only pattern. Changing it breaks the installed SDK surface.

- **B. Enumerate all OBJECT libs per test target.** Rejected (F-C iterations 1–3): does not converge; each new lib reveals symbols from another transitive dependency. Maintenance burden is O(N²) in the number of OBJECT libs.

- **C. Link ONLY `chronon3d_sdk_impl` without `chronon3d_sdk`.** Rejected: the STATIC archive does not propagate INTERFACE include paths or public third-party deps (glm, fmt, spdlog, …). Both targets are needed.

- **D. Use `$<TARGET_OBJECTS:...>` directly on each test target.** Rejected: duplicates the registry enumeration logic at each call site; brittle when OBJECT libs are added or removed. The registry-driven `chronon3d_sdk_impl` aggregation is the single source of truth.

- **E. Always use `--whole-archive` (eliminate Rule 1, mandate Rule 2 always).** Considered but rejected as over-broad: most test targets do not exercise static-init-only symbols and work correctly with the two-target pattern alone (cf. `chronon3d_renderer_tests`). Mandating `--whole-archive` universally would add dead code to every test binary with no benefit for the majority. Rule 1 is the default; Rule 2 is the escalation.

## References

- `src/CMakeLists.txt` — `chronon3d_sdk` (INTERFACE, `target_sources` + `$<TARGET_OBJECTS:...>`) and `chronon3d_sdk_impl` (STATIC, `target_link_libraries PRIVATE obj`) definitions, lines ~200–250.
- `cmake/Chronon3DRegistry.cmake` — `CHRONON3D_REGISTRY_OBJECT_LIBS` list that drives both propagation loops.
- `tests/renderer_tests.cmake` — working reference: canonical two-target pattern (`chronon3d_sdk` + `chronon3d_sdk_impl` + test-specific deps).
- `tests/text_preset_visual_tests.cmake` — current state (Option C residual): `chronon3d_sdk_impl` present but `--whole-archive` missing; build rc=1 with 30 undefined refs.
- `docs/baselines/tcb-txt-00-f-c-blocked-4ab8cbb8.md` — full F-C empirical history (7 iterations, 4 strategies, root-cause hypotheses H1/H4).
- [ADR-018 — CMake `CMAKE_CONFIGURE_DEPENDS` for per-area `.cmake` staleness](./ADR-018-link-rot-text-visual.md) — prerequisite fix that ensures `cmake --build` re-runs configure when test `.cmake` files are edited.
- `cmake --help-policy CMP0074` — `find_package()` uses `PackageName_ROOT` variables (CMake 3.27+). Not directly relevant to OBJECT propagation but referenced for completeness of the CMake policy landscape.
- GNU ld documentation: `--whole-archive` — "For each archive mentioned on the command line after the `--whole-archive` option, include every object file in the archive in the link, rather than searching the archive for the required object files."
- CMake 3.12 release notes: "Object libraries may now be linked into other targets via `target_link_libraries()`." — the mechanism used by `chronon3d_sdk_impl` to aggregate OBJECT `.o` files into the STATIC archive.
- `AGENTS.md` §"Regole di lavoro" — "non segnare verde una suite che restituisce failure" / "non cambiare un gate per nascondere un errore" / "fare PR piccole e mirate".
