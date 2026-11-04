# Deterministic golden end-to-end gate over the real CLI.
#
# The test renders three small scenes twice, hashes every raw RGBA frame and
# compares the hashes with the committed manifest. It is registered only when
# the CLI target exists in this configuration; CI builds that target before
# running the gate (see .github/workflows/ci.yml, golden-e2e job).
if(TARGET chronon3d_cli)
    add_test(
        NAME chronon3d_golden_e2e_tests
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_SOURCE_DIR}/tests/e2e/golden/run_golden_e2e.py
                --cli $<TARGET_FILE:chronon3d_cli>
    )
    set_tests_properties(chronon3d_golden_e2e_tests PROPERTIES
        LABELS "e2e;golden"
        TIMEOUT 900
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )
endif()
