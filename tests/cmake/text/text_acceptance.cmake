# ═══════════════════════════════════════════════════════════════════════════
# tests/cmake/text/text_acceptance.cmake
#
# TICKET-REFACTOR-TESTS-SPLIT-18-19 §B — acceptance cluster
# (supplementary tests that validate the broader text V1 quality bar):
#   - TICKET-FASE3-MULTILINGUAL — 8 multilingual scripts (kerning, mixed
#     advance widths, mixed baseline, Hangul, Devanagari, Arabic, Hebrew,
#     fallback matrix)
#   - TICKET-TEXT-EXPORT-V1 — deterministic export regression
#
# These are bundled into a single "acceptance" sub-file because they are
# supplementary to the 17 ambiti and validate the system's broader
# acceptance criteria (multilingual + export parity).
#
# Aggregated by tests/text_golden_tests.cmake.
# ═══════════════════════════════════════════════════════════════════════════

# TICKET-FASE3-MULTILINGUAL — 8 multilingual text goldens.
target_sources(chronon3d_text_golden_tests
    PRIVATE
        text_golden/text_multilingual/01_kerning_pairs.cpp
        text_golden/text_multilingual/02_mixed_advance_widths.cpp
        text_golden/text_multilingual/03_mixed_baseline.cpp
        text_golden/text_multilingual/04_hangul_composition.cpp
        text_golden/text_multilingual/05_devanagari_conjuncts.cpp
        text_golden/text_multilingual/06_arabic_shaping.cpp
        text_golden/text_multilingual/07_hebrew_nikud.cpp
        text_golden/text_multilingual/08_fallback_matrix.cpp
)

# TICKET-FASE3-MULTILINGUAL ctest aliases (7 per-script + 1 fallback matrix).
add_test(
    NAME TextMultilingualKerningPairs
    COMMAND chronon3d_text_golden_tests --test-case="Multilingual.KerningPairs *"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
add_test(
    NAME TextMultilingualMixedAdvanceWidths
    COMMAND chronon3d_text_golden_tests --test-case="Multilingual.MixedAdvanceWidths *"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
add_test(
    NAME TextMultilingualMixedBaseline
    COMMAND chronon3d_text_golden_tests --test-case="Multilingual.MixedBaseline *"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
add_test(
    NAME TextMultilingualHangulComposition
    COMMAND chronon3d_text_golden_tests --test-case="Multilingual.HangulComposition *"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
add_test(
    NAME TextMultilingualDevanagariConjuncts
    COMMAND chronon3d_text_golden_tests --test-case="Multilingual.DevanagariConjuncts *"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
add_test(
    NAME TextMultilingualArabicShaping
    COMMAND chronon3d_text_golden_tests --test-case="Multilingual.ArabicShaping *"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
add_test(
    NAME TextMultilingualHebrewNikud
    COMMAND chronon3d_text_golden_tests --test-case="Multilingual.HebrewNikud *"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
add_test(
    NAME TextMultilingualFallbackMatrix
    COMMAND chronon3d_text_golden_tests --test-case="Multilingual.FallbackMatrix *"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# TICKET-TEXT-EXPORT-V1 — deterministic export regression.
target_sources(chronon3d_text_golden_tests
    PRIVATE
        text_golden/text_export/text_export_golden.cpp
)
add_test(
    NAME TextExportGolden
    COMMAND chronon3d_text_golden_tests --test-case="TextExportGolden*"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# TextGolden umbrella ctest (runs the full executable).
add_test(
    NAME TextGolden
    COMMAND chronon3d_text_golden_tests
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
