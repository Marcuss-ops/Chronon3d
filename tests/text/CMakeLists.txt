# tests/text/CMakeLists.txt — Standalone focused text test bundles
#
# All targets use chronon3d_add_test_suite so registration remains canonical
# and targeted CTest filters work without inflating the core test binary.

if(CHRONON3D_BUILD_TESTS)

# 1. Alignment isolation
chronon3d_add_test_suite(
    NAME  chronon3d_text_alignment_isolated_tests
    TIER  INTEGRATION
    SOURCES
        ${CMAKE_CURRENT_LIST_DIR}/test_text_alignment_isolated.cpp
    LINK_TARGETS
        chronon3d_pipeline
        chronon3d_backend_software
        chronon3d_visual_test_support
)

# 2. Auto-fit isolation
chronon3d_add_test_suite(
    NAME  chronon3d_text_auto_fit_tests
    TIER  UNIT
    SOURCES
        ${CMAKE_CURRENT_LIST_DIR}/test_text_auto_fit.cpp
    LINK_TARGETS
        chronon3d_pipeline
)

# 3. Unicode line breaking
chronon3d_add_test_suite(
    NAME  chronon3d_text_unicode_line_breaking_tests
    TIER  UNIT
    SOURCES
        ${CMAKE_CURRENT_LIST_DIR}/test_unicode_line_breaking.cpp
    LINK_TARGETS
        chronon3d_text_core
        chronon3d_pipeline
        chronon3d_backend_software
)

# 4. Oversized-text clip false-green audit
chronon3d_add_test_suite(
    NAME  chronon3d_text_clip_oversized_tests
    TIER  INTEGRATION
    SOURCES
        ${CMAKE_CURRENT_LIST_DIR}/test_text_clip_oversized.cpp
    LINK_TARGETS
        chronon3d_pipeline
        chronon3d_backend_software
        chronon3d_visual_test_support
)

# 5. Expressive per-letter word-emphasis factory
chronon3d_add_test_suite(
    NAME  chronon3d_text_word_emphasis_animators_tests
    TIER  UNIT
    SOURCES
        ${CMAKE_CURRENT_LIST_DIR}/test_word_emphasis_animators.cpp
    LINK_TARGETS
        chronon3d_text_core
)

# 6. Constant-cost semantic profiles
#    Base / important phrase / special name / important word. The suite locks
#    the maximum selector, property and keyframe budget instead of relying on
#    noisy wall-clock thresholds.
chronon3d_add_test_suite(
    NAME  chronon3d_text_lightweight_emphasis_tests
    TIER  UNIT
    SOURCES
        ${CMAKE_CURRENT_LIST_DIR}/test_lightweight_emphasis_animators.cpp
    LINK_TARGETS
        chronon3d_text_core
)

# 7. SubtitleTrackBuilder explicit font references (no implicit engine
#    default font path). The builder must never re-introduce a
#    repository-relative default like `assets/fonts/Poppins-Bold.ttf`.
chronon3d_add_test_suite(
    NAME  chronon3d_subtitle_font_ref_tests
    TIER  UNIT
    SOURCES
        ${CMAKE_CURRENT_LIST_DIR}/test_subtitle_font_ref.cpp
    LINK_TARGETS
        chronon3d_pipeline
)

endif()  # CHRONON3D_BUILD_TESTS
