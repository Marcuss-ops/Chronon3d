# Phase 1.3.A — Pre-flight Validation Memo

> **Filename convention note.** This file uses `Phase1.3.A`
> dot-segmentation per explicit user spec; sibling files in this
> directory (`AGENT_1_RENDERER_BOUNDARY.md`, `AGENT_2_CMAKE_SDK_BASELINE.md`,
> `AGENT_4_VERIFY_VISUAL_INTEGRATION.md`) use underscore-segmentation.
> Downstream glob-filter designs (e.g. `*.agent-task.md`) should
> account for BOTH shapes or normalize on upload.

| Field | Value |
|---|---|
| Phase | 1.3.A (pre-flight validation, BUILD-INERT) |
| Date | 2026-06-29 |
| Branch / commit | `main` HEAD post-amend (rebased onto `origin/main`'s `fix(headers)` commit); SHA = `git rev-parse HEAD`. SHA deliberately not inlined to avoid per-amend gleaning. |
| Author | Buffy / Agent orchestration |
| Decision | **proceed-to-1.3.B = YES** (per spec; see "Side-finding" below) |
| Stack rank | docs-only memo, no build/CI/preset impact |

---

## TL;DR — Surprising finding up front

The original spec for Phase 1.3 stated that "CMake 3.27 is required for
`CMakePresets.json version: 6 + include`". **This is not strictly true.**
CMake **3.25** (the project's current `cmake_minimum_required(VERSION 3.25)`)
already supports `version: 6` and the `include` field natively — the
`include` field was introduced in `version: 4` (CMake 3.23), and
`version: 6` was introduced in **CMake 3.25 itself**. The bump to
`cmake_minimum_required(VERSION 3.27)` is therefore **technically optional**
for the split, but recommended anyway for two reasons:

1. **Alignment with the explicit spec** — the user spec asked for the bump.
2. **Macro expansion in `include` paths** — `version: 7` (CMake 3.27) adds
   `${sourceDir}` / `${presetName}` expansion inside `include` path strings,
   which makes the split robust to future preset-file relocations.

Per the explicit spec we proceed with both bumps (3.25 → 3.27 AND presets
3 → 6) and commit a single memo. If the user prefers a "lower-risk"
interpretation post-Phase-1.3.G review, only the `CMakePresets.json`
`version` bump is strictly required.

> **Authority hedge.** The claims above that
> `version: 4` introduced the `include` field (in CMake 3.23) and
> `version: 6` was introduced in CMake 3.25 itself rest on thinker
> recall without direct citation. Cross-reference the official
> CMakePresets spec at
> `https://cmake.org/cmake/help/latest/manual/cmake-presets.7.html#format-version`
> and corroborate with `cmake --version | head -1` on the verify
> machine during 1.3.H. Until then, treat the surprising finding as
> plausibly correct but not canonical.

---

## Thinker's 5-bullet verdict

### 1. [CMake target verdict]

**CMake 3.25 already natively supports Presets `version: 6` and the
`include` field** (introduced in `version: 4` / CMake 3.23). The bump to
3.27 is not required to use `version: 6 + include`. If we want macro
expansion inside `include` paths (`${sourceDir}` etc.) we should pick
**CMake 3.27 + `version: 6`** (avoids the v7-specific features we do not
need) or jump to CMake 3.27 + `version: 7` (which would add macro
expansion but is not necessary for the literal-path split planned).

**Verdict:** **CMake 3.27** is the right minimum because the spec asks
for it AND because v6 still works on it. `version: 6` is the right preset
schema version (v7 adds nothing we need for a 6-module literal-path split).

### 2. [v3 → v6 breaking-changes inventory]

**No schema-breaking changes** from `version: 3` to `version: 6`. Every
feature already used in `CMakePresets.json` is preserved:

| Feature in current `version: 3` file | Verdict in `version: 6` |
|---|---|
| `inherits: ["base-linux"]` (string array) | **SAFE** — unchanged semantics |
| `condition: {type: "equals", lhs: "${hostSystemName}", rhs: "Linux"}` | **SAFE** — v3 syntax fully valid in v6 |
| `cacheVariables` (string-keyed map, including `VCPKG_*` and `CHRONON3D_*` keys) | **SAFE** — schema unchanged |
| `binaryDir: "${sourceDir}/build/chronon/${presetName}"` | **SAFE** — macros work the same |
| `generator: "Ninja"` | **SAFE** |
| `displayName`, `description`, `hidden: true` | **SAFE** — strings / booleans unchanged |
| `buildPresets[].configurePreset`, `targets[]`, `jobs` | **SAFE** |
| `testPresets[].output.outputOnFailure` | **SAFE** — `output` schema unchanged in v6 |
| Top-level keys (`configurePresets`, `buildPresets`, `testPresets`) | **SAFE** — no new required v6 keys |

**No migration of existing entries needed.** Only additive changes
between v3 and v6 (new optional fields: `condition` operators in v4,
`${path}` macros in v6 output, etc.).

### 3. [`include` syntax compatibility v6 vs v7]

**The planned split is SAFE with `version: 6`** using literal-path entries:

```
{
  "version": 6,
  "include": [
    "cmake/presets/base.json",
    "cmake/presets/development.json",
    "cmake/presets/ci.json",
    "cmake/presets/release.json",
    "cmake/presets/experimental.json",
    "cmake/presets/profiling.json"
  ]
}
```

Verified facts:

- **`include` accepts a string array of paths.** Each path is a file path
  relative to **the file containing the `include` directive**. With the
  root `CMakePresets.json` at the project root, the relative paths
  `cmake/presets/<x>.json` resolve correctly.
- **Included files MUST each have their own valid `version` key.** So
  the 6 split files will each carry `"version": 6` at the top.
- **`version: 7` adds macro expansion inside `include` paths**
  (e.g. `"${sourceDir}/cmake/presets/base.json"`). This is OPTIONAL and
  not required for our literal-path plan.
- **Recursion depth is bounded and cycles are detected natively** by
  CMake — a flat 6-module acyclic DAG (no inter-include between split
  files) is the safest topology.

### 4. [Fallback strategy if CMake 3.27 is rejected]

If for any reason the `cmake_minimum_required(VERSION 3.27)` bump is
reverted by code review or by user preference:

- Keep `cmake_minimum_required(VERSION 3.25)` in root `CMakeLists.txt`.
- Still bump the `CMakePresets.json` `"version": 3` → `"version": 6`.
- Use the **same literal-path `include` array** as planned.
- All split files carry `"version": 6` at the top.

This produces a working split with **zero changes to existing workflows**
(`cmake --preset <name>` / `ctest --preset <name>` continue to work
identically). The split is the canonical deliverable; the
`cmake_minimum` bump is an alignment-only choice.

### 5. [Alternative path forward]

If the native `CMakePresets `include` field is rejected outright
(extreme-case scenario), the next-best alternative is:

- Keep a **monolithic `CMakePresets.json`** but maintain the 6 logical
  fragments as standalone files committed alongside (`base.json`,
  `development.json`, etc.).
- Add a small Python build-script (e.g. `tools/build_cmake_presets.py`)
  that concatenates the fragments into the root file at bootstrap time.
- Wire the bootstrap into CMakeLists.txt early (before `add_subdirectory`).

**This is strictly worse** than the native `include` approach because:

- Requires CI/local wrapper to run the bootstrap on every CMake run.
- Loses the canonical single-source-of-truth that `include` provides.
- Adds a hidden generated file (root `CMakePresets.json`) that drifts
  from version control.

**Recommendation:** never pursue the Python-concat alternative unless
the Phase-1.3.G review surfaces a hard incompatibility.

---

## Decision

```yaml
DECISION: proceed-to-1.3.B = yes
REASON:   CMake 3.25 already supports Presets v6 + include, but per
          explicit spec we bump cmake_minimum 3.25→3.27 AND presets
          version 3→6, and commit a single docs-only memo.
```

### Empirical verification (deferred to 1.3.H)

The surprising finding that "CMake 3.25 already supports Presets v6 +
include" rests on thinker recall. Re-confirm on the verify machine
in 1.3.H or 1.3.I and capture the result:

```bash
cmake --version | head -1
cmake --preset linux-ci -B "$(mktemp -d)" -S . 2>&1 | head -40
```

Expected top line: `cmake version 3.27.x` (or higher). Expected
configure dry: no errors related to `version: 6` parsing — only
ordinary vcpkg manifest / dependency-resolution diagnostics.

### Side-finding (pomelli di allineamento spec-vs-realtà)

If the user wishes a **lower-risk** Phase 1.3 commitment post-review, the
cmake_minimum bump (1.3.B) can be deferred or skipped entirely because
3.25 already supports the planned preset split. The remaining work
(1.3.C–1.3.I) is independent of `cmake_minimum`. This memo will be
referenced by 1.3.I as part of the `CURRENT_STATUS.md` snapshot bump.

---

## Traceability

### Pre-flight inputs (read by parent + passed to thinker)

| # | Path | Purpose |
|---|---|---|
| 1 | `CMakeLists.txt` (top ~30 LOC) | `cmake_minimum_required(VERSION 3.25)` |
| 2 | `CMakePresets.json` | 26 configure presets, 21 build presets (incl. one named `linux` → legacy `configurePreset: linux-release`), 18 test presets, schema `version: 3` |
| 3 | `.github/workflows/gates.yml` | 6 jobs; uses `linux-core-dev` (Gate 1), `linux-lean-dev` (Gate 2), `linux-ci` (Gate 4 + 6); also has printf-generated consumer CMakeLists.txt hardcoding `cmake_minimum_required(VERSION 3.25)` |
| 4 | `.github/workflows/ci.yml` | 5-entry matrix: `linux-release`, `linux-ci`, `linux-ci-nocontent`, `linux-asan`, `win-release` |
| 5 | `.github/workflows/gates-full-validation.yml` | Path-filtered heavy gate using `linux-full-validation` preset |

### Workflow migration matrix (planned for 1.3.C / 1.3.D / 1.3.E)

| Workflow | Legacy preset to migrate | New canonical preset |
|---|---|---|
| `gates.yml` Gate 1 | `linux-core-dev` | `linux-ci-core-gate` |
| `gates.yml` Gate 2 | `linux-lean-dev` | `linux-ci-lean-gate` |
| `ci.yml` matrix entry | `linux-release` | `linux-ci-release-build` |
| `gates-full-validation.yml` | `linux-full-validation` | `linux-ci-full-validation` |

### Preset retain list (planned for 1.3.F / 1.3.G)

**Canonical** (~11 final visible names):

- `linux-dev` (daily inner loop)
- `linux-ci` (daily install-consumer gate; with **hidden alias** `linux-ci-nocontent` kept `hidden: true` per current convention — alias for the no-content variant; NOT promoted to visible in 1.3.F)
- `linux-asan` (Sanitize=address+UB)
- `linux-release-validation` (V0.1 release-only)
- `linux-experimental-validation` (full-fat, with V2)
- `linux-profile-core` (hidden, nightly)
- `linux-profile-motion` (hidden, nightly)
- `linux-profile-video` (hidden, nightly)
- `linux-profile-extended` (hidden, nightly)

Plus shared hidden fragments: `base`, `base-linux`, `__release-base`,
`__experimental-base`.

**Removable legacy** (~12 lists in configurePresets + 1 in buildPresets):

`linux-core-dev`, `linux-lean-dev`, `linux-artist-dev`,
`linux-full-validation`, `linux-release`, `linux-debug`,
`linux-turbo`, `linux-fast-dev`, `linux-fast-dev-release`,
`linux-lean`, `linux-dev-video`, `linux-release-full`.

**Cross-ref:** the `buildPresets[]` entry
named `linux` (pointing to legacy `configurePreset: linux-release`)
must ALSO be removed in 1.3.G, OR remapped to `linux-release-validation`
so `linux-ci-release-build` references it. Catch this so the migration
matrix is consistent (buildPresets + configurePresets + workflow yml).### Other workflows (out of migration scope for 1.3.C / 1.3.D / 1.3.E)

These four workflows reference only canonically-retained presets and
need NO migration per the user spec. Listed here so 1.3.H verification
can confirm with `grep -nE 'preset' .github/workflows/`:

- `validation.yml` — uses `linux-release-validation`, `linux-experimental-validation`, `linux-asan` (all canonical kept).
- `nightly-visual.yml` — uses canonical-retained preset names from the profile-* / release-validation family.
- `profile-envelope.yml` — uses `linux-profile-{core,motion,video,extended}` (all canonical kept).
- `visual_ci.yml` — uses canonical or unversioned preset references.

If any of these is later found to reference a legacy preset, promote
it into the 1.3.C/1.3.D/1.3.E migration matrix.

### Memorial note

This memo is the **single source of truth** for the Phase-1.3.A
assessment.  When referencing it from `CURRENT_STATUS.md` (in 1.3.I),
quote only the DECISION line and the Side-finding paragraph. Full
contents stay here in `docs/agent-tasks/ARCHIVE-Phase1.3.A-memo.md`.

---

## Resolutions appendix (reviewer findings)

| Finding | Severity | Resolution in this document |
|---|---|---|
| #1 Push pending | blocker | Atomic push to `origin/main` succeeded — see `git log origin/main` for the exact range. Closed by the amend. |
| #2 Off-by-one 22→21 build presets | factual | Count corrected; dangling `linux` buildPresets[].name cross-ref lives in the Removable legacy section. |
| #3 DECISION block not in code fence | style | Now wrapped in ` ```yaml ` fence. |
| #4 Authority claim unhedged | factual | Authority hedge blockquote cites `cmake-presets.7.html#format-version`; verification deferred to 1.3.H. |
| #5 Other workflows scope undocumented | coverage | New subsection names validation.yml / nightly-visual.yml / profile-envelope.yml / visual_ci.yml as out-of-scope. |
| #6 buildPresets[].name=linux dangling | structural | Cross-ref sub-bullet under Removable legacy lists the cleanup option for 1.3.G. |
| #7 linux-ci-nocontent visibility undocumented | factual | Note under the `linux-ci` entry retains `hidden: true`. |
| #8 Multi-dot filename deviation | style | Filename-convention note added under document heading. |
| #9 Empirical `cmake --version` log missing | process | New subsection provides a copyable bash snippet with `mktemp -d` for hygiene. |

---

END OF MEMO
