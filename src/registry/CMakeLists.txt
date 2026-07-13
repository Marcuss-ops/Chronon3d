# ==============================================================================
# src/registry/CMakeLists.txt — chronon3d_registry
# ==============================================================================

add_library(chronon3d_registry OBJECT
    sampler_registry.cpp
    shape_registry.cpp
    source_registry.cpp
    text_preset_registry.cpp
    # Phase 3.2 — AnimatorResolver implementations moved out of the public
    # header (include/chronon3d/registry/animator_resolver.hpp) into this
    # .cpp so the header is declaration-only. The three static bodies
    # (spec_is_rich + rich_paint_anchor + compose_for — 22-branch
    # resolver table) now link from this OBJECT library.
    animator_resolver.cpp
    # M1.5#13 (1/4) — per-category text-preset factory TUs.  Each TU
    # exports `create_text_presets()` in its own
    # `register_helpers_internal::factory_<category>` namespace (no
    # ODR collision).  Step 1/4 ships only the basic category; steps
    # 2/3/4 will add kinetic / cinematic / social respectively.
    text_preset_factories_basic.cpp
    # FASE 1 — Reveal-category factory TU.  Originally 10 entries in a
    # single 518-LoC `text_preset_factories_reveal.cpp`; split into 3 TUs
    # (basic sub-bucket + selectors sub-bucket + thin aggregator) so each
    # file carries a single ownership scope:
    #   • factory_reveal_basic    → 6 entrance-animation presets
    #     (text_animations / fade_in / blur_in / slide_up / slide_down / scale_in)
    #   • factory_reveal_selectors → 4 selector-driven presets
    #     (tracking_close / masked_line_reveal / word_cascade / character_cascade)
    #   • factory_reveal          → 10-entry aggregator (forward-decls only,
    #     concatenates the 2 sub-bucket vectors in canonical Reveal order).
    # All three live in the SAME `factory_reveal` namespace; symbol distinc-
    # tion (`create_basic_reveal_presets` / `create_selector_reveal_presets`
    # vs `create_text_presets`) avoids ODR collision.  No change to the
    # consumer `text_preset_registry.cpp:202` (still calls
    # `register_helpers_internal::factory_reveal::create_text_presets()`).
    text_preset_factories_reveal_basic.cpp
    text_preset_factories_reveal_selectors.cpp
    text_preset_factories_reveal.cpp
    # FASE 1 — Cinematic-category factory TU (4 entries extracted from
    # text_preset_registry.cpp).
    text_preset_factories_cinematic.cpp
    # FASE 1 — Emphasis-category factory TU (4 entries extracted from
    # text_preset_registry.cpp).
    text_preset_factories_emphasis.cpp
)
set_target_properties(chronon3d_registry PROPERTIES UNITY_BUILD OFF)
target_link_libraries(chronon3d_registry PUBLIC chronon3d)
target_include_directories(chronon3d_registry PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_precompile_headers(chronon3d_registry PRIVATE <chronon3d/detail/pch.hpp>)
