# ── Core Tests (Math, Geometry, Animation, Timeline, Cache, SIMD, Assets, Text, Media, Extension, Architecture) ──

# Blend2D/Text-dependent test sources (only compiled when both are available)
set(CORE_BLEND2D_TESTS "")
if(CHRONON3D_USE_BLEND2D AND CHRONON3D_ENABLE_TEXT)
    list(APPEND CORE_BLEND2D_TESTS
        text/test_text_layout.cpp
        text/test_text_bounds.cpp
        text/test_text_cache_key.cpp
        # TEXT-RET-01 — retired legacy multi-layer TextAnimator (its
        # 17 tests in tests/text/test_text_animator.cpp deleted).
        # The canonical path (TextPreset registry → AnimatorResolver →
        # TextAnimatorSpec → wire_preset_text_run_params → lb.text_run(...).commit())
        # produces ONE TextRunShape RenderNode, not N per-char layers.
        # Staggered reveal coverage lives in
        # tests/test_text_preset_registry.cpp Sub-cases 11-27 + 30-32.
        text/test_text_material.cpp
        text/test_text_style_presets.cpp
        text/test_font_engine.cpp
        text/test_text_quality_glyph.cpp
        text/test_text_quality_shaping.cpp
        text/test_text_quality_tracking.cpp
        text/test_text_quality_arabic.cpp
        text/test_text_bidi.cpp
        # P1-2 -- font determinism regression suite (Feature Freeze cat-2).
        #   (1) FriBidi compile-time gate (static_assert),
        #   (2) font fallback chain sentinel (documentation lock),
        #   (3) same-machine bidi + fallback determinism,
        #   (4) bidi run-count regression (FriBidi active -> multi-run).
        # See docs/tickets/TICKET-P1-ACTION-PLAN.md §P1 #2.
        text/test_text_font_determinism.cpp
        # M1.5#8 — golden test for FontResolver + resolve_text_run_tree
        # determinism (FNV-1a hash snapshot + FriBidi env override sanity).
        text/test_text_font_resolver_golden.cpp
        text/test_text_unit_map.cpp
        text/test_text_unit_map_8level.cpp
        text/test_selector_shapes.cpp
        text/test_selector_evaluate.cpp
        text/test_selector_combine.cpp
        # M1.5#2 — locks EffectiveTextState equality contract for the
        # 5-field fast-path identity (text + FontLayoutIdentity +
        # direction + language + features).  Pure value-construction,
        # no font engine needed.
        text/test_effective_text_state.cpp
        # Bug #7 regression — Fase 1#7 close-out.  Concurrency coverage
        # for the refactored FreeTypeFaceCache (key-indexed cache +
        # shared_ptr<FT_Face> lease anchor on FontFaceHandle).  See
        # tests/text/test_freetype_face_cache_concurrency.cpp for the
        # race scenario: thread A holds a handle while thread B invokes
        # get_face() on different paths / sizes; identity-stable hits;
        # outlives cache destruction; size-bucket independence.
        text/test_freetype_face_cache_concurrency.cpp
        # FASE 3 — concurrency test for TextScratchManager (TSan-clean).
        # Exercises two concurrent draw_text_run-shaped workloads to
        # verify that the engine-vended scratch states are mutex-guarded
        # and pool entries are returned to the right owner. See commit
        # message "fix(text): move static scratch/surface pools into
        # TextRenderResources (thread-safety)" for the refactor context.
        text/test_text_pool_concurrency.cpp
        # Cat-2 font preflight I/O fence regression. Defends against
        # re-introduction of synchronous font I/O on render threads
        # (BLFontFace::createFromFile / FT_New_Face).  Covers:
        #   (1) arm + miss + fence=true -> throws std::runtime_error
        #   (2) disarm + miss -> cache lazy-loads (production path)
        #   (3) re-arm + hit -> no throw (post-preflight hot path)
        #   (4) per-tuple proof: distinct sizes get distinct entries
        #   (5) static-grep proof: hot path contains no I/O calls
        text/test_font_io_fence.cpp
        # TICKET-068 — Bug #5 / Fase 1#5 regression test.  Crossfade
        # with OUTGOING text longer than ACTIVE text + the crossfade
        # slots populated.  The fix in `draw_run_layer()` stroke branch
        # uses `source_placed.glyphs[gi].glyph_id` (active layout,
        # bounded -- safe) rather than `layout.placed.glyphs[gi].glyph_id`
        # (outgoing layout, unbounded -- would OOB on longer outgoing).
        # This test establishes the OOB-precondition data shape and
        # verifies the apply-state path is crash-free; the static-grep
        # discipline in the commit body is the no-regression lock.
        text/test_crossfade_stroke.cpp
        # TICKET-068 follow-on E2E — machine-check the dispatch path of
        # the Bug #5 fix (commit 0d32e049) by invoking
        # `SoftwareBackend::draw_text_run` end-to-end with a stroke-enabled
        # shape carrying the OOB-precondition crossfade data shape (outgoing
        # text strictly longer than active text).  Pairs with the
        # data-shape-only test above: that file locks the precondition +
        # apply-state path, this file locks the dispatch path returns
        # without crashing (failure mode of Bug #5 was OOB-read on
        # `layout.placed[gi]`).  Skipped via MESSAGE when the Inter-Bold
        # font fixture is unavailable (same pattern as crossfade_stroke +
        # font_io_fence + freetype_face_cache_concurrency).
        text/test_draw_text_run_crossfade_stroke.cpp
        # P0-1 regression — draw_text_run scratch_state acquisition +
        # per-span font_handle fix.  Before the fix, scratch_state was
        # used at ~15 call sites without being declared, and the old
        # `font_handle.ft_face` referenced an undeclared variable.
        # Covers: (1) null text_resources → InvalidInput, (2) null
        # asset_resolver → InvalidInput, (3) E2E render-success with
        # stroke proving per-span font_handle + RAII scratch acquisition.
        text/test_draw_text_run_scratch_state.cpp
        # Fase 1.3 (verdetto Issue #4) regression — GlyphAtlas preserves
        # full metadata across store/lookup.  Before this commit the
        # cache stored only std::shared_ptr<BLImage> and reconstructed a
        # default-init GlyphAtlasEntry on lookup, losing x_offset,
        # y_offset, advance_x, fill_color_rgba.  Tests 1–3 in this file
        # verify direct round-trip; caller-side re-check of
        # fill_color_rgba (text_rasterizer_render.cpp:627) handles
        # cross-color rejection per the documented contract.
        text/test_glyph_atlas_metadata.cpp
        # CR#1 (Fase 1.2 + 1.5 closure) regression — compile_text_layout
        # structured-error taxonomy + build_text_run wrapper
        # multi-font skip semantics (N-1 contract).  Cases:
        #   1. Err(EmptySource):       empty utf8 + no pre-split paragraphs
        #   2. Err(MissingFont):       no usable font after resolve_fallback_fonts
        #   3. Err(UnsupportedMultiFontRun): 2+ runs with divergent FontSpec
        #   4. build_text_run skip:    doc with 1 multi-font paragraph + 2
        #      single-font paragraphs yields N-1 layouts (N=3 => 2).  Skipped
        #      automatically when tests/fixtures/Inter-Bold.ttf is absent.
        text/test_compile_text_layout_errors.cpp
        # TICKET-100 — identity regression lock for the materialize_text_run_shape
        # refactor.  Locks (1) materialize_text_run_shape ≡ compile_text_layout
        # identity on equivalent input (source_text / font_size / glyph_count /
        # units size); (2) cache-hit returns the same shared_ptr across calls
        # (legacy-canonical cache_key preservation); (3) direction + language
        # override survives the post-compile helper pass (compile_text_layout's
        # hardcoded Auto/empty defaults would otherwise leak through);
        # (4) text_layout->font.font_size mirrors params.text.font.font_size
        # (review P0 #6 closure — pre-refactor `shaped_font = {4 fields}` left
        # text_layout->font.font_size at the default 0.0f).  Gracefully degrades
        # via MESSAGE+return when system fonts are absent (no external fixture).
        text/test_compile_text_layout_identity.cpp
        # ADR-018 — auto-fit binary search regression locks:
        # shrink triggers, no-op when fits, min clamp, 12-iteration determinism.
        text/test_auto_fit_font_size.cpp
        # TICKET-101 — text cat-3 #2 regression suite for build_text_run +
        # compile_text_layout after the refactor that:
        #   (a) added `paragraph_index` to TextLayoutRequest (HPP POD
        #       extension, zero new public classes) so compile_text_layout
        #       can index into the ORIGINAL document without a synthetic
        #       per-paragraph TextDocument,
        #   (b) removed the para_doc synthesis from the build_text_run
        #       wrapper, preserving spans / font-overrides / font-size-per-
        #       span / tracking-per-span / paragraph-style / direction /
        #       language that were previously destroyed (review P0 #2),
        #   (c) moved UnsupportedMultiFontRun rejection from
        #       compile_text_layout to the PUBLIC build_text_run wrapper
        #       (verdict Issue #3 closure at the public API surface).
        # 3 TEST_CASEs: (1) wrapper rejects multi-font paragraph
        # (UnsupportedMultiFontRun propagation, paragraph absent from
        # result); (2) span override (font_size) preserved through
        # compile_text_layout (regression lock for previous para_doc
        # synthesis that dropped span info); (3) paragraph style
        # (line_height) preserved through compile_text_layout
        # (regression lock for previous para_doc synthesis that dropped
        # paragraph style).  Uses LocalEngine fixture pattern from
        # test_compile_text_layout_errors.cpp; degrades gracefully when
        # system fonts are unavailable.
        text/test_rich_text_paragraph_preservation.cpp
        # TICKET-092 -- text cat-3 #3 regression suite for the INTERNAL
        # accumulator (compile_text_document) introduced in src/text/.
        # The accumulator pattern replaces the previous
        # `spdlog::warn(...) + goto next_paragraph` skip-on-failure
        # pattern with a per-paragraph result vector (Ok AND Err both
        # preserved) keyed by `source_index` so callers can identify
        # WHICH paragraph failed.  3 TEST_CASEs:
        #   (1) central paragraph (i=1) is multi-font: all 3
        #       CompiledParagraphResult preserved (size == 3), Err at
        #       source_index == 1 has TextLayoutErrorKind ::
        #       UnsupportedMultiFontRun, complete == false;
        #   (2) single-paragraph multi-font doc: 1 entry with Err at
        #       source_index == 0, complete == false (regression lock
        #       that the accumulator does NOT silently swallow the
        #       failure into an empty vector as the old warn+skip
        #       pattern did);
        #   (3) all-single-font 3-paragraph doc: 3 Ok entries,
        #       complete == true (sanity check that the success case is
        #       purely additive, no semantic change for callers that
        #       only ever saw successful compiles).  All cases
        #       deterministic; system-font availability is irrelevant
        #       because the multi-font pre-check fires on the RESOLVED
        #       tree before any shaping.  Header reach:
        #       `../../src/text/text_document_compile_result.hpp` is
        #       the only way the test sees the INTERNAL accumulator
        #       types (they are NOT in include/chronon3d/, by
        #       cat-3-freeze design).
        text/test_compile_text_layout_per_paragraph_failure.cpp
        # TICKET-105 -- text cat-2 identity/preservation regression
        # suite expansion.  3 deterministic TEST_CASEs (no threads,
        # no time, no PRNG per AGENTS.md v0.1 cat-2 freeze-compliant
        # invariants):
        #   (1) materialize_text_run_shape -> compile_text_layout
        #       identity across frames: same `shared_ptr<TextRunLayout>`
        #       AND same `layout_hash()` between frame N and frame
        #       N+1 scramble (cache hit locks).
        #   (2) Joint-include contract (canonical-only mode): the
        #       canonical `text_unit_map.hpp` (8-level class) compiles
        #       cleanly in this TU; the 8-level ladder counts are
        #       observable; the ODR conflict with the narrow
        #       `struct TextUnitMap` in `glyph_selector.hpp` is
        #       documented and delegated to TICKET-083 (post-baseline-
        #       verde migration).  Narrow compile contract is exercised
        #       by sibling test files at single-header TU boundaries.
        #   (3) Double-include safety: triple `#include
        #       <chronon3d/text/text_unit_map.hpp>` at file scope.
        #       If `#pragma once` were broken, the third include would
        #       emit a class redefinition error at compile time.
        #       File compile-success is the regression lock.
        text/test_text_unit_map_joint_include.cpp
        # TICKET-103a -- text cat-3 #4 cache-key collision regression
        # suite.  2 deterministic TEST_CASEs (no threads, no time,
        # no PRNG, no font engine dependency):
        #   (1) Same text + font + wrap but direction=LTR vs direction=RTL
        #       -> TextLayoutCacheKey::digest() MUST differ (locks bidi
        #       cache-collision contract; pre-TICKET-103a the
        #       force-override collapsed both to TextDirection::Auto,
        #       producing a false cache hit on bidi-shaping inputs);
        #   (2) Same text + font + wrap + direction but language="ar"
        #       vs language="en" -> digests MUST differ (locks BCP-47
        #       cache-collision contract; pre-TICKET-103a
        #       language.clear() collapsed both, producing a false
        #       cache hit on Arabic vs English script-specific shaping
        #       decisions in HarfBuzz).  Pure key-construction tests;
        #       no font engine / system fonts required.
        text/test_layout_cache_collision.cpp
        # P1-8 -- regression lock for the canonical
        # font_identity_of(FontSpec) -> FontIdentity helper + the
        # FontIdentity equality operators (defined in
        # include/chronon3d/text/font_engine.hpp).  Pure helper / POD
        # test (no font engine, no threads, no time, no PRNG --
        # AGENTS.md v0.1 cat-2 freeze-compliant).  5 TEST_CASE groups
        # cover: (A) font_size change alone is identity-equal at any
        # size from 0 to 96 pt; (B) each of the 4 identity fields
        # (font_path / font_family / font_weight / font_style) DOES
        # diverge on its own; (C) combined field changes diverge;
        # default-constructed FontSpecs share identity; (D)
        # operator== and operator!= are exact logical complements;
        # (E) FontSpec::operator== implies identity equality
        # (lock against the two equalities drifting).  Mirrors the
        # pure-helper pattern of test_layout_cache_collision.cpp.
        text/test_font_identity_contract.cpp
        text/test_character_offset_pre_shaping.cpp
        text/test_text_document_builder.cpp
        # FASE 4b — timed text model + adapter tests
        text/test_timed_text_document.cpp
        # F2.A — TextDefinition adapter convergence tests:
        # from_text_spec / from_text_run_spec field-by-field mapping,
        # centered_text / glow_text convergence, no-data-loss round-trip,
        # default TextSpec, TextSpanOverride, determinism.
        # Gated on Blend2D because content/text/text_helpers_centered.hpp
        # transitively includes backends/text/text_layout_engine.hpp.
        text/test_text_definition.cpp
    )
endif()

# ── Shared link contract (§11.1 INTEGRATION tier) ────────────────────
set(_CORE_LINK_TARGETS
    chronon3d_sdk
    chronon3d_sdk_impl
    chronon3d_pipeline
)

chronon3d_add_test_suite(
    NAME   chronon3d_core_tests
    TIER   INTEGRATION
    LINK_TARGETS ${_CORE_LINK_TARGETS}
    SOURCES
    core/test_frame_context.cpp
    core/memory/test_huge_page_allocator.cpp
    core/math/test_math.cpp
    core/math/test_output_transform.cpp
    simd/test_simd_kernels.cpp
    cache/test_lru_weight.cpp
    cache/test_lru_cache.cpp
    cache/test_framebuffer_pool.cpp
    cache/test_video_frame_cache.cpp
    cache/test_persistent_framebuffer_store.cpp
    # Fix #3 (post-FASE-18 review) -- standalone test for FASE 18's
    # extracted evict_lru_for helper trio.  Exercises the LRU eviction
    # driver in isolation via the public release() / stats() / set_budget_bytes()
    # surface.  Deterministic, cat-2 freeze-compliant.
    cache/test_evict_lru_for.cpp
    core/test_sharded_telemetry_store.cpp
    core/test_render_counters.cpp
    core/test_cache_eval_dirty_counters.cpp
    core/math/test_camera_pose.cpp
    core/math/test_camera_2_5d_projection.cpp
    core/math/test_projector_2_5d.cpp
    core/math/test_2_5d_roadmap.cpp
    core/math/test_camera_projection_resolver.cpp
    core/math/test_camera_projection_geometry_safety.cpp
    # Fix #3 (post-FASE-17 review) -- standalone tests for FASE 17's
    # extracted inline helpers.  Deterministic, cat-2 freeze-compliant
    # (no threads / no time / no PRNG).  Test files live alongside
    # test_camera_projection_resolver.cpp because they share the headers
    # under test (camera_projection_clip.hpp, camera_projection_frustum.hpp).
    core/math/test_clip_with_uv.cpp
    core/math/test_frustum_culling.cpp
    core/geometry/test_geometry.cpp
    # F1.B — unified text placement resolver (ADR-019 Decision 3)
    text/test_text_placement_resolver.cpp
    core/animation/test_animation.cpp
    core/animation/test_interpolate.cpp
    core/animation/test_spring.cpp
    core/animation/test_animated_value.cpp
    core/animation/test_animated_value_expression.cpp
    core/animation/test_keyframes.cpp
    core/animation/test_keyframe_tracks.cpp
    core/animation/test_animated_transform.cpp
    core/animation/test_cubic_bezier.cpp
    core/animation/test_stagger.cpp
    core/animation/test_animation_curve.cpp
    core/animation/test_animation_track.cpp
    core/animation/test_wiggle.cpp
    core/animation/test_sample_time.cpp
    # P2-D — frame rate edge cases: 23.976/24/25/29.97/30/59.94/60 fps,
    # subframe evaluation, freeze frame, reverse playback, TemporalSampleKey ticks.
    # Core types only (FrameRate, SampleTime, TemporalSampleKey) — no video dependency.
    core/test_frame_rate_edge_cases.cpp
    core/animation/test_temporal_spatial_curve.cpp
    core/animation/path/test_catmull_rom_path.cpp
    core/animation/path/test_spatial_bezier_path.cpp
    core/animation/test_quaternion_track.cpp
    core/timeline/test_timeline.cpp
    core/timeline/test_timeline_builder.cpp
    core/timeline/test_code_first_composition.cpp
    core/timeline/test_sequence.cpp
    test_project.cpp
    core/timeline/test_timeline_resolver.cpp
    core/timeline/test_sequence_builder.cpp
    core/timeline/test_sequence_animation.cpp
    core/timeline/test_sequence_integration.cpp
    assets/test_svg_path_loader.cpp
    assets/test_render_preflight.cpp
    assets/test_asset_registry.cpp
    assets/test_asset_resolver.cpp
    assets/test_asset_manifest.cpp
    assets/test_asset_preflight_resolver.cpp
    core/math/test_color_space.cpp
    core/math/test_nan_guard.cpp
    core/test_system_metrics_parse.cpp
    # WP-6 PR 6.0 — ExecutionScope (root/tile/precomp) construction
    # invariants + parent chain + recursion guard + child-arena
    # independence.  Header-only type under test: no .cpp pairing.
    core/test_execution_scope.cpp
    core/math/test_expression.cpp
    core/math/test_expression_extended.cpp
    test_text_preset_registry.cpp
    # TEXT-RES-01 — TextPresetDescriptor single-source-of-truth invariants
    # (id/metadata/builder/animator_factory/fixture completeness, resolver
    # ↔-registrar equivalence, fail-safe paths).  See
    # tests/registry/test_text_preset_descriptor.cpp for the Sub-cases A1-D2.
    registry/test_text_preset_descriptor.cpp
    # M1.5#3 — text_run.hpp umbrella contract lock (header-only,
    # non-gated, no Blend2D dependency).  Static_asserts lock the
    # 5 sub-header surfaces + umbrella transit coverage + zero
    # deprecated singleton symbols.  Cat-2 freeze-compliant
    # (no threads / no time / no PRNG / no font engine).  Runs
    # unconditionally on every preset; the surrounding target
    # `chronon3d_core_tests` may carry TICKET-011 LINK rot
    # independently (separate scope, see FOLLOWUP_TICKETS).
    text/test_text_run_umbrella_contract.cpp
    # M1.5#5 — orchestrator delegation regression lock for the
    # text_run_builder.cpp -> text/compiler/*.cpp split.  3
    # deterministic TEST_CASEs (no threads / no time / no PRNG /
    # no Blend2D) that lock:
    #   (1) compile_text_document end-to-end: 3 single-font
    #       paragraphs -> all 3 entries preserved with source_index
    #       0..2 + complete == every-Ok,
    #   (2) orchestrator delegation through MissingFont seam:
    #       middle paragraph with empty font spec -> 3 entries
    #       preserved (TICKET-092 "Err in middle, Ok siblings
    #       preserved" pattern) + Err kind == MissingFont at
    #       source_index 1,
    #   (3) orchestrator delegation through empty paragraph
    #       short-circuit seam (TICKET-101): consecutive \n ->
    #       middle paragraph Ok with empty TextRunLayout (valid
    #       empty units + bounds {0,0} + line_height = default
    #       * 1.2).
    # Non-gated like test_text_run_umbrella_contract.cpp; the test
    # is structural rather than font-bound, so it runs on every
    # preset (no Inter-Bold.ttf fixture required).
    text/test_text_run_multi_run_failure_policy.cpp
    ${CORE_BLEND2D_TESTS}
    media/test_media_placement.cpp
    core/test_result.cpp
    core/test_scene_hasher_camera.cpp
    render_graph/executor/test_cache_key_contract.cpp
    render_graph/executor/test_framebuffer_lifetime.cpp
    render_graph/executor/test_text_bbox_reconcile.cpp
    render_graph/executor/test_executor_levels_parallel.cpp
    render_graph/builder/test_graph_build_pass_order.cpp
    # WP-3 PR 3.0 + PR 3.3 — render-session noexcept-throw guards and
    # multi-session isolation tests live in tests/runtime/ alongside the
    # other session/services tests.
    runtime/test_render_session_reset_and_isolation.cpp
    # TICKET-A3-SESSION-POLICY (Agent3 mission DoD gate (c)) — 2-frame
    # regression lock for the KeepLastValidCamera -> session.last_valid_camera
    # wire. Lives in tests/runtime/ alongside the other session/services
    # tests (matches the WP-3 PR 3.x convention for cross-frame state tests).
    runtime/test_camera_session_keep_last_valid.cpp
    # TICKET-A3-CACHE-LEASE (Agent3 mission DoD gate (d)) — locks the
    # contract that acquire() does NOT advance last_evaluated_frame
    # (commit() is the sole writepoint) and that a failed evaluation
    # without commit() leaves the cache entries_ untouched. Pairs with
    # the existing tests/runtime/test_render_session_reset_and_isolation.cpp
    # (WP-3 PR 3.x) on the cache-isolation pattern.
    runtime/test_camera_session_cache_failed_no_commit.cpp
    # Fase 5 — TICKET-P1-09 closure: two independent RenderRuntime
    # instances must not share state (caches, pools, registries,
    # resolvers, schedulers, executors).  Pure constructor/config test;
    # no backend, no threads, no time, no PRNG.
    runtime/test_render_runtime_isolation.cpp
    # TICKET-ZERO-A1 / TICKET-A3-CACHE-LEASE follow-on — locks the
    # SESSION STATE rollback contract on top of test_camera_session_cache_
    # failed_no_commit.cpp (which only locks last_evaluated_frame).
    # 3 SUBCASEs: (a) failed eval + no commit → checkpoint.session
    # preserved; (b) successful eval + commit → checkpoint.session
    # shows post-eval state + last_evaluated_frame advances;
    # (c) successful eval + no commit → checkpoint.session preserved.
    # Companion to camera_session_cache_failed_no_commit.cpp on the
    # working-copy-on-Entry pattern in camera_session_cache.hpp.
    runtime/test_camera_session_cache_failed_no_commit_session_state.cpp
    # TICKET-106 — Cat-2 path-list parity regression.  Source lives at
    # `${CMAKE_SOURCE_DIR}/tools/` per user request (colocated with the
    # gate script it asserts against); compiled into chronon3d_core_tests
    # so it runs in the existing CTest slot.  Asserts every gate-3
    # TICKET row's acceptance-criterion recipe matches the actual scan
    # paths in tools/check_software_renderer_boundary.sh, locking the
    # drift bug observed on `main@872993ea` (TICKET-079 row cited 6
    # paths; gate-3 walker scans 5 paths).  ID chosen after partner
    # commits claimed BOTH prior eligible IDs -- `TICKET-086` (FASE-4
    # thread_local permutation cache refactor) AND `TICKET-100`
    # (FASE-4 legacy `materialize_text_run_shape` pipeline elimination)
    # -- for unrelated refactors, so this Cat-2 test landed under the
    # next free numeric slot.
    ${CMAKE_SOURCE_DIR}/tools/test_software_renderer_boundary_consistency.cpp
)

# TICKET-006: CORE_BLEND2D_TESTS exercise symbols that only resolve when
# chronon3d_backend_text is linked (bidi_segmenter, font_engine, glyph_atlas,
# shared_font_engine, rasterize_text_to_bl_image, etc.). Without this guard
# in non-Blend2D / non-text presets (e.g. linux-core-dev), the build reports:
#   'undefined symbol: chronon3d::shared_font_engine()'
#   'undefined symbol: chronon3d::glyph_atlas_lookup(...)'
#   'undefined symbol: chronon3d::segment_bidi_runs(...)'
# The duplication with tests/scene_tests.cmake is intentional: core-tests
# pull in text_layout / text_bounds / text_quality_*, scene-tests pull in
# layer_design_kit / layer_builder / text_run_builder — both end up calling
# into chronon3d_backend_text. Linking indiscrimate would be a regression.
if(CHRONON3D_ENABLE_TEXT AND CHRONON3D_USE_BLEND2D AND TARGET chronon3d_backend_text)
    target_link_libraries(chronon3d_core_tests PRIVATE chronon3d_backend_text)
endif()

# Make CMAKE_CURRENT_BINARY_DIR available for ExtensionLoader plugin tests (finds .so files)
target_compile_definitions(chronon3d_core_tests PRIVATE
    CMAKE_CURRENT_BINARY_DIR="${CMAKE_CURRENT_BINARY_DIR}"
)
target_include_directories(chronon3d_core_tests PRIVATE ${CMAKE_SOURCE_DIR})

# TICKET-011 build-rot fix: unity-build exclusions for test files with
# anonymous-namespace struct collisions (LocalEngine) and ODR conflicts
# (canonical class TextUnitMap vs narrow struct TextUnitMap).
set_source_files_properties(
    ${CMAKE_CURRENT_SOURCE_DIR}/text/test_compile_text_layout_errors.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/text/test_compile_text_layout_identity.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/text/test_rich_text_paragraph_preservation.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/text/test_compile_text_layout_per_paragraph_failure.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/text/test_text_unit_map_joint_include.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/text/test_text_unit_map_8level.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/text/test_crossfade_stroke.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/text/test_draw_text_run_crossfade_stroke.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/text/test_font_io_fence.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/text/test_draw_text_run_scratch_state.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test_text_preset_registry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/runtime/test_camera_session_cache_failed_no_commit_session_state.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/registry/test_text_preset_descriptor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/authoring/test_animator_dsl.cpp
    PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON
)

# Sequence V2 compositions live in the content module — only compile when content is built.
if(CHRONON3D_BUILD_CONTENT AND TARGET chronon3d_content)
    target_sources(chronon3d_core_tests PRIVATE core/timeline/test_sequence_v2_compositions.cpp)
    target_link_libraries(chronon3d_core_tests PRIVATE chronon3d_content)
endif()

# Azione 18 — AnimTypewriter silent-failure P0 #3 regression lock.
# Pure static-source grep test: fails loud if the canonical
# `[AnimTypewriter]` spdlog::error tag disappears from
# content/animation_compositions.cpp OR if the silent-degrade
# pattern re-emerges.  No runtime deps (no font engine, no
# SoftwareRenderer, no threads, no time, no PRNG) — pure
# cat-2 freeze-friendly source-text sniff.  See
# content/animation_compositions.cpp:98-103 for the canonical
# emit that this test guards.
#
# Gating rules (defensive): the test is registered only when
# (a) chronon3d test-suite targets are enabled AND
# (b) the production source file exists on disk.
# Rationale: the test does NOT link against the content module
# (it reads the source via fstream).  So `CHRONON3D_BUILD_CONTENT`
# is unnecessarily restrictive — the test is runnable on any
# preset that has tests enabled.  The source-file existence check
# avoids adding the test in trimmed-down source distributions
# where the content directory is absent.
#
# Compile-time path injection: CONTENT_ANIMATION_COMPOSITIONS_PATH
# makes the test CWD-independent (ctest may run from the build dir,
# not the project root).
if(CHRONON3D_BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/content/animation_compositions.cpp")
    target_sources(chronon3d_core_tests PRIVATE text/test_anim_typewriter_error_path.cpp)
    target_compile_definitions(chronon3d_core_tests PRIVATE
        CONTENT_ANIMATION_COMPOSITIONS_PATH="${CMAKE_SOURCE_DIR}/content/animation_compositions.cpp"
    )
endif()


