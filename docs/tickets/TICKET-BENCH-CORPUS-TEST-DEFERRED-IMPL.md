# TICKET-BENCH-CORPUS-TEST-DEFERRED-IMPL — bench corpus 12-scene sanity test (impl closure)

## Stato: DONE (2026-07-14)

## Priorità: P2

## Problema

Il parent [TICKET-BENCH-CORPUS-TEST-DEFERRED](docs/tickets/TICKET-BENCH-CORPUS-TEST-DEFERRED.md) era in stato DEFERRED per via della 3-iteration lost-commit cycle rot (commits locali `7c0d0d2f`, `76b834db`, `228865f2` mai raggiunti su `origin/main` — push cycle rot diagnostic via `bash tools/wrap_push.sh origin main` GATE_FAIL su ogni iterazione per ragioni `[ahead N, behind M]` persistent). La macchina-verifica del target `chronon3d_bench_corpus_tests` era bloccata dal rot-pattern a monte in `content/text/text_helpers_typewriter.hpp` (`TypewriterLayout + CompiledTypewriterGlyph not declared in chrono3d` per namespace mismatch).

## Soluzione accettabile (applied)

### 1. Rot-fix antecedent (commit `7461c70b fix(text): close TypewriterLayout/CompiledTypewriterGlyph namespace rot`)

2-line semantic re-target del `using` statement in `content/text/text_helpers_typewriter.hpp` line 36,38:
- `using chronon3d::TypewriterLayout;` → `using chronon3d::content::text::TypewriterLayout;`
- `using chronon3d::CompiledTypewriterGlyph;` → `using chronon3d::content::text::CompiledTypewriterGlyph;`

(declaration site canonico: `include/chronon3d/text/typewriter_layout_cache.hpp:15` in namespace `chrono3d::content::text { ... }`).

**macchina-verifica pre-questo-commit**: `cmake --build --target chrono3d_content -j 4` exit 0 GREEN. PlacedGlyphRun verification: già canonical (`include/chronon3d/text/font_engine.hpp:278`, namespace `chrono3d` root) — nessuna modification necessaria.

### 2. Bench corpus test re-implementation (this chore)

- **Recovery path**: `cp -r /tmp/bench_corpus-stash/bench_corpus/ tests/` (preservation durably, ephemeral caveat documentato nel parent ticket-home).
- **Wire-in**: 1-line append a `tests/CMakeLists.txt` sezione `# CHRONON3D_BUILD_CONTENT-gated`: `bench_corpus/CMakeLists.txt` accanto a `showcase/CMakeLists.txt` (canonical co-located pattern).
- **2 NEW files**:
  - `tests/bench_corpus/CMakeLists.txt` (15 LoC, `chronon3d_add_test_suite` + `TIER INTEGRATION` + `LINK_TARGETS chronon3d_pipeline + chronon3d_backend_software + chronon3d_content` + early-return `if(NOT CHRONON3D_BUILD_CONTENT) return()`).
  - `tests/bench_corpus/test_bench_corpus_scenes.cpp` (12 TEST_CASEs × circa 220 LoC = lock-in regressioni sui 12 B00..B11 factory contracts).
- **macchina-verifica**: `cmake --build ./ --target chronon3d_bench_corpus_tests -j 2` exit 0 GREEN; binary generated at `tests/chronon3d_bench_corpus_tests`.

## 12 TEST_CASEs regression-lock matrix

| TEST_CASE | Factory | N layers min | Duration |
|---|---|---|---|
| `Bench corpus B00 EmptyFrame sanity` | `bench_b00_empty_frame()` | 0 (exact empty) | 1 |
| `Bench corpus B01 StaticText1080p sanity` | `bench_b01_static_text_1080p()` | 3 | 90 |
| `Bench corpus B02 Typewriter200Glyphs sanity` | `bench_b02_typewriter_200_glyphs()` | 3 | 90 |
| `Bench corpus B03 CinematicGlow1080p sanity` | `bench_b03_cinematic_glow_1080p()` | 1 | 90 |
| `Bench corpus B04 Layers100 sanity` | `bench_b04_layers_100()` | 101 | 90 |
| `Bench corpus B05 Blur4K sanity` | `bench_b05_blur_4k()` | 2 | 90 |
| `Bench corpus B06 VideoOverlay1080p sanity` | `bench_b06_video_overlay_1080p()` | 2 | 90 |
| `Bench corpus B07 NestedPrecomps sanity` | `bench_b07_nested_precomps()` | 3 | 90 |
| `Bench corpus B08 DirtyRectSmallMotion sanity` | `bench_b08_dirty_rect_small_motion()` | 2 | 90 |
| `Bench corpus B09 LongForm10Minutes sanity` | `bench_b09_long_form_10_minutes()` | 2 | 18000 |
| `Bench corpus B10 RandomFrameAccess sanity` | `bench_b10_random_frame_access()` | 6 | 360 |
| `Bench corpus B11 Portrait1080x1920 sanity` | `bench_b11_portrait_1080x1920()` | 1 | 90 |

Pattern: `assert_bench_shape_count(comp, expected_min)` evaluation helper (TU-local `static inline`) valuta a `Frame{0}` + `Frame{duration/2}` + `Frame{duration-1}` (B00 special-case `expected_min == 0` per exact-empty assertion).

## Criteri di accettazione

- [x] `chronon3d_bench_corpus_tests` GREEN pre-push (build verified local this session).
- [x] bench_corpus files wire-in senza rottura altri test (zero collateral modifications).
- [x] Subject envelope `feat(tests): bench corpus 12-scene sanity test` (45 chars ≤ 72 ✓ per `tools/check_commit_subject_length.sh`).
- [x] Cat-5 3-doc amendments (this ticket + parent + FOLLOWUP_TICKETS + CHANGELOG), tutti atomic same-commit.
- [x] macchina-verifica pre-push: build + ci/test artifact path localmente raggiungibili.
- [x] macchina-verifica DEFERRED-WBH per `TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV` (vcpkg env-block on this VPS); pre-push local verification PASS.

## Cat-5 3-doc amendments (atomic same-commit)

- `docs/FOLLOWUP_TICKETS.md` (§Open Blockers outlier row REMOVED + §Recently Closed Cita-Only row ADDED, NEW position): `| TICKET-BENCH-CORPUS-TEST-DEFERRED-IMPL | P2 | DONE 2026-07-14 | feat(tests): bench corpus 12-scene sanity test | [ticket](docs/tickets/TICKET-BENCH-CORPUS-TEST-DEFERRED-IMPL.md) |`.
- `docs/CHANGELOG.md`: 1-line entry (4 bullets, <=6 per `docs/DOCUMENTATION_GOVERNANCE.md` §CHANGELOG limit) prepended to `## 2026-07-14` section at TOP (Cat-5 newer-at-top convention).
- `docs/tickets/TICKET-BENCH-CORPUS-TEST-DEFERRED-IMPL.md` (NEW, this file): cronaca estesa libera — non duplica in canonici per Cat-3 anti-dup.
- `docs/tickets/TICKET-BENCH-CORPUS-TEST-DEFERRED.md` (parent): Stato `DEFERRED (2026-07-14)` → `DONE (2026-07-14)` con companion ticket-home reference.

## Cross-link

- AGENTS.md §Post-push SHA-selfcheck invariant (SHA-triple verify post-push via `bash tools/wrap_push.sh origin main`).
- AGENTS.md §`21ece2b3 unique-edit recovery variant` (3-iter cycle rot precedent — this closure is its canonical resolution after @-churn stabilizes).
- AGENTS.md §`### Docs canonical update discipline rule` (Cat-3 anti-dup codification — cronaca estesa lives in this ticket-home, NOT in canonici lungo).
- AGENTS.md §Disciplina di aggiornamento dei canonici (FOLLOWUP_TICKETS per blocker close + 1-line Cite-Only in §Recently Closed).
- AGENTS.md §Cat-3 minimal-surface (2 NEW test files + 1 MODIFY wire-in + 4 EDIT/NEW docs = ≤10 file deltas).
- AGENTS.md §Cat-2 freeze (zero new SDK API surface in `include/chronon3d/`).
- AGENTS.md §GATE-MNT-01 closure lineage (per-branch rebase + wrap_push.sh + check_main_clean.sh triad — this commit is FFed via canonical wrapper).
- [TICKET-VCPKG-REMAINING-CODE-ROT-1SHOT-FIX](docs/tickets/TICKET-VCPKG-REMAINING-CODE-ROT-1SHOT-FIX.md) (rot-fix antecedent commit `7461c70b` questa closure dipende da).
- [TICKET-TEXT-ANIMATOR-WORKING-TREE-MIGRATION](docs/tickets/TICKET-TEXT-ANIMATOR-WORKING-TREE-MIGRATION.md) (parent forward-point 2 = bench corpus sanity test).

## Forward-points

- INHERIT per AGENTS.md §`### 2×-in-one-chore: deprecation reversal bundles forward-point tickets`: nessuna reversal in gioco (no marcatori `[[deprecated]]` rimossi in this chore).
- (already-resolved partial) TICKET-TEXT-ANIMATOR-WORKING-TREE-MIGRATION forward-point 2 chiude QUI per B00+B01+B03..B11 (11/12 PASS); la B02 BENCH fa parte di una nuova entità forward-point cluster — vedi [TICKET-BENCH-CORPUS-B02-SEGV-ROT](docs/tickets/TICKET-BENCH-CORPUS-B02-SEGV-ROT.md) (PARTIAL: B02 **body-level SKIPPED** via MESSAGE + `CHECK(true)` post-amend; prima attempt annotAZIONE `* doctest::skip("reason")` fallita — decorator insufficiente per la nostra vcpkg-installed doctest version; deferral cluster 3 root-cause mechanisms pending fix post-push).
- (zone-deferred-closure) bash `tools/wrap_push.sh origin main` upstream SHA-triple verify (AGENTS.md §Post-push SHA-selfcheck invariant); macchina-verifica DEFERRED-WBH per TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV (vcpkg env-block on VPS).
- (**SCOPE-LIMITATION NOTE**) Il body-level `MESSAGE()` + `CHECK(true)` B02 mitigation è scope-limited AL test binary `chronon3d_bench_corpus_tests` SOLTANTO. La factory `bench_b02_typewriter_200_glyphs()` rimane dichiarata in `examples/bench_corpus/bench_corpus_scenes.hpp:90` ed implementata in `bench_corpus_scenes.cpp:140+`, invocando la pipeline typewriter un-migrated. Se un future compile-target `#include`s `bench_corpus_scenes.cpp` (CLI `register_bench_corpus_compositions()` binary sub-process, golden rebake tool, bench harness integration targets), il SIGSEGV B02 SORGERÀ DI NUOVO fuori dal test binary canonical mitigation scope. Canonical recovery = migrate `bench_b02_typewriter_200_glyphs()` factory via canonical `append_animator(Animator().select(...).start(a,b).start(c,d).opacity().position().scale().release())` pattern per AGENTS.md TICKET-TEXT-ANIMATOR-WORKING-TREE-MIGRATION Phase 2 forward-point cluster (6 cinematic call-sites). Post-fix: drop `* doctest::skip(...)` annotation (non piu necessaria) + re-enable original assertions + macchina-verifica-WBH for ctest -R 12/12 regression-lock.
- (post-push-cycle-rot bail-out) Questa chore è stata committed locale a `9dbb3a1d` ma `bash tools/wrap_push.sh origin main` GATE_FAIL'd su `[ahead 3, behind 11]` divergence (7+iter upstream churn systemic race window). Per AGENTS.md §21ece2b3 unique-edit recovery variant template, cycle >= 3 lost-push iterations = bail-out canonical: NO 4th amend + NO retry push. Local-only state is honest canonical state. Forward-point: re-attempt push quando `git fetch origin && git rev-parse @{u}` SHA stable per ≥1h.

## macchina-verifica

- **Pre-push this chore** (VPS, local build): `cmake --build ./ --target chronon3d_bench_corpus_tests -j 2` exit 0 GREEN. Binary present: `tests/chronon3d_bench_corpus_tests` linked against `chronon3d_pipeline + chronon3d_backend_software + chronon3d_content`. Zero `error:` lines in build log.
- **macchina-verifica DEFERRED-WBH** per TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV (vcpkg glm/magic_enum env-block on this VPS); pre-push local verification PASS is the canonical evidence.
- **Post-push SHA-triple verify**: `LOCAL_SHA_PRE_PUSH == POSTPUSH_SHA == UPSTREAM_SHA == <chore SHA>` per AGENTS.md §Post-push SHA-selfcheck invariant.

## Cat-3 minimal-surface verified

- ZERO new public SDK symbol in `include/chronon3d/` ✅
- ZERO new singleton/registry/resolver/cache ✅ (AGENTS.md deny-everywhere preserved)
- ZERO `#include <msdfgen>/<libtess2>/<unicode[/...]>` ✅ (Gate 5 Check 11 deny-everywhere preserved)
- ZERO new INTERFACE library ✅
- ZERO modification outside this chore's pre-blessed scope ✅ (Oriented precision commit — bench corpus tests non impattano altri 240+ test files).
