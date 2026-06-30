# Follow-up Tickets — Open Blockers Index

> Snapshot: `main@88d2deec` — 2026-06-29. Linux-only.
>
> File canonico per gli aperti. Massimo ~10 righe di tabella, ordinate per blocco della
> certificazione `Baseline verde: CERTIFICATA` (vedi `AGENTS.md` regola di lavoro #4). Tutti gli
> altri ticket aperti non-baseline-critical vivono in `docs/ARCHIVE/FOLLOWUP_TICKETS_HISTORY.md`
> (verbatim cronologia) e in git history pre-Step-6 (commit `6f3309e6`).
>
> Schema pregresso (`🔵 Planned` / `🟡 Partial` / `🟢 Done`) rimane canonico. Estensioni:
> `🟡 Code-fix` introdotto da Step 6 redux è sinonimo di `🟡 Partial` (tickets con codice già
> scritto ma non ancora macchina-verificato). `🔵 OPEN` introdotta per TICKET-051 è sinonimo
> di `🔵 Planned` (Step 4 spec Agent 3).

## Open blockers (top 10 — prioritari per `Baseline verde`)

| ID | Area | Stato | File principali | Gate |
|---|---|---|---|---|
| TICKET-051 | A4.3 per-preset diagnostic | 🔵 OPEN | `tests/text/test_text_preset_visual.cpp` | A4.3 visual |
| TICKET-036 | chronon3d_camera_architecture_gate P0 | 🔵 Planned | `tools/check_camera_architecture.sh` | arch-boundary (gate 5/6) |
| TICKET-044 | arch_boundaries_selftest hardcoded | 🔵 Planned | `tools/check_architecture_boundaries_selftest.sh` | arch-boundary (gate 5) |
| TICKET-046 | filename drift stale refs | 🔵 Planned | `tools/check_filename_drift.sh` | arch-boundary (gate 5) |
| TICKET-011 | pre-existing mainline build rot | 🔵 Planned | `src/` | arch-boundary (gate 1–8) |
| TICKET-005 | post-cascade cleanup | 🟡 Partial | `src/` + `include/` | arch-completeness (gate 5) |
| TICKET-022 | camera double look-at compiled path | 🟡 Code-fix | `include/camera/` | arch-boundary (gate 5/6) |
| TICKET-024 | camera OrbitMotion world-Z vs camera basis | 🟡 Code-fix | `include/camera/` | camera path |
| TICKET-026 | camera MotionBlurSettings Mode duality | 🟡 Code-fix | `include/camera/` | arch-boundary (gate 5) |
| TICKET-064 | §9 ExecutionScope — ScopeError/ScopeErrorCode structured error model (PR 6.8 prep) | 🟡 Code-fix | `include/chronon3d/core/execution/scope_error.hpp` | arch-boundary (gate 5) |
| TICKET-067 | GATE-MNT-01 strict-SHA equality incompatible with post-commit push | 🟢 Done | `tools/check_main_clean.sh` | GATE-MNT-01 (pre-push wrapper). `fix(gate): swap HEAD==origin/main strict SHA for git merge-base --is-ancestor` (env-vars Agent3, atomic on `main`; 4 tracked files [`tools/check_main_clean.sh` + `AGENTS.md` § GATE-MNT-01 line 2 + `docs/FOLLOWUP_TICKETS.md` TICKET-067 row update + this TICKET-075 Recently-closed row]; semantic swap: bidirectional `git merge-base --is-ancestor` check accepts FF-pull + post-commit-push + strict equality, rejects only true divergence where neither side is an ancestor of the other; `tools/wrap_push.sh` torna drop-in per `git push` post atomic-commit senza declared-deviation raw-push; AGENTS.md co-update rule rispettata — comment header + diagnostic-message di `tools/check_main_clean.sh` aggiornati nello stesso commit). |
| TICKET-068 | P1#5 — missing targeted crossfade+stroke regression test (Bug #5 / Fase 1#5) | 🟢 Done | `tests/text/test_crossfade_stroke.cpp` (+ `tests/core_tests.cmake` registration) | text regression. The TARGETED regression test for Bug #5 (Fase 1#5 P0) has landed. Two test cases in `tests/text/test_crossfade_stroke.cpp`: (1) crossfade with OUTGOING text LONGER than ACTIVE text establishes the OOB-precondition data shape (crossfade_layout->placed.glyphs.size() > shape->layout->placed.glyphs.size()) without crashing apply_state -- the surface that the buggy `layout.placed[gi]` would OOB on; (2) post-gap clears crossfade slots per PR 11 spec, no stale data leak forward. Registered in `tests/core_tests.cmake` `CORE_BLEND2D_TESTS` list (gated by `CHRONON3D_ENABLE_TEXT AND CHRONON3D_USE_BLEND2D`). Source fix `draw_run_layer()` stroke branch uses `source_placed.glyphs[gi].glyph_id` (active layout, bounded -- safe) instead of `layout.placed.glyphs[gi].glyph_id` (outgoing layout, OOB on longer outgoing). Discovered that the source fix is ALREADY on `main` (verified via byte-level grep on lines 492-496 of `src/backends/software/processors/text_run/text_run_processor.cpp`) -- this commit closes TICKET-068 by adding the missing Cat-2 regression test. AGENTS.md co-update rule respected (TICKET-068 row status emoji + commit reference updated in same commit).

> **Ordinamento tabella top-10**: priorita `gate-impact desc` (regole AGENTS.md #4).
> Priority order = blocco diretto dei gate `Baseline verde: CERTIFICATA` (gate 1-9 RELEASE_GATE).
> Per espandere la copertura o riordinare, applicare stessa euristica sui ticket deferred listati sotto.

## Sprint-suspended (meta-tickets)

> Le sospensioni di sprint documentate qui sotto non sono "ticket di lavoro" nel senso top-10 ma
> **meta-tickets di stato**: tracciano l'halt di un piano di migrazione in attesa di una
> condizione esterna (certificazione baseline verde, revoca formale del freeze, ecc.).
> Convenzione: lo schema-status resta canonico (`🔵 OPEN`) e lo `SUSPENDED` è un
> qualificatore testuale a chiarezza.

| ID | Area | Stato | File / Contesto |
|---|---|---|---|
| TICKET-066 | 12-ATOMO Single Selector Pipeline migration plan | 🔵 OPEN — **SUSPENDED** pending baseline verde | `docs/TEXT_SELECTOR_SINGLE_PIPELINE_PLAN.md` + downstream `include/chronon3d/text/{glyph_selector,glyph_selector_spec,compiled_selector,glyph_selector_evaluator,animation/text_animator_spec}*`. ATOMO 5/6/7/8 rolled-back per freeze `AGENTS.md` v0.1 (VIETATO: nuovi `#include`, nuove classi pubbliche, modifiche che espandono la superficie API); ATOMO 2 work preservato intatto in `stash@{0}`. |

## Other open (deferred — see archive)

29 ticket aperti non-baseline-critical. Per `**Status**` reale + root cause + risoluzione,
vedi [`docs/ARCHIVE/FOLLOWUP_TICKETS_HISTORY.md`](docs/ARCHIVE/FOLLOWUP_TICKETS_HISTORY.md)
o `git show 6f3309e6:docs/FOLLOWUP_TICKETS.md` (pre-Step-6 source-of-truth, 3834-line).

| ID | Area |
|---|---|
| TICKET-EXP2-G3 | ExpressiveV2 |
| TICKET-009 | experimental subtree |
| TICKET-010 | experimental subtree |
| TICKET-012 | Arch violation: forbidden include |
| TICKET-013 | Arch violation: sanction bypass |
| TICKET-014 | Test fail: render_session_reset |
| TICKET-015 | Test fail |
| TICKET-016 | Test fail |
| TICKET-017 | Re-enable TICKET-007.c cycle detection |
| TICKET-018 | Re-enable temporal frame-keyed jitter |
| TICKET-019 | Re-enable motion-blur torture |
| TICKET-021 | PoseTracksSource Zoom over FOV/Physical |
| TICKET-023 | OrientAlongPath stub |
| TICKET-025 | Trajectory hardcoded zoom/fov |
| TICKET-027 | ShotTimeline diagnostics |
| TICKET-028 | Constraint stateful without Checkpoint |
| TICKET-033 | CameraDescriptor payload on LayerKind::Camera |
| TICKET-034 | CameraDescriptor as canonical default |
| TICKET-035 | Framebuffer lacks bytes |
| TICKET-041 | Sync cmake/Chronon3DRegistry |
| TICKET-042 | Sync vcpkg.json deps with CMakeLists.txt |
| TICKET-043 | Audit apps/* includes |
| TICKET-045 | tools/check_gitignored_dirs + audit_software |
| TICKET-047 | tools/test_architectural.sh Section X |
| TICKET-054 | build-fast.sh target forwarding |
| TICKET-055 | Per-composition keyframe PNG forensic glyph |
| TICKET-056 | A4.4 + A4.5 memory envelope tuning |
| TICKET-057 | Drop tests/helpers/render_fixtures.hpp |

## Cross-link history

Per la cronologia completa (49+ ticket chiusi + 28 deferred a follow-up, tutti con `**Status**` reale
preservato verbatim dal pre-Step-6), vedi:

- `docs/ARCHIVE/FOLLOWUP_TICKETS_HISTORY.md` — verbatim cronologia ticket chiusi + deferred.
- `git show 6f3309e6:docs/FOLLOWUP_TICKETS.md` (commit pre-Step-6 pinned) per la versione 3834-line originale.
- `docs/CHANGELOG.md` — eventi sul main.

### Recently closed (atomic `main` commits, 2026-06-29)

| ID | Area | Status | Atomic commit on `main` |
|---|---|---|---|
| TICKET-052 | TEXT-UNM-01 — real TextUnitMap 8-level identity ladder | 🟢 Done (code on main) | `feat(text): TEXT-UNM-01 — real TextUnitMap with separated identity levels` (env-vars Agent3; **§12 closure doc lost during partner rebase**, see followup SECT-12-RESTORE; **code surface intact on main HEAD**, verify via `git grep kInvariant include/chronon3d/text/glyph_selector.hpp`) |
| TICKET-053 | TEXT-SEL-01 — Range / Wiggly / Expression canonical selectors | 🟢 Done | `feat(text): TEXT-SEL-01 — Range/Wiggly/Expression selectors as canonical types` (env-vars Agent3, atomic commit on `main`) |
| TICKET-058 | Step 9 ghost_sweep ROT-2 — `RenderRuntime::executor` chain on software_renderer consumers | 🟢 Done (atomic on `main`) | `e853e728 fix(backends/software): include render_runtime.hpp for software_renderer consumers (Step 9 ghost_sweep ROT-2)` — Fix-5 sibling rot-2 mirror del Fix-2 in `81cdc738`. Audit-comment block TICKET-038/TXT-00 cita rot-1 precedent. Forbidden-list: solo `include/chronon3d/backends/software/software_renderer.hpp`. |

See:
- `docs/MIGRATION_TEXT_SPEC.md` §13 for full closure documentation (3 canonical selector types stored under `std::variant<RangeSelector, WigglySelector, ExpressionSelector>`, `std::visit` dispatch via `evaluate_selector_v2` / `evaluate_selectors_v2`, `SafeAccessMap` with `textIndex` / `textTotal` / `frame` auto-binding, 13 new TEST_CASEs at `tests/text/test_glyph_selector_spec.cpp`).
- `docs/MIGRATION_TEXT_SPEC.md` §12 (closure for TEXT-UNM-01) is **missing on disk** — followup SECT-12-RESTORE in atomi backlog to restore closure doc after partner rebase truncated it.

| TICKET-059 | TEXT-PLY-01 — 14-feature ParagraphStyle extension + wiring | 🟢 Done | `feat(text): TEXT-PLY-01 — 14-feature ParagraphStyle extension + wiring` (env-vars Agent3, atomic on `main`; §14 closure doc in `docs/MIGRATION_TEXT_SPEC.md` §14; 14 TEST_CASEs in `tests/text/test_paragraph_layout_extras.cpp`) |

| TICKET-048 | GATE-MNT-01 — wire main-sync gate as pre-push wrapper | 🟢 Done | `ops(gates): GATE-MNT-01 — wire main-sync gate as pre-push wrapper` (env-vars Agent3, atomic on `main`; 4 tracked files: `tools/check_main_clean.sh` + `tools/wrap_push.sh` + `docs/FOLLOWUP_TICKETS.md` TICKET-048 row + `AGENTS.md` § GATE-MNT-01; local-only `.git/hooks/pre-push` installed per-repo for defence in depth) |

| TICKET-060 | SECT-12-RESTORE — MIGRATION_TEXT_SPEC §12 stub (chronology drift closure) | 🟢 Done | `docs: SECT-12-RESTORE — restore §12 stub pointing to §15` (env-vars Agent3, atomic on \`main\`; §12 stub inserted in \`docs/MIGRATION_TEXT_SPEC.md\` immediately before §13 marker; 2-file docs-only diff, no partner-owned \`src/registry/text/\` files staged) |

| TICKET-061 | TEXT-UNM-01 S15 deferred -- span_index_by_name resolves SemanticSpanRef.name | done | feat(text): close TEXT-UNM-01 S15 deferred -- span_index_by_name now resolves SemanticSpanRef.name (env-vars Agent3, atomic on main; include/chronon3d/text/text_unit_map.hpp +1 member; src/text/text_unit_map.cpp ctor init + impl replacement; tests/text/test_text_unit_map_8level.cpp +1 TEST_CASE (i); docs/MIGRATION_TEXT_SPEC.md S15.X; docs/FOLLOWUP_TICKETS.md TICKET-061 row) |

| TICKET-062 | PARTNER-OVERLAY-CLEANUP — gitignore src/registry/text/ (Refactor 1B) | 🟢 Done | `chore(gitignore): PARTNER-OVERLAY-CLEANUP — exclude Refactor 1B overlay (canonical registry is cmake/...)` (env-vars Agent3, atomic on `main`; 2-file diff [`.gitignore` + `docs/FOLLOWUP_TICKETS.md`]; gate-fix-only, NOT a gate-skip; partner commit `8d5ed596` never carried the 8 stale files; partner-overlay now invisible to `git status` → GATE-MNT-01 passes cleanly for all pushes; if partner rescinds, `git add -f src/registry/text/` recovers them).

| TICKET-063 | SCRATCH-HELPERS-CLEANUP — gitignore tools/_*.py (Agent3 scratch tooling) | 🟢 Done | `chore(gitignore): SCRATCH-HELPERS-CLEANUP — exclude tools/_*.py (Agent3 idempotency helpers)` (env-vars Agent3, atomic on \`main\`; SAME atomic-commit as TICKET-062 — amends commit 8bbd376b; 2-file config+docs diff [\`.gitignore\` extension + \`docs/FOLLOWUP_TICKETS.md\` TICKET-063]; gate-fix-only, NOT a gate-skip; underscore prefix signals \`ephemeral local helper, NOT durable project tooling\` consistent with existing .gitignore convention \`/src/registry/text/\` + \`.worktrees/\` + \`.build-tmp/\`).
| TICKET-070 | SoftwareBackendServices validation follow-ups | 🟡 Partial | src/backends/software/ + tests/backends/software/ | post-Fase-1 close-out | Sub-item (b) closed: regression test for `make_software_backend()` null REQUIRED services landed in `tests/backends/software/test_software_backend_factory.cpp` (registered in `tests/backends_software_tests.cmake`). The test exercises (i) the happy-path `Result::value(unique_ptr<SoftwareBackend>)` path on a live `SoftwareRenderer`-wired bundle (BOTH modes), (ii) static-grep wiring proof over `src/backends/software/software_backend.cpp` that asserts every `assert(services.<field>)` debug guard AND every `SoftwareBackendServicesError::Code::Missing<Field>` release branch is present (BOTH modes, project convention lifted from `tests/text/test_font_io_fence.cpp`), and (iii) per-field `Result::err(Code::Missing<X>, field_name="<x>")` exercise for all 6 REQUIRED services (NDEBUG-only — debug builds assert() before Result::err). Sub-item (c) MINOR follow-ups already applied at codebase level (canonical assert form `assert(ptr && "msg")`; reordered validation matches user spec). Sub-item (a) is the only remaining work: TICKET-046 follow-up to relax owner from REQUIRED to null-tolerable once `runtime::RenderRuntime`-sourced `SoftwareProcessorContext` lands. |
| TICKET-071 | Real `GlyphOutlineCache` with `(font_identity × glyph_id × size × hinting_mode)` key + LRU + deterministic outline hash | 🔵 Planned | src/backends/text/ | post-rename | Cover (a) plumbing `hinting_mode` through `FontSpec → TextRunShape → draw_text_run` (currently uniform `FT_LOAD_NO_BITMAP` so any future dim is a no-op constant under default load); (b) deterministic outline hash (XXH3 of canonicalized vertex tuples) for variance checks; (c) LRU policy sized against `kMaxPooledStates = 8` to avoid unbounded memory growth under burst; (d) regression matrix demonstrating hit/miss + eviction-after-touch semantics in tests/text/test_glyph_outline_cache.cpp (NOT YET CREATED). Pre-requisite for option (b) closure: hinting-mode-visible user plumbing. Until then the symbol `GlyphOutlineBuilder` (rename of `GlyphOutlineCache`) is the canonical name. |
| TICKET-072 | Extend font preflight walk to enumerate ALL `(font_path, font_size)` variants per layer (crossfade From/To, animated font swaps) | 🔵 Planned | src/backends/software/software_renderer.cpp | post-preflight | Cat-2 preflight PR landed the minimum-scope walk (per-layer (fontspec) — first TextRunShape per `is_text()` layer). Follow-up must walk every `TextRunShapeHandle` payload in the node vector, including `shape.crossfade_glyphs` and any animated font swaps surfaced via `text_run_driver.cpp::update_text_run_shape_per_frame`. Required for users that swap fonts across crossfade boundaries, otherwise the preflight misses one slot per crossfade-side layer. Public API (`preflight_fonts`) supports the extension additively — predicate branch additions only, no signature changes. |
| TICKET-073 | Capture `reports/perf/dof_span_before_after.json` measurements from CI / cmake 3.27+ env | 🟡 Partial | tests/renderer/camera/test_per_pixel_dof.cpp | post-perf-dof-refactor | perf(dof) atomic commit shipped TEST_CASE `PerPixelDOF: 1920x1080 micro-benchmark — drop depth copy (delta report)` that, on `./build-fast.sh test "*1920x1080*"` in any CMake 3.27+ env, will overwrite `reports/perf/dof_span_before_after.json` with real `after_ms` / `before_alloc_copy_ms` / `delta_per_frame_ms` numbers. Local hardened Linux env shipped cmake 3.25 only (project requires 3.27+); the bench source committed but the placeholder JSON was hand-written (explicit `status: deferred_pending_ci_run` flag) rather than fabricated. CI runs linux-fast-dev preset and has 3.27+, so the first CI run fills the file. Acceptance: `status` flips away from `deferred_pending_ci_run`; `delta_per_frame_ms` is non-zero and shows the 1920×1080 zero-copy saving. |
| TICKET-074 | FREEZE-CLEANUP-variant — remove dead `glyph_selector_v2` variant pipeline (broken spec test referencing non-existent `TextSelectorUnit::CodePoint`) | 🟢 Done | `chore(deprecate): remove dead glyph_selector_v2 variant pipeline (freeze cleanup)` (env-vars Agent3, atomic on `main`; 4 tracked files: 3 deletions [`include/chronon3d/text/glyph_selector_spec.hpp` + `src/text/glyph_selector_v2.cpp` + `tests/text/test_glyph_selector_spec.cpp`] + 1 doc edit [`docs/FOLLOWUP_TICKETS.md` TICKET-066 row refreshed + this TICKET-074 row added]; freeze-aligned Cat-3 “Rimozione percorsi legacy — dead code” per AGENTS.md v0.1; variant pipeline surface remains BLOCKED pending baseline verde per the user-cited constraint “nuove classi pubbliche nella pipeline variant sono bloccate dal freeze finché baseline non è 11/11 verde”; static-grep post-commit confirms zero `glyph_selector_spec.hpp` or `glyph_selector_v2` references in the tracked tree; `tests/text/glyph_selector_helpers.hpp` retained — does NOT include spec.hpp). |

| TICKET-075 | GATE-MNT-01 strict-SHA → ancestor-relation (close TICKET-067) | 🟢 Done | `fix(gate): swap HEAD==origin/main strict SHA for git merge-base --is-ancestor` (env-vars Agent3, atomic on `main`; 4 tracked files [`tools/check_main_clean.sh` + `AGENTS.md` § GATE-MNT-01 line 2 + `docs/FOLLOWUP_TICKETS.md` TICKET-067 row + this TICKET-075 row]; semantic swap: bidirectional ancestor-relation check accepts FF-pull + post-commit-push + strict equality, rejects only true divergence where neither side is an ancestor of the other; `tools/wrap_push.sh` torna drop-in per `git push` post atomic-commit senza declared-deviation raw-push; AGENTS.md co-update rule rispettata — comment header + diagnostic-message di `tools/check_main_clean.sh` aggiornati nello stesso commit; § GATE-MNT-01 di AGENTS.md aggiornato nello stesso commit). |

| TICKET-069 | FreeTypeFaceCache destruction contract enforcement (runtime) | 🟢 Done | `include/chronon3d/backends/text/text_render_resources.hpp` + `src/backends/text/text_render_resources.cpp` | The lease-anchor lifetime contract for `FreeTypeFaceCache` (documented in the cpp `// FreeTypeFaceCache` DESTRUCTOR CONTRACT comment block — "the cache must NOT be destroyed while concurrent `get_face()` calls are in flight") is now ENFORCED at runtime, not just documented. Three debug-only instrumentation pieces, all `#ifndef NDEBUG`-gated (Release builds have zero payload): (1) private `std::atomic<int> in_flight_{0}` field on `FreeTypeFaceCache`; (2) file-local RAII `InFlightGuard` struct in anonymous namespace increments at top of `get_face()` body (BEFORE the `std::lock_guard<std::mutex> lock(mutex_)` so even threads blocked on mutex acquisition count as in-flight) and decrements on every return path including throw-unwinding via `std::runtime_error` (fence-trip path); (3) `~FreeTypeFaceCache()` loads `in_flight_.load(std::memory_order_acquire)` and asserts == 0 with descriptive diagnostic naming TICKET-069 + corrective hint. Memory ordering: `relaxed` for `fetch_add`/`fetch_sub` (counter snapshot semantics, cheapest) + `acquire` for destructor load (observes all relaxed writes from any thread's in-flight call before assert). Existing 5 regression cases in `tests/text/test_freetype_face_cache_concurrency.cpp` pass unchanged because none trigger concurrent destruction while `get_face()` is in flight — the assert fires exactly when the contract is violated, never on benign use. Public accessor `[[nodiscard]] int in_flight() const noexcept` added on the class (relaxed load) for future tests to probe counter transitions without intrusive instrumentation. Gating convention uniform `#ifndef NDEBUG` (matches `make_software_backend` assert block in `software_backend.cpp:269`). AGENTS.md co-update rule respected (TICKET-069 row added to Recently-closed table in same commit). |
