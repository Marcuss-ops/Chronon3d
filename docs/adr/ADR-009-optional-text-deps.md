# ADR-009 — Third-Party Text Dependencies (opt-in)

- **Status:** Accepted (WIP — Phases 9 / 11 / 12 prerequisites)
- **Date:** 2026-06-23
- **Deciders:** Core render / text team
- **Tags:** text, dependencies, extension-points, vcpkg

## Context

`docs/TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md` phases 9 (CJK / ICU),
11 (Text 3D via `libtess2`) and 12 (MSDF via `msdfgen`) require
specific, mature algorithms that were identified as the main AE-gap
during the audit on 2026-06-23:

| Phase | Library candidate | Need |
|---|---|---|
| 9 — CJK / Thai / Khmer / Burmese line-breaking | `icu` | Unicode UAX #14/#29 segmentation — graphemes, words, lines, sentences |
| 11 — Text 3D extrusion + bevel | `libtess2` | polygon tessellation with holes (the inside of "O"); without it the hand-rolled ear-clipper must handle multi-contour outlines |
| 12 — MSDF atlas + scale-estreme glow + morph | `msdfgen` | reference industry MSDF generator (Unreal/Unity dependency) |

The CPU-first / headless posture (`AGENTS.md` §Regole di lavoro,
`docs/MIGRATION_TEXT_SPEC.md` §9) is currently enforced as a
**deny-everywhere** deny-list by Gate 5 Check 11 in
`tools/check_architecture_boundaries.sh`:

| Header           | Currently |
| ---------------- | --------- |
| `<msdfgen>`      | DENIED everywhere |
| `<libtess2>`     | DENIED everywhere |
| `<unicode/...>`  | DENIED everywhere |

Hand-rolling the three libraries is years of work and a bug surface
that would always trail the Unicode Consortium spec; for phases
9 / 11 / 12 this is not a sustainable plan. The current
`vcpkg.json` therefore does not contain any of these packages (the
only text dependencies are `freetype`, `harfbuzz`, `fribidi`, `zlib`
under the `text` feature).

## Decision

1. **Approve (opt-in)** the following vcpkg features in `vcpkg.json`:

   - **`icu-layout`** *(profile `global-text`)* — pulls `icu`.
     Powers `IcuBoundaryResolver` for grapheme / word / line /
     sentence segmentation in Phase 9, plus CJK / Thai / Lao / Khmer
     / Burmese line-breaking.
   - **`text-3d`** *(profile `extended`)* — pulls `libtess2`.
     Powers `GlyphMeshBuilder` in Phase 11 — front / back / side
     faces + bevel via CPU tessellation of FreeType outlines.
   - **`text-msdf`** *(profile `extended`)* — pulls `msdfgen`.
     Powers `MsdfGlyphRasterizer` in Phase 12, plus the morph /
     displacement extensions.

2. **Reject `tinyspline`**, `clipper2`, `par_shapes`, `bgfx`,
   `freetype-gl`, `boost::locale`, `skia` for now:

   - `tinyspline` / NURBS — the existing `PathSampler` in
     `src/text/path_sampler.cpp` (De Casteljau + arc-length) already
     covers the Bézier use-case of Phase 5. Adding NURBS is not on
     the roadmap and would breach the "non introdurre una pipeline
     parallela" rule in `docs/TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md`
     §Regole architetturali.
   - `boost::locale` — shells out to libicu anyway and adds a Boost
     layer; upstream ICU is more direct.
   - `skia` / `SkShaper` — pulls the entire Skia tree (MB-scale binary,
     GPU-leaning). Conflicts with CPU-first headless posture and
     duplicates the existing `FontEngine` pipeline.
   - `freetype-gl` — already drive `FreeType` headlessly via
     `src/text/font_engine.cpp`; `freetype-gl` is a GLUT-side helper,
     not applicable here.
   - `clipper2`, `par_shapes`, `bgfx` — clip / morph / GPU helpers
     irrelevant until a future ADR pitches a concrete use-case.

3. **Default OFF.** None of the three new vcpkg features is added to
   `default-features` in `vcpkg.json`. Activation is **per CMake
   preset** via `VCPKG_MANIFEST_FEATURES` — for example,
   `linux-profile-extended` enables `text-3d` + `text-msdf`, and a
   future `linux-profile-motion-icu` (or `global-text`-equivalent)
   enables `icu-layout`. The `linux-profile-core` profile (49
   packages per `docs/ARCHIVE/stabilization-plan/08-dependency-profiles.md`)
   remains untouched.

4. **Scoped Gate-5 exemptions.** Update
   `tools/check_architecture_boundaries.sh` Check 11 to allow the
   new `#include`s **ONLY within their adapter directories**; the
   deny-everywhere posture is preserved everywhere else (CPU-first,
   headless, self-contained text canon). Any future leak trips Gate
   5 and fails the PR:

   | Header             | Allowed adapter path                       |
   | ------------------ | ----------------------------------------- |
   | `<msdfgen/...>`    | `src/text/glyph_raster/`                  |
   | `<libtess2/...>`   | `src/text/text_3d/`                       |
   | `<unicode/...>`    | `src/text/boundary_resolver/`, `src/backends/text/icu_resolver.cpp` |

   Symbols from these libraries must NOT escape their adapters —
   downstream consumers see only:

   - `IGlyphRasterizer*` (msdfgen → interface);
   - `TextBoundaryResolver*` (ICU → interface);
   - `Text3DNode` / `GlyphMesh` (libtess2 → CPU raster).

   This is the same pattern already documented in
   `docs/EXPRESSIONS_V2_PIPELINE.md` and the Phase 9 / Phase 12
   paragraphs of the text roadmap.

## Consequences

- **Positive.** Phases 9 / 11 / 12 become *C++ wrapper work
  around mature libraries* rather than ground-up algorithm work.
  Reduces R&D effort, cuts bug surface, and yields industry-grade
  multilingual / 3D / MSDF quality.
- **Positive.** Compile-time opt-in keeps the headless CPU-first
  core slim (49 vcpkg packages per `linux-profile-core`);
  dev / CI matrices that need advanced text ops use the matching
  profile explicitly.
- **Negative.** Tight adapter discipline is now an ongoing
  invariant. A leaky `#include <msdfgen/...>` outside
  `src/text/glyph_raster/` will fail Gate 5 on the PR.
- **Negative.** Adoption expands binaries; tracked via
  `tools/measure_profile.sh` and reported in
  `docs/ARCHIVE/stabilization-plan/08-dependency-profiles.md`.
- **Neutral.** Existing `text` feature (`freetype`, `harfbuzz`,
  `fribidi`, `zlib`) and Blend2D / `mesh` feature are unaffected.

## Alternatives considered

- **Hand-rolled ICU surrogates + `libtess2` clones.** Rejected:
  out-of-scope for the P0 → first-milestone timeline; would
  always trail Unicode Consortium releases and lack the
  industry-tested colour-emoji fallback.
- **Skia module (`SkShaper`).** Rejected: pulls entire Skia tree
  (multi-MB binary, GPU-leaning); integrates poorly with our
  CPU-first pipeline and would duplicate `FontEngine` + `Placed`.
- **`boost::locale`.** Rejected in favour of upstream ICU — adds a
  Boost layer for no functional gain.
- **`freetype-gl`.** Rejected — only applicable to GLUT contexts;
  we already drive `FreeType` via `src/text/font_engine.cpp`.

## Open follow-up gaps (tracked, not blocking this PR)

- **CMakePresets wiring.** Decision #3 mandates activation via
  `VCPKG_MANIFEST_FEATURES` per preset. As of this commit, no preset
  in `CMakePresets.json` adds the new features to
  `VCPKG_MANIFEST_FEATURES`. Tracked as a separate follow-up PR
  mirroring the precedent in
  `docs/ARCHIVE/stabilization-plan/08-dependency-profiles.md` (work item
  "Allineare i preset non-profile ... rendere esplicite tutte le
  feature in ogni preset"). Until that lands, the three features are
  documented and vcpkg-installable, but no consumer of `vcpkg.json`
  will activate them at configure time — opt-in by definition.
- **CMake target definitions.** `CHRONON3D_USE_ICU`,
  `CHRONON3D_ENABLE_TEXT_3D`, `CHRONON3D_ENABLE_TEXT_MSDF` are
  referenced in `vcpkg.json` descriptions and the roadmap but not
  yet declared in root `CMakeLists.txt` / `cmake/`. Tracked as
  separate scope per `MIGRATION_TEXT_SPEC.md §1` (canonical
  contract for opt-in flags).
- **Visual Regression Harness fixtures.** The 16 scenario fixtures
  listed in `docs/TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md` Phase 2
  do not yet include CJK / emoji / 3D / MSDF scenarios. Tracked in
  Phase 2.

## References

- `docs/TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md` (Phases 9, 11, 12 + §Feature opzionali).
- `docs/MIGRATION_TEXT_SPEC.md` §9 — Boundary enforcement, ADR precedent.
- `AGENTS.md` §Regole di lavoro — ADR-first deroga rule.
- `tools/check_architecture_boundaries.sh` (Check 11).
- `vcpkg.json`.
- `docs/ARCHIVE/stabilization-plan/08-dependency-profiles.md` — profile matrix
  + `default-features` policy.
- `src/text/path_sampler.cpp` — canonical PathSampler (Phase 5);
  reason for rejecting `tinyspline`.
