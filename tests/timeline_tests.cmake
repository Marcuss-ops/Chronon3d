# -- Timeline tests --
# Pure unit coverage for RenderJob, CompositionDescriptor and PropsCodec.
# No renderer backend, Blend2D or FontEngine is required.
#
# Contracts:
#   * test_render_job_video.cpp — canonical RenderJob video construction.
#   * test_composition_descriptor_decode.cpp — PropsCodec is the sole external
#     ValueMap decoder; values without a codec fail instead of being ignored.
#   * test_props_codec.cpp — schema introspection and codec round-trip.
#   * test_composition_descriptor_prepare.cpp — decode, validation and dynamic
#     metadata resolution happen before composition construction.
chronon3d_add_test_suite(
    NAME chronon3d_timeline_tests
    TIER UNIT
    SOURCES
        timeline/test_render_job_video.cpp
        timeline/test_composition_descriptor_decode.cpp
        timeline/test_props_codec.cpp
        timeline/test_composition_descriptor_prepare.cpp
        timeline/test_registry_resolve.cpp
)
