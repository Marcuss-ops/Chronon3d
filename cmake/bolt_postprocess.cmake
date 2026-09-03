# Canonical BOLT post-link target for release-pgo-thinlto-bolt.
# BOLT consumes an ELF executable plus perf2bolt .fdata; static SDK archives
# are intentionally excluded from this post-link optimization boundary.

find_program(LLVM_BOLT_EXECUTABLE NAMES llvm-bolt bolt)
if(NOT LLVM_BOLT_EXECUTABLE)
    message(FATAL_ERROR
        "CHRONON3D_BOLT_POSTPROCESS requires llvm-bolt on PATH. "
        "Use release-pgo-thinlto when BOLT is unavailable."
    )
endif()

if(NOT TARGET chronon3d_cli)
    message(FATAL_ERROR
        "CHRONON3D_BOLT_POSTPROCESS requires CHRONON3D_BUILD_CLI=ON"
    )
endif()

if(NOT CHRONON3D_BOLT_DATA_PATH)
    message(FATAL_ERROR
        "CHRONON3D_BOLT_DATA_PATH must point to perf2bolt .fdata"
    )
endif()

add_custom_target(bolt-postprocess
    COMMAND ${LLVM_BOLT_EXECUTABLE}
            $<TARGET_FILE:chronon3d_cli>
            -data ${CHRONON3D_BOLT_DATA_PATH}
            -o $<TARGET_FILE:chronon3d_cli>.bolt
            -relocs
    DEPENDS chronon3d_cli
    COMMENT "BOLT post-link optimization for chronon3d_cli"
    VERBATIM
)
