# ═══════════════════════════════════════════════════════════════════════════
# tests/cmake/text/text_transforms.cmake
#
# TICKET-REFACTOR-TESTS-SPLIT-18-19 §B — transforms cluster
# (ambito 16 of the 17: 6 TICKET-FASE2-TRANSFORMS-ANIMATION §10 tests).
#
# Aggregated by tests/text_golden_tests.cmake.
# ═══════════════════════════════════════════════════════════════════════════

# 01_rotate_z — Z-axis rotation in the canvas plane (3 rotations × 2 ARs).
target_sources(chronon3d_text_golden_tests
    PRIVATE
        text_golden/text_transforms_animation/01_rotate_z_not_cut.cpp
)
add_test(
    NAME TextRotateZ
    COMMAND chronon3d_text_golden_tests --test-case="TextTransforms.RotateZ *"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# 02_scale — uniform 0.5× / 1.5× / 2.0× + non-uniform 0.96×1.04.
target_sources(chronon3d_text_golden_tests
    PRIVATE
        text_golden/text_transforms_animation/02_scale.cpp
)
add_test(
    NAME TextTransformsScale
    COMMAND chronon3d_text_golden_tests --test-case="TextTransforms.Scale_*"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# 03_anchor — TopLeft / TopRight / BottomLeft / BottomRight.
target_sources(chronon3d_text_golden_tests
    PRIVATE
        text_golden/text_transforms_animation/03_anchor.cpp
)
add_test(
    NAME TextTransformsAnchor
    COMMAND chronon3d_text_golden_tests --test-case="TextTransforms.Anchor_*"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# 04_parent_transform — parent at +500 X / -300 X.
target_sources(chronon3d_text_golden_tests
    PRIVATE
        text_golden/text_transforms_animation/04_parent_transform.cpp
)
add_test(
    NAME TextTransformsParent
    COMMAND chronon3d_text_golden_tests --test-case="TextTransforms.ParentTransform_*"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# 05_rotation_extended — negative + zero angles.
target_sources(chronon3d_text_golden_tests
    PRIVATE
        text_golden/text_transforms_animation/05_rotation_extended.cpp
)
add_test(
    NAME TextTransformsRotationExt
    COMMAND chronon3d_text_golden_tests --test-case="TextTransforms.RotateZ_m*"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# 06_2_5d_camera — 2.5D / 3D-enabled text.
target_sources(chronon3d_text_golden_tests
    PRIVATE
        text_golden/text_transforms_animation/06_2_5d_camera.cpp
)
add_test(
    NAME TextTransforms2_5D
    COMMAND chronon3d_text_golden_tests --test-case="TextTransforms.TwoPointFiveD_*"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
