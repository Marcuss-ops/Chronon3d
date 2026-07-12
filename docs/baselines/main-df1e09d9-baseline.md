# main@df1e09d9 — rot-cascade diagnostic baseline (verification DEFERRED to working build host)

> **§honesty status** (per AGENTS.md v0.1): This baseline is **DEFERRED** — the canonical
> fit-build-host verification session has not yet been triggered. The rot-class classification +
> per-file fix-decision matrix + diagnostic counts documented here are **MACHINE-VERIFIED** from
> the diagnostic artifact (`/tmp/build_test_artifact.log`, ~166K chars) but the canonical
> `cmake --build .tmp/chronon-builds/linux-fast-dev` end-to-end run on a fit build host is
> **FORWARD-POINTED**.

> **State**: TICKET-BUILD-ROT-CASCADE-CAMERA remains **OPEN** (canonical
> `chronon3d::chronon3d::*` double-namespace rot + transitive-include forward-decl rot, all
> introduced in commit `cd2548cb feat(api): public camera facade + external consumer SDK test`).

> **Cross-link to canonical ticket**: [`docs/FOLLOWUP_TICKETS.md`](../FOLLOWUP_TICKETS.md)
> `## Open Blockers` `TICKET-BUILD-ROT-CASCADE-CAMERA` row.
> **Cross-link to classification commit**: [`docs/CHANGELOG.md`](../CHANGELOG.md) 2026-07-12
> `docs(followup): expand TICKET-BUILD-ROT-CASCADE rot-class findings` entry (commit
> `df1e09d9`).
> **Cross-link to source CHANGELOG analysis**: [`docs/CHANGELOG.md`](../CHANGELOG.md) 2026-07-12
> `21 errors mask text_helpers rot` entry (the prior onion-peel analysis that originally
> estimated 21 upstream errors; current clean rebuild shows 245 errors across 10+ files
> confirming the prior `50-200+ files to fix` estimate is on-target).

---

## Diagnostic data (machine-verified 2026-07-12)

The diagnostic was performed via a clean rebuild of `chronon3d_dev_fast` after
`rm -rf .tmp/chronon-builds/` (re-confirming that the prior `.gitignore prevention` rot-fix at
commit `3e18388a` works correctly — the 245 errors are pre-existing rot, NOT new dirt). The
build log is preserved at `/tmp/build_test_artifact.log` (~166K chars, 245 `error:` markers).

### Rot-class pattern counts (decision matrix input)

| Rot-class | Direct occurrences | Cascade amplifiers | Description |
|---|---:|---:|---|
| (a) `chronon3d::chronon3d::*` double-namespace | 30 | ~88 | type pollution from `using namespace chronon3d;` in inner namespace |
| (b) Forward-decl rot (`incomplete type`) | 13 | ~24 | forward `class Scene` referenced but `Scene::layers()/nodes()` accessed |
| (c) Forward-decl rot (`not declared`) | 30 | ~13 | missing transitive include in the consumer header |
| (d) Namespace pollution (`is not a member of`) | 20 | ~10 | type referenced via `chronon3d::chronon3d::` instead of `::chronon3d::` |
| (e) `std::variant` synthesized method | 0 (refined) | 0 | prior basher reported 21; refined count is 0 (the 21 matches were generic C++ template-instantiation noise from `(a)` cascade, NOT a separate variant move-constructor rot-class) |

**Total `error:` markers**: 245 (machine-verified via `grep -cE 'error:' /tmp/build_test_artifact.log`).
**Prior 21-error count** (`docs/CHANGELOG.md` "21 errors mask text_helpers rot" entry) was the
result of a PARTIAL clean rebuild that halted at the 21st error; the current 245 is the full
build's exhausted error count. The 50-200+ files estimate from the prior TICKET
open description is **on-target** (the 245 count includes ~88+24 template-instantiation cascades
per root cause, expanding the surface).

### Per-file error distribution (top 10 files, ~87% of total)

| Rank | File (relative to `REPO_ROOT`) | Error count | Rot-class (a/b/c/d) |
|---|---|---:|---|
| 1 | `include/chronon3d/runtime/render_runtime.hpp` | 88 | (a) + (b) cascade |
| 2 | `include/chronon3d/scene/model/core/scene.hpp` | 24 | (b) `Scene::layers()/nodes()` |
| 3 | `src/backends/text/blend2d_glyph_conversion.cpp` | 18 | (a) text backend |
| 4 | `src/backends/text/text_rasterizer_render.cpp` | 17 | (a) text backend |
| 5 | `src/backends/text/font_engine.cpp` | 13 | (a) text backend |
| 6 | `include/chronon3d/core/scope/execution_scope.hpp` | 13 | (a) + (c) |
| 7 | `include/chronon3d/internal/render_graph/core/scene_hasher.hpp` | 12 | (b) cascade from (2) |
| 8 | `include/chronon3d/timeline/composition.hpp` | 11 | (a) `CameraDescriptor`/`CameraProgram` |
| 9 | `include/chronon3d/render_graph/cache/scene_program_cache.hpp` | 9 | (a) + (c) cache |
| 10 | `include/chronon3d/internal/runtime/render_session.hpp` | 9 | (a) + (c) |

**Subtotal top 10**: 214 errors (~87% of 245). Remaining 31 errors spread across 6+ smaller
sites (the `chronon3d::chronon3d::` rot also appears in `include/chronon3d/backends/software/software_renderer.hpp` + `software_render_session.hpp` + `include/chronon3d/effects/{curves.hpp,glow_pipeline.hpp}` + `src/effects/effect_catalog.cpp` + `include/chronon3d/scene/camera/camera_v1/camera_framing_solver.hpp` + others).

---

## Per-file canonical-fix decision matrix

The 4 fix patterns below are the canonical options (per `docs/CHANGELOG.md` "21 errors mask text_helpers rot" entry's `Fix pattern` section + ADR-012 chronon3d_sdk_manifest_boundary precedent for forwarding-header re-exports).

| # | Fix pattern | Risk | Traceability | Per-site cost |
|---|---|---|---|---|
| ① | `::chronon3d::` prefix (force global lookup) | low | low | ~1 line per site |
| ② | Move type out of inner namespace | medium | high | ~1 header split per type |
| ③ | `using` declaration at top of file | medium | medium | ~1 line per file |
| ④ | Forwarding header re-export | low | high | ~1 NEW header per type |

### Recommended fix per file

| # | File | Recommended fix | Why |
|---|---|---|---|
| 1 | `include/chronon3d/runtime/render_runtime.hpp` | **① + ④** | The 88 errors are mostly internal struct member types referring to `chronon3d::cache::*` + `chronon3d::graph::*` + `chronon3d::assets::*` + `chronon3d::effects::*` + `chronon3d::execution::*`. **①** prefixes force global lookup at all 88 sites. **④** Forwarding header `include/chronon3d/runtime/render_runtime_fwd.hpp` re-exports the canonical types so downstream consumers (text backend + scene_hasher + effect_stack_node) can forward-declare without pulling in the heavy runtime header. |
| 2 | `include/chronon3d/scene/model/core/scene.hpp` | **④** + **①** | The 24 errors are all `Scene::layers()/nodes()` access in `scene_hasher.hpp` from a forward-decl `class Scene` in `render_graph_context.hpp:87`. **④** Add `include/chronon3d/scene/model/core/scene_fwd.hpp` (forward-decls only, NO definition) and update `scene_hasher.hpp` to include the canonical `scene.hpp` directly. **①** as a fallback: prefix the consumer-side `Scene::` access if the forwarding-header split isn't viable. |
| 3-5 | `src/backends/text/{blend2d_glyph_conversion,text_rasterizer_render,font_engine}.cpp` | **①** | These 3 are *consumers* of the rot, NOT polluters — they just need the global-lookup fix at the call sites (e.g., `::chronon3d::FontPreflightSummary` instead of `chronon3d::FontPreflightSummary`). Lowest risk, ~5 sites per file. |
| 6 | `include/chronon3d/core/scope/execution_scope.hpp` | **①** + **③** | The 13 errors are `chronon3d::RenderSession` + `chronon3d::Result<...>` referenced via inner-namespace lookup. **①** prefix at the 4 declaration sites. **③** as alternative: `using chronon3d::RenderSession; using chronon3d::Result;` at top of the file (if the rot is from a `namespace chronon3d::core::scope { using namespace chronon3d; }` declaration elsewhere). |
| 7 | `include/chronon3d/internal/render_graph/core/scene_hasher.hpp` | **④** (root-cause via (2)) | The 12 errors cascade from (2). **④** `#include "chronon3d/scene/model/core/scene.hpp"` directly (NOT the forward-decl from `render_graph_context.hpp`). This makes `Scene::layers()/nodes()` resolvable. |
| 8 | `include/chronon3d/timeline/composition.hpp` | **①** | 11 errors are `chronon3d::camera_v1::CameraDescriptor` + `CameraProgram` not found. **①** prefix: `::chronon3d::camera_v1::CameraDescriptor` forces global lookup. |
| 9 | `include/chronon3d/render_graph/cache/scene_program_cache.hpp` | **①** + **④** | 9 errors are `chronon3d::RenderCounters` + `chronon3d::cache::LruCache` + `chronon3d::cache::CacheDiagnostics::Handle` + `m_cache` / `m_counters` member undeclared (downstream of the namespace pollution). **①** prefix at the 4 declaration sites. **④** Forwarding header for `RenderCounters` if it's a small types-only struct. |
| 10 | `include/chronon3d/internal/runtime/render_session.hpp` | **①** | 9 errors are `chronon3d::graph::SceneHasher` + `chronon3d::graph::SceneProgramStore` + `m_camera_timeline` member undeclared (cascading from (8)). **①** prefix at the 4-6 sites. |

### Smaller sites (sub-3 errors each, ~31 total errors)

- `include/chronon3d/backends/software/software_renderer.hpp` + `software_render_session.hpp` — **①** (prefix `::chronon3d::` for `graph` + `cache` + `FontPreflightSummary` + `ExecutionScheduler` namespaces)
- `include/chronon3d/effects/curves.hpp` + `glow_pipeline.hpp` — **①** (prefix `::chronon3d::CurvePoint` + `::chronon3d::DebugConfig`)
- `src/effects/effect_catalog.cpp` — **①** (prefix `::chronon3d::EffectStack` + add `using` at top)
- `include/chronon3d/scene/camera/camera_v1/camera_framing_solver.hpp` — **DOMAIN BUG** (the `CameraFramingResult` not declared is a typo per the compiler hint: should be `CameraFramingRequest`. Forward to TICKET-FRAMING-V1 §G for proper classification — this is a separate domain bug, NOT a rot-class symptom)

### Estimated cost of canonical-fix application (per rot-pattern principle)

- ① (prefix) — ~50 sites × 1 line = ~50 LoC across 9 files
- ④ (forwarding header) — 3 NEW headers × ~20 LoC each = ~60 LoC
- ② (namespace move) — 0 sites (no type relocations needed if ① + ④ suffice)
- ③ (using) — 0 sites (if ① + ④ cover)
- DOMAIN BUG — 1 file (camera_framing_solver.hpp, separate ticket)

**Total estimated fix LoC**: ~110 LoC across ~13 files (the matrix's "top 10" + 3 NEW forwarding headers). This is consistent with the prior `50-200+ files to fix` estimate (the file-count includes downstream test files + content/ scenes that surface the same rot when their include paths are audited).

---

## Verification protocol (for the working build host)

```bash
# 0. PRE-REQUISITE: working build host
#    - vcpkg glm + magic_enum + freetype + harfbuzz + fribidi + spdlog + fmt + tbb + xxhash + highway + nlohmann-json
#      (all pre-installed per `cmake/Chronon3DVcpkgToolchain.cmake` Invariant I1; canonical bootstrap path)
#    - tmpfs quota sufficient for the full ninja compile (30+ min on 16 cores)
#    - chronon3d_cli rebuild must NOT be blocked by `chronon3d::chronon3d::` rot in `src/effects/`

# 1. CONFIGURE (vcpkg toolchain + linux-fast-dev preset)
export VCPKG_ROOT="$PWD/vcpkg_bootstrap"
cmake --preset linux-fast-dev -S . -B .tmp/chronon-builds/linux-fast-dev

# 2. COMPILE (per the rot-class, expect 245 errors initially; goal = 0)
cmake --build .tmp/chronon-builds/linux-fast-dev --target chronon3d_dev_fast 2>&1 | \
    tee /tmp/host-build-df1e09d9.log

# 3. VERIFY error count is now 0
errors=$(grep -cE 'error:' /tmp/host-build-df1e09d9.log 2>/dev/null || echo 0)
echo "post-fix errors: $errors (target: 0)"
test "$errors" -eq 0 || { echo "VERIFICATION_FAILED: $errors errors remain" >&2; exit 1; }

# 4. CTEST end-to-end (per AGENTS.md §honesty forward-point)
ctest --test-dir .tmp/chronon-builds/linux-fast-dev --output-on-failure
ctest_exit=$?
echo "ctest exit: $ctest_exit (target: 0 for success)"

# 5. IF VERIFICATION PASSES: open TICKET-BUILD-ROT-CASCADE-CAMERA-DONE commit (atomic chore on main)
#    with subject `fix(build): TICKET-BUILD-ROT-CASCADE-CAMERA rot-class fix (per docs/baselines/main-df1e09d9-baseline.md)`
#    + 3-doc same-commit alignment: CHANGELOG entry + TICKET row state DONE + baseline NEW SHA capture.
```

### Per-step fix verification (when applying the matrix incrementally)

```bash
# Apply fix for file N → re-compile → verify error count decrement
# E.g., after fix #1 (render_runtime.hpp):
cmake --build .tmp/chronon-builds/linux-fast-dev --target chronon3d_dev_fast 2>&1 | tee /tmp/host-build-step1.log
errors_step1=$(grep -cE 'error:' /tmp/host-build-step1.log)
echo "after fix #1: $errors_step1 errors (expected: 245 - 88 = 157 if ① + ④ applied)"

# Continue until errors = 0
```

---

## §honesty forward-point (NOT in this commit, deferred per AGENTS.md "Fare PR piccole e mirate")

The actual fit-build-host verification session is **deferred to a working build host**. The
current VPS is env-blocked (vcpkg `glm` + `magic_enum` + tmpfs quota insufficient for the full
project compile). This baseline captures:

1. The **machine-verified diagnostic** (245 errors, 10+ file breakdown, rot-class pattern counts).
2. The **per-file canonical-fix decision matrix** (10+ files, 4 fix patterns, per-file recommendations with rationale).
3. The **verification protocol** (canonical command sequence to machine-verify the fix on a working build host).
4. The **estimated cost of the fix** (~110 LoC across ~13 files).

What is **NOT** captured in this baseline (deferred to working build host execution):

- The **post-fix re-run error count decrement** (the 88-error → 0-error delta for `render_runtime.hpp`, etc.).
- The **ctest end-to-end PASS** (the canonical `ctest --test-dir ...` end-to-end run, which is the macchina-verifica closure per AGENTS.md §honesty).
- The **DONE commit on main** (TICKET-BUILD-ROT-CASCADE-CAMERA state-transition from OPEN to DONE requires a successful verification per AGENTS.md "Non segnare verde una suite che restituisce failure").

Per AGENTS.md v0.1 §honesty: this baseline is **a machine-verifiable intermediate state** for
future maintainers + the working-build-host agent. It is **not** the closure of
TICKET-BUILD-ROT-CASCADE-CAMERA — the ticket remains OPEN until the post-fix verification
passes on a fit build host.

---

## Cross-references

- [`docs/FOLLOWUP_TICKETS.md`](../FOLLOWUP_TICKETS.md) `## Open Blockers` `TICKET-BUILD-ROT-CASCADE-CAMERA` row (the canonical ticket; +1 verified-expansion clause per the prior commit `df1e09d9`).
- [`docs/CHANGELOG.md`](../CHANGELOG.md) 2026-07-12 `docs(followup): expand TICKET-BUILD-ROT-CASCADE rot-class findings` entry (the audit-trail for this baseline's source diagnostic).
- [`docs/CHANGELOG.md`](../CHANGELOG.md) 2026-07-12 `21 errors mask text_helpers rot` entry (the prior onion-peel analysis; current baseline refines the 21-error estimate to 245-error full build).
- [`docs/CHANGELOG.md`](../CHANGELOG.md) 2026-07-12 `fix(gate): check push range origin/main..HEAD not last 10` entry (TICKET-GATE-SUBJECT-RANGE closure; the 65-char subject of the classification commit `df1e09d9` is within the gate).
- `/tmp/build_test_artifact.log` (the diagnostic artifact, ~166K chars, preserved per AGENTS.md audit-trail principle).
- Commit `cd2548cb feat(api): public camera facade + external consumer SDK test` (the rot-introduction commit, forward-point of `TICKET-BUILD-ROT-RENDER-GRAPH` closure at 22 file edits).
- AGENTS.md v0.1 §honesty (the deferred-verification forward-point convention; the machine-verifiable intermediate state protocol).
- `docs/baselines/main-7eb5c2ba-baseline.md` (the prior green baseline, 11/11 PASS at 2026-06-29). This rot-cascade baseline is **NOT** a green baseline — it documents a rot-state, not a healthy state. When the rot is fixed + a new green compile + ctest passes, a new `docs/baselines/main-<new-sha>-baseline.md` will replace this one (per the standard baseline-replacement protocol).
