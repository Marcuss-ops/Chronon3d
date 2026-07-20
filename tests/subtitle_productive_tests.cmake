# tests/subtitle_productive_tests.cmake
# ─────────────────────────────────────────────────────────────────────
# Productive subtitle support tests.
#
# Covers:
#   - SubtitleTrack / SubtitleCue / TimedWord / WordStyleState types
#   - SRT / WebVTT / JSON subtitle adapters
#   - Active word highlight logic
#   - 8+ subtitle presets in the registry
#   - Authoring API (Layer::subtitles + SubtitleTrackBuilder)
# ─────────────────────────────────────────────────────────────────────

chronon3d_add_test_suite(
    NAME chronon3d_subtitle_productive_tests
    TIER UNIT
    LINK_TARGETS chronon3d_pipeline
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/text/test_subtitle_productive.cpp
    LABELS text ungated
)
