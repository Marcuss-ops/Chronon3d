# -- Timeline tests --
# Unit tests for the chronon3d/timeline/ header-only descriptor
# surface.  TIER=UNIT because the tests exercise pure struct + factory
# contracts (RenderJob + VideoSettings + TypedCompositionDescriptor
# decode/validate/factory) with no rendering backend, no Blend2D, and
# no FontEngine.  Default link contract (chronon3d_pipeline) is the
# OBJECT aggregate of every per-subsystem .o, which is enough to
# resolve the public headers.
#
# Locks three contracts:
#   * test_render_job_video.cpp — post-rename RenderJob::video_job
#     factory + VideoSettings default-initialization.
#   * test_composition_descriptor_decode.cpp / test_props_codec.cpp —
#     ValueMap → typed Props decode and schema introspection.
#   * test_composition_descriptor_prepare.cpp — type-erased decode,
#     validation and dynamic metadata resolution before factory creation.
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
        timeline/test_composition_descriptor_prepare.cpp
)
