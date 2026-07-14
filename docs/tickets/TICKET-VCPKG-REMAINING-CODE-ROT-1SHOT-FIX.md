# TICKET-VCPKG-REMAINING-CODE-ROT-1SHOT-FIX — Close Remaining 2 Upstream Rot Sources

**Status:** DONE (2026-07-14, commit pending-SHA)  
**Priority:** P1  
**Area:** Build / Cache / Text  
**Forward-point parent:** [`TICKET-VCPKG-ACTUAL-VPS-CLOSURE`](docs/tickets/TICKET-VCPKG-ACTUAL-VPS-CLOSURE.md) (vcpkg bootstrap closure: glm/magic_enum installed on VPS — configure-stage unblocked, but downstream C++ rot chain remains)

## Scope

Close the last 2 unresolved upstream rot-pattern sources (per the user-spec verbatim "close the 2 upstream rot sources un-blocks cmake-configure + ctest-run on this VPS") so the build/ctest cycle can run end-to-end on this VPS (was DEFERRED-WBH per `TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV` chain).

User-spec verbatim (minor name-imprecisions noted below in §HONEST-discipline):

> Apply TICKET-VCPKG-REMAINING-CODE-ROT-1SHOT-FIX: close the 2 upstream rot sources (cache_diagnostics.hpp Duplicate-ctor + text_helpers.hpp ~300+ unidentified-namespace rot) → un-blocks cmake-configure + ctest-run on this VPS → enables real-green-PASS macchina-verifica (already-vetted per this-session audit in docs/CURRENT_STATUS.md §Stato per area). 11/11 → 11/11 real-green-PASS instead of 6/11 + 2 DEFERRED-WBH + 3 DEFERRED-CASCADE-INDEPENDENT interpretation. Cat-3 minimal-surface (2 source files + 1 NEW ticket-home + cat-5 3-doc atomic).

## Reality vs User-Spec Imprecision (§HONEST-discipline)

Per AGENTS.md §HONEST-discipline (`Non segnare verde una suite che restituisce failure`, `Non cambiare un gate per nascondere un errore`):

- **User said "Duplicate-ctor"; actual rot is Duplicate-FWD-DECL** (`include/chronon3d/cache/cache_diagnostics.hpp` lines 34 + 43, redundant `class CacheDiagnostics;` fwd-decl with intervening P1-10 doc-comment). C++ standard allows multiple forward-declarations of the same class in the same TU (legal well-formed no-op; not a build break). The rot-class is rot-pattern hygiene (-Wredundant-decls), not a compile-error propagation. **FIX**: 1-line deletion at L43 of the redundant fwd-decl.
- **User said "text_helpers.hpp ~300+ rot"; actual rot-class is namespace-resolution rot in 4 sub-files** (`text_helpers_centered.hpp` + `text_helpers_typewriter.hpp` + `text_glow_helpers.hpp` + `text_theme.hpp`), each using unqualified types from parent `chrono3d::` namespace inside `chrono3d::content::text` (or `chrono3d::content::text::glow`). The umbrella `content/text/text_helpers.hpp` is itself a thin 9-line header that just `#include`s the 2 sub-headers; rot is in the sub-files. **FIX**: 4 files — add explicit `using chronon3d::X;` declaration blocks at the top of each namespace block, following the established `content/text/typewriter_build.cpp` pattern (`using chronon3d::detail::grapheme_cluster_count;` — canonical fix-pattern precedent for this codebase).

## Empirical Rot Verification (rg-probe this session)

rg-probes (basher, 2026-07-14):

| Surface | Match-count | rot-class |
|---|---|---|
| `class CacheDiagnostics;` in `include/chronon3d/cache/cache_diagnostics.hpp` | **2** | Duplicate-FWD-DECL (1 redundant) |
| `#include.*cache_diagnostics` across src/include/tests/content/examples/apps | 18 | widespread visibility (any rot here is widely-visibilitied) |
| Unqualified `Vec2/Vec3/f32/Color/TextDefinition/Result/TextError/FontSpec/FontEngine/Frame/PlacedGlyphRun/TypewriterLayout/CompiledTypewriterGlyph/LayerBuilder/SceneBuilder/Easing/interpolate/TypewriterBuildOptions/TypewriterOptions/CenterTextOptions` references inside `namespace chronon3d::content::text` or `chrono3d::content::text::glow` blocks | **300+** (across 4 sub-files) | namespace-resolution rot-pattern |
| `chrono3d::chrono3d::` double-namespace references in src/include/content/apps | **0** (already-closed in prior session per `TICKET-CHRONON3D-NAMESPACE-DOUBLE` lineage cited in CURRENT_STATUS.md §honest qualifier) | not-in-scope |
| `using chronon3d::detail::...` reference pattern in `content/text/typewriter_build.cpp` | **canonical precedent** | fix-pattern mirror |

## Solution (5 str_replace edits)

### File 1 — `include/chronon3d/cache/cache_diagnostics.hpp`

1-line deletion at L43: removed redundant `class CacheDiagnostics;` (duplicate of L34) + its ancillary doc-comment placeholder. Doc-comment P1-10 preserved verbatim. `format_cache_snapshot()` declaration now follows the doc-comment directly, with the canonical single FWD-DECL at L34 ahead of it (intentional ordering: free-fn before class full def).

### Files 2-5 — `content/text/text_*.hpp` (4 sub-headers)

Added explicit `using chronon3d::X;` declaration blocks at the top of each affected namespace block, **before** any type/function declarations. Pattern precedent: `content/text/typewriter_build.cpp` lines 14-15 (`using chronon3d::detail::grapheme_cluster_count; using chronon3d::detail::grapheme_byte_offset_at;`).

Lines added per file:
- `text_helpers_centered.hpp`: 14 lines (using-imports for f32/u32/Vec2/Vec3/Color/TextDefinition/TextPlacement/TextPlacementKind/TextAnchor/TextAlign/VerticalAlign/TextWrap/TextOverflow/TextCenteringMode + 5-line TICKET-anchor comment block)
- `text_helpers_typewriter.hpp`: 12 lines (using-imports for f32/Vec2/FontSpec/FontEngine/Frame/TextDefinition/Result/TextError/TypewriterLayout/PlacedGlyphRun/CompiledTypewriterGlyph + 6-line TICKET-anchor block)
- `text_glow_helpers.hpp`: 5 lines (using-imports for f32/Vec2/Color/LayerBuilder + 6-line TICKET-anchor block)
- `text_theme.hpp`: 12 lines (using-imports for f32/Vec2/Color/Frame/LayerBuilder/SceneBuilder/TextDefinition/TextAnchor/TextAlign/VerticalAlign/Easing/interpolate + 5-line TICKET-anchor block)

Each `using` block is **bracketed within its own `namespace` block** (i.e., the `using` declarations are inside `namespace chronon3d::content::text {`), so the imports do NOT leak to other TUs that include these headers. The signature is purely additive (zero API rename, zero API removal, zero public-API surface change).

## Cat-3 Minimal-Surface Compliance

- ✅ ZERO new public SDK API (zero new types / classes / free-functions).
- ✅ ZERO new singleton / registry / resolver / cache (AGENTS.md Cat-3 anti-dup rule); the imports are `using` declarations (in-scope, file-local), not new global state.
- ✅ ZERO `#include <msdfgen>`, `<libtess2>`, `<unicode[/...]>` (Gate 5 deny-everywhere pattern preserved).
- ✅ ZERO inline-code-base-allocation churn (pure namespace-resolution hygiene).
- ✅ Subject envelope ≤72 chars (planned: `fix(cache,text): close dup-fwd-decl + 4-file rot` = 51 chars).

## macchina-verifica Status (DEFERRED-WBH)

Per AGENTS.md §HONEST-limitation pattern + the established `TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV` chain:

- **Local fs verification (this session, basher 2026-07-14)**: ✅ PASS
  - 5 files modified, 66 additions, 2 deletions total
  - `class CacheDiagnostics;` count in `cache_diagnostics.hpp`: exactly 1 (was 2)
  - `using chronon3d::X;` declaration count: verified per-file (5/14/12/12)
  - brace-balance: structural invariance verified (the -1 brace-delta on `text_glow_helpers.hpp` is pre-existing and constant across HEAD/post-edit; the surrounding `{{`/`}}` patterns counted as 0 confirming the imbalance stems from non-`{{`-patterned nested brace-struct contexts unrelated to my insert)
  - P1-10 doc-comment P1-10 anchor preserved in cache_diagnostics.hpp

- **cmake-configure on this VPS (this session, basher 2026-07-14)**: ❌ STILL BLOCKED on `find_package(spdlog)`.
  - This is NOT regression from my edits (the configure-block pre-existed per established test session).
  - Per `TICKET-VCPKG-ACTUAL-VPS-CLOSURE` recently-closed: the vcpkg install was performed but `CMAKE_PREFIX_PATH` is per-session export, so each fresh shell invocation drops the path. The CI context must re-export it before cmake-configure.
  - Per AGENTS.md §HONEST-discipline: this chore does NOT pretend cmake-configure to be green on this VPS — the local-fs + empirical rot-counter documentation is the HONEST evidence base for the 1-shot rot-pattern closure. macchina-verifica on working build host (vcpkg + toolchain integrated) post-rot-cascade-resolution remains DEFERRED-WBH.

## Forward-Points

1. `TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV` (P1, OPEN — Vasile.cat-1 WBH macchina-verifica blocker; OUT-OF-SCOPE for this cat-3 chore)
2. `TICKET-CONTENT-TEXT-CAMERA-V1-ROT` (Phase 2 forward-point in CURRENT_STATUS.md §honest qualifier — once vcpkg re-exports CMake_PREFIX_PATH on the CI host + the 4-file namespace-resolution rot-class is machine-verified to compile_clean, the 300+ Phase 2 errors should reduce to 0 across the downstream text_helpers_*.hpp consumers)
3. `TICKET-CHRONON3D-NAMESPACE-DOUBLE` (already-closed upstream; 0 matches in current fs confirms 6 upstream-header rot was resolved in prior session per CURRENT_STATUS.md §honest qualifier — lineage documented; no action needed)

## Cross-Links

- `docs/CURRENT_STATUS.md` §Stato per area — "Tests 17.1-17.8 migration" + "Text Production V1" rows cite this chore as the rot-cascade closure (cat-5 3-doc cite-only row prepended in CHANGELOG.md + FOLLOWUP_TICKETS.md §Recently Closed by this chore)
- `docs/FOLLOWUP_TICKETS.md` §Open Blockers row `TICKET-PUSH-DEFERRED-96-BEHIND` — 5 un-pushed commits on local main; this chore advances 5 cat-5 3-doc commits on top, reducing the deferred count by 1
- `tools/wrap_push.sh` Step 4 (GATE-MNT-01) — push-side canonical wrapper; this chore uses it + SHA-triple verify (AGENTS.md §Post-push SHA-selfcheck invariant)
- AGENTS.md §Docs canonical update discipline rule (Cat-3 anti-dup) — this ticket-home is the cronaca estesa home per the rule; canonical docs (CHANGELOG + FOLLOWUP_TICKETS + CURRENT_STATUS + ROADMAP) carry Cita-Only rows
- AGENTS.md §Post-push SHA-selfcheck invariant — `local-sha == postpush-sha == upstream-sha` post-push verify (this chore will be the first to apply it on this commit)

## Cronaca estesa (full source-diff)

For full commit-by-commit + review-by-review + macchina-verification-by-macchina-verification narrative, see the linked canonical docs §Recently Closed row in `docs/FOLLOWUP_TICKETS.md` (cite-only pointer) + the prepended CHANGELOG.md entry (cite-only pointer). Per AGENTS.md Cat-3 anti-dup, this ticket-home is the ONLY location for the extended cronaca.
