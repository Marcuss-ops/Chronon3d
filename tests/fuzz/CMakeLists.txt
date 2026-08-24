# ---------------------------------------------------------------------------
# tests/fuzz/CMakeLists.txt — libFuzzer targets for the Chronon3D IPC boundary
#
# Architecture:
#   production libraries (chronon3d_ipc, chronon3d)
#           ↑
#           │
#   fuzz targets (standalone, never linked from production code)
#
# Gate: CHRONON3D_BUILD_FUZZERS=ON (OFF by default).
# Compiler: Clang only (libFuzzer is a Clang built-in).
# ---------------------------------------------------------------------------

if(NOT CHRONON3D_BUILD_FUZZERS)
    return()
endif()

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR
        "CHRONON3D_BUILD_FUZZERS requires Clang.  "
        "Detected compiler: ${CMAKE_CXX_COMPILER_ID}.  "
        "libFuzzer is a Clang built-in (-fsanitize=fuzzer).")
endif()

# chronon3d_ipc OBJECT library transitively depends on flatbuffers;
# OBJECT libraries do not propagate link dependencies so we must
# find_package explicitly for the fuzz targets.
find_package(flatbuffers CONFIG REQUIRED)

# The fuzz targets compile ipc_codec.cpp directly (NOT via the chronon3d_ipc
# OBJECT target, which would pull in composition_session.cpp → full engine).
# So they need their OWN flatc generation of the IPC schema — self-contained,
# independent of the production target's build graph.
find_program(CHRONON3D_FLATC flatc REQUIRED)
set(FUZZ_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(FUZZ_GENERATED_HEADER "${FUZZ_GENERATED_DIR}/chronon_ipc_generated.h")

add_custom_command(
    OUTPUT "${FUZZ_GENERATED_HEADER}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${FUZZ_GENERATED_DIR}"
    COMMAND "${CHRONON3D_FLATC}"
        --cpp
        -o "${FUZZ_GENERATED_DIR}"
        "${CMAKE_SOURCE_DIR}/schema/chronon_ipc.fbs"
    DEPENDS "${CMAKE_SOURCE_DIR}/schema/chronon_ipc.fbs"
    COMMENT "[fuzz] Compiling FlatBuffers schema: chronon_ipc.fbs → C++"
    VERBATIM
)

# ── Shared include directories for all fuzz targets ────────────────────
set(FUZZ_INCLUDE_DIRS
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src/ipc
    ${FUZZ_GENERATED_DIR}
)

# ── Seed corpus generator ──────────────────────────────────────────────
# Generates valid FlatBuffers frames (Status, CreateComposition, RenderFrame,
# Shutdown) so the fuzzer starts from realistic inputs.
#
# Links only ipc_codec.cpp directly (not chronon3d_ipc OBJECT library) to
# avoid pulling in composition_session.cpp which depends on the full engine
# (RenderEngine, Scene, compile_composition, etc.).
add_executable(generate_ipc_fuzz_corpus
    generate_corpus.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc/ipc_codec.cpp
    ${FUZZ_GENERATED_HEADER}
)
target_link_libraries(generate_ipc_fuzz_corpus PRIVATE
    flatbuffers::flatbuffers
)
target_include_directories(generate_ipc_fuzz_corpus PRIVATE ${FUZZ_INCLUDE_DIRS})

set(CORPUS_DIR "${CMAKE_CURRENT_BINARY_DIR}/corpus/ipc")
set(CORPUS_SEEDS
    "${CORPUS_DIR}/status.bin"
    "${CORPUS_DIR}/create_composition.bin"
    "${CORPUS_DIR}/render_frame.bin"
    "${CORPUS_DIR}/shutdown.bin"
)

add_custom_command(
    OUTPUT ${CORPUS_SEEDS}
    COMMAND generate_ipc_fuzz_corpus "${CORPUS_DIR}"
    DEPENDS generate_ipc_fuzz_corpus
    COMMENT "[fuzz] Generating IPC seed corpus → ${CORPUS_DIR}"
)

# ── IPC codec fuzz target ──────────────────────────────────────────────
# Links ipc_codec.cpp directly (not chronon3d_ipc OBJECT library) to avoid
# pulling in composition_session.cpp which depends on the full engine
# (RenderEngine, Scene, compile_composition, etc.).  The fuzz target only
# exercises the encode/decode boundary — no engine components needed.
add_executable(ipc_codec_fuzz
    ipc_codec_fuzz.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc/ipc_codec.cpp
    ${FUZZ_GENERATED_HEADER}
    ${CORPUS_SEEDS}
)
target_compile_options(ipc_codec_fuzz PRIVATE
    -fsanitize=fuzzer,address,undefined
    -fno-omit-frame-pointer
    -g
)
target_link_options(ipc_codec_fuzz PRIVATE
    -fsanitize=fuzzer,address,undefined
)
target_link_libraries(ipc_codec_fuzz PRIVATE
    flatbuffers::flatbuffers
)
target_include_directories(ipc_codec_fuzz PRIVATE ${FUZZ_INCLUDE_DIRS})
set_target_properties(ipc_codec_fuzz PROPERTIES UNITY_BUILD OFF)

# ── Expression parser fuzz target ─────────────────────────────────────
# Header-only recursive-descent parser (include/chronon3d/math/expression.hpp).
# No engine, no backend, no third-party link deps — just the public header.
add_executable(maybe_expression_fuzz maybe_expression_fuzz.cpp)
target_compile_options(maybe_expression_fuzz PRIVATE
    -fsanitize=fuzzer,address,undefined
    -fno-omit-frame-pointer
    -g
)
target_link_options(maybe_expression_fuzz PRIVATE
    -fsanitize=fuzzer,address,undefined
)
target_include_directories(maybe_expression_fuzz PRIVATE ${CMAKE_SOURCE_DIR}/include)
set_target_properties(maybe_expression_fuzz PROPERTIES UNITY_BUILD OFF)

# ── Composition descriptor fuzz target ─────────────────────────────────
# Exercises nlohmann::json parse + typed field extraction that mirrors
# CompositionSession::create_composition.  Header-only nlohmann_json, no engine.
add_executable(composition_descriptor_fuzz composition_descriptor_fuzz.cpp)
target_compile_options(composition_descriptor_fuzz PRIVATE
    -fsanitize=fuzzer,address,undefined
    -fno-omit-frame-pointer
    -g
)
target_link_options(composition_descriptor_fuzz PRIVATE
    -fsanitize=fuzzer,address,undefined
)
target_link_libraries(composition_descriptor_fuzz PRIVATE
    nlohmann_json::nlohmann_json
)
target_include_directories(composition_descriptor_fuzz PRIVATE ${CMAKE_SOURCE_DIR}/include)
set_target_properties(composition_descriptor_fuzz PROPERTIES UNITY_BUILD OFF)

# ── Regression tests ──────────────────────────────────────────────────
# Every crash discovered by a fuzzer becomes a permanent regression test.
# Drop the crash input into tests/fuzz/regressions/<target>/ and CMake
# auto-registers a CTest test that replays it once under ASan+UBSan.
#
# Semantics: a clean exit (0) = the crash is still fixed; a sanitizer abort
# (non-zero) = the bug regressed and the test FAILS.
#
# Layout:
#   regressions/ipc_codec/             → ipc_codec_fuzz
#   regressions/maybe_expression/      → maybe_expression_fuzz
#   regressions/composition_descriptor/ → composition_descriptor_fuzz
#
# These are registered as CTest tests even when CHRONON3D_BUILD_TESTS=OFF
# because the fuzz targets are a standalone build gate.

function(chronon3d_add_fuzz_regressions target_name regressions_subdir)
    file(GLOB _inputs CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/regressions/${regressions_subdir}/*")
    list(FILTER _inputs EXCLUDE REGEX "\\.gitkeep$")
    foreach(_input IN LISTS _inputs)
        # NAME_WE strips the extension (e.g. heap_overflow_decode_reply.bin
        # → heap_overflow_decode_reply) for a clean test name.
        get_filename_component(_input_name "${_input}" NAME_WE)
        add_test(
            NAME "fuzz_regression_${target_name}_${_input_name}"
            COMMAND $<TARGET_FILE:${target_name}> "${_input}" -runs=1
        )
        set_tests_properties("fuzz_regression_${target_name}_${_input_name}"
            PROPERTIES
            LABELS "fuzz;fuzz-regression"
        )
    endforeach()
endfunction()

chronon3d_add_fuzz_regressions(ipc_codec_fuzz ipc_codec)
chronon3d_add_fuzz_regressions(maybe_expression_fuzz maybe_expression)
chronon3d_add_fuzz_regressions(composition_descriptor_fuzz composition_descriptor)

# ── Aggregate target ───────────────────────────────────────────────────
# Convenience target that runs every fuzz regression test via CTest.
# Build it with:  cmake --build . --target fuzz_regressions
add_custom_target(fuzz_regressions
    COMMAND ${CMAKE_CTEST_COMMAND} -L fuzz-regression --output-on-failure
    DEPENDS ipc_codec_fuzz maybe_expression_fuzz composition_descriptor_fuzz
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    USES_TERMINAL
    COMMENT "Running fuzz regression corpus (ctest -L fuzz-regression)"
)
