# tests/text_rich_authoring_tests.cmake
# Rich text span builder end-to-end tests.
#
# Locks the authoring::Text span builder API and the runtime lowering
# of TextSpanOverride entries into TextDocument spans.  Pure struct +
# adapter inspection — no rendering backend required.

chronon3d_add_test_suite(
    NAME chronon3d_text_rich_authoring_tests
    TIER UNIT
    LINK_TARGETS chronon3d_pipeline
    SOURCES text/test_text_rich_authoring.cpp
    LABELS text ungated
)
