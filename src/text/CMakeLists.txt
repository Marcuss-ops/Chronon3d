# src/text/CMakeLists.txt — Core text module (non-backend-specific)
# Exposes: chronon3d_text_core

add_library(chronon3d_text_core OBJECT
    animated_text_document.cpp
    animation/glyph_instance_state.cpp
    animation/text_animator_evaluator.cpp
    animation/text_animator_stack.cpp
    animation/text_property_applier.cpp
    animation/text_tracking_evaluator.cpp
    animation/text_pre_shaping.cpp
    every_line_composer.cpp
    glyph_selector.cpp
    path_sampler.cpp
    # TICKET-104 -- INTERNAL PendingTextRun::consumed real-decrement
    # helper.  Provides `text_internal::mark_consumed()` + diagnostic
    # counter.  Under `src/text/`, NOT in `include/chronon3d/`, per
    # AGENTS.md v0.1 cat-3 freeze (internal hygiene, zero public surface
    # expansion).  See src/text/pending_text_run_impl.hpp for the
    # contract.
    pending_text_run_impl.cpp
    single_line_composer.cpp
    text_document.cpp
    text_document_builder.cpp
    # FASE 4b — timed text model + adapters (SRT/VTT/JSON → TimedTextDocument)
    timed_text_document.cpp
    timed_text_adapter_srt.cpp
    timed_text_adapter_vtt.cpp
    timed_text_adapter_json.cpp
    text_path_composer.cpp
    # M1.5#8 — text_resolver.cpp monolith DELETED + content split under
    # src/text/resolver/ (4 single-responsibility sub-cpps sharing an
    # INTERNAL-only header suite; see src/text/resolver/font_resolver.hpp).
    #   - text_run_resolver.cpp   unico entry pubblico orchestrator (resolve_text_run_tree + resolve_and_shape + shape_resolved_run)
    #   - text_span_resolver.cpp   split_by_font + effective_font (span override stack)
    #   - text_bidi_resolver.cpp   bidi emission per font-homogeneous sub-range
    #   - text_font_resolver.cpp   FontResolver::resolve + [[deprecated]] resolve_fallback_fonts impl
    resolver/text_run_resolver.cpp
    resolver/text_span_resolver.cpp
    resolver/text_bidi_resolver.cpp
    resolver/text_font_resolver.cpp
    # M1.5#4 — text_run.cpp DECOMPOSED into three sub-files.
    # The original monolithic text_run.cpp has been removed;
    # sub-files must be compiled as separate TUs.
    text_run_hash.cpp
    text_layout_cache.cpp
    text_layout_identity.cpp
    text_run_builder.cpp
    # M1.5#5 — split text_run_builder.cpp orchestrator into 4
    # single-responsibility sub-cpp helpers under compiler/.  Each
    # file owns one of the canonical 9 stages of the pipeline
    # (validate request + MissingFont check → shape every run +
    # apply failure policy + all-run guard → compose paragraph +
    # apply composition to placed + concatenate → populate font
    # spans).  Bodies verbatim moves from the pre-M1.5#5 monolith;
    # no behavioural changes.  Internal-only — NOT in
    # include/chronon3d/, NOT exposed to SDK consumers
    # (AGENTS.md v0.1 cat-3 freeze-compliant surface).
    compiler/text_compile_validation.cpp
    compiler/text_run_shaping.cpp
    compiler/text_run_composition.cpp
    compiler/text_font_span_builder.cpp
    # M1.5#2 — text_run_driver.cpp slimmed into an orchestrator that
    # delegates the slow-path / prewarm stages to single-responsibility
    # helpers under driver/.  Internal-only (NOT in include/chronon3d/).
    text_run_driver.cpp
    driver/text_state_sampler.cpp
    driver/text_font_state.cpp
    driver/text_layout_rebuild.cpp
    text_run_geometry.cpp
    unicode/utf8_decoder.cpp
    unicode/whitespace.cpp
    boundary_resolver/text_unit_map.cpp
    # TICKET-108a — internal text_resolver_helpers (adopt aliases.hpp 7
    # typedefs through internal-only helper functions; not installed).
    internal/text_resolver_helpers.cpp
)

# ── Unity-build exclusion ────────────────────────────────────────────────
# Two text-core TUs clash when bundled into a single Unity chunk:
#   - timed_text_document.cpp defines anonymous-namespace hash helpers
#     (hcombine / hbytes / hstring / hval) that are also defined in one
#     of the timed_text_adapter_*.cpp files compiled in the same Unity
#     group.  Compiling as separate TUs eliminates the duplicate-symbol
#     redefinition error.
#   - boundary_resolver/text_unit_map.cpp + glyph_selector.cpp end up
#     in the same Unity chunk, and both pull in a different definition
#     of `class chronon3d::TextUnitMap` (one in text_unit_map.hpp, the
#     legacy one in glyph_selector.hpp).  Same fix: compile as separate
#     TUs to break the ODR-violation-at-compile-time.
# This is a build-system workaround for pre-existing TU-local name
# collisions; it does NOT change the public API surface and does NOT
# alter the semantics of the text subsystem.  Tracked under the
# "Correzioni di build" exception of the v0.1 feature freeze.
set_source_files_properties(
    timed_text_document.cpp
    boundary_resolver/text_unit_map.cpp
    PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON
)

# NOTE: chronon3d_text_core does NOT declare its own PUBLIC include directory
# here.  Public header access is already provided transitively through the
# PUBLIC link to `chronon3d` (which has both $<BUILD_INTERFACE:…> and
# $<INSTALL_INTERFACE:include>).  Adding a redundant PUBLIC include dir here
# would leak the source-tree path when consumers try to import this target
# from the installed Chronon3DTargets.cmake.
#
# PRIVATE link to chronon3d_cache so cross-module `<chronon3d/cache/*.hpp>`
# headers (under chronon3d_cache's `include_private/`) become visible at
# compile time.  The cache target exposes include_private via INTERFACE
# $<BUILD_INTERFACE:…>, so PRIVATE-link consumers automatically gain
# `<chronon3d/cache/lru_cache.hpp>` etc. without leaking internal headers
# to downstreams.  Required for `src/text/text_run_hash.cpp` which uses the
# generic LRU utility without coupling to chronon3d_cache's whole interface.
target_link_libraries(chronon3d_text_core
    PUBLIC  chronon3d
    PRIVATE chronon3d_cache
)

# PRIVATE include of ${CMAKE_SOURCE_DIR} so that source-relative
# includes (e.g. "src/text/internal/text_resolver_helpers.hpp")
# resolve during external-consumer builds (install_consumer_test).
# The path is PRIVATE — it does not leak to downstream consumers.
target_include_directories(chronon3d_text_core PRIVATE ${CMAKE_SOURCE_DIR})
