# -- Timeline tests --
# Unit tests for the chronon3d/timeline/ header-only descriptor
# surface.  TIER=UNIT because the tests exercise pure struct + factory
# contracts (RenderJob + VideoSettings + TypedCompositionDescriptor
# decode/validate/factory) with no rendering backend, no Blend2D, and
# no FontEngine.  Default link contract (chronon3d_pipeline) is the
# OBJECT aggregate of every per-subsystem .o, which is enough to
# resolve the public headers.
#
# Locks two contracts:
#   * test_render_job_video.cpp — post-rename RenderJob::video_job
#     factory + VideoSettings default-initialization; the factory had 0
#     production call sites after the 0d1854a6 rename, so this test
#     guards against silent regressions in the canonical D1 descriptor
#     surface.
#   * test_composition_descriptor_decode.cpp — TypedCompositionDescriptor
#     .decode callback + to_descriptor() wrap (TICKET-V2-VALUEMAP-PROPS-MERGE).
#     Locks the ValueMap → typed Props merge so future refactors cannot
#     accidentally regress external CLI/JSON override routing.
#
# Per ADR-018, this file is also listed in CMAKE_CONFIGURE_DEPENDS
# at the top of tests/CMakeLists.txt.
chronon3d_add_test_suite(
    NAME chronon3d_timeline_tests
    TIER UNIT
    SOURCES
        timeline/test_render_job_video.cpp
        timeline/test_composition_descriptor_decode.cpp
        timeline/test_props_codec.cpp
)
