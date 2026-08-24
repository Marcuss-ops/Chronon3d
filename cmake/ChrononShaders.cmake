# ChrononShaders.cmake — Canonical shader pipeline for Chronon3D
#
# chronon_add_compute_shader(
#     NAME <name>
#     SOURCE <path_to_.comp>
# )
#
# Pipeline: .comp → glslangValidator → spirv-val → spirv-opt → final .spv → embed → .hpp
# Outputs: ${CHRONON3D_VULKAN_GENERATED_DIR}/<name>.comp.spv
#          ${CHRONON3D_VULKAN_GENERATED_DIR}/<name>_comp_spv.hpp
# Sets:    chronon3d_<name>_spv     → path to the .spv file
#          chronon3d_<name>_header  → path to the generated .hpp header
#
# When CHRONON3D_ENABLE_VULKAN is OFF, this file is a no-op.
# When CHRONON3D_SPIRV_VALIDATE is ON (default: ON), spirv-val is run and
# the build fails on invalid SPIR-V.  When CHRONON3D_SPIRV_OPTIMIZE is ON
# (default: ON), spirv-opt -O is applied for deterministic, optimized output.

include_guard(GLOBAL)

# Guard: only active when the Vulkan backend is built
if(NOT CHRONON3D_ENABLE_VULKAN)
    return()
endif()

# ---------------------------------------------------------------------------
# Detect tools
# ---------------------------------------------------------------------------
find_program(CHRONON3D_GLSLANG_VALIDATOR glslangValidator REQUIRED)

option(CHRONON3D_SPIRV_VALIDATE "Validate SPIR-V with spirv-val during build" ON)
option(CHRONON3D_SPIRV_OPTIMIZE "Optimize SPIR-V with spirv-opt during build" ON)

# Resolve the vcpkg tools directory so spirv-val/spirv-opt are found even
# when not on the global PATH (vcpkg [tools] feature installs them under
# ${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/tools/spirv-tools/).
set(_chronon3d_spirv_tools_hints "")
if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
    list(APPEND _chronon3d_spirv_tools_hints
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/tools/spirv-tools")
endif()

if(CHRONON3D_SPIRV_VALIDATE)
    find_program(CHRONON3D_SPIRV_VAL spirv-val HINTS ${_chronon3d_spirv_tools_hints})
    if(NOT CHRONON3D_SPIRV_VAL)
        message(WARNING "CHRONON3D_SPIRV_VALIDATE=ON but spirv-val not found (install spirv-tools[tools])")
        set(CHRONON3D_SPIRV_VALIDATE OFF)
    endif()
endif()

if(CHRONON3D_SPIRV_OPTIMIZE)
    find_program(CHRONON3D_SPIRV_OPT spirv-opt HINTS ${_chronon3d_spirv_tools_hints})
    if(NOT CHRONON3D_SPIRV_OPT)
        message(WARNING "CHRONON3D_SPIRV_OPTIMIZE=ON but spirv-opt not found (install spirv-tools[tools])")
        set(CHRONON3D_SPIRV_OPTIMIZE OFF)
    endif()
endif()

# ---------------------------------------------------------------------------
# chronon_embed_spirv_to_header
# ---------------------------------------------------------------------------
function(chronon_embed_spirv_to_header)
    set(_options "")
    set(_one_value_args INPUT OUTPUT SYMBOL)
    set(_multi_value_args "")
    cmake_parse_arguments(EMBED "${_options}" "${_one_value_args}" "${_multi_value_args}" ${ARGN})

    if(NOT EMBED_INPUT OR NOT EMBED_OUTPUT OR NOT EMBED_SYMBOL)
        message(FATAL_ERROR "chronon_embed_spirv_to_header: INPUT, OUTPUT, and SYMBOL are required")
    endif()

    add_custom_command(
        OUTPUT "${EMBED_OUTPUT}"
        COMMAND ${CMAKE_COMMAND}
            "-DINPUT=${EMBED_INPUT}"
            "-DOUTPUT=${EMBED_OUTPUT}"
            "-DSYMBOL=${EMBED_SYMBOL}"
            -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src/backends/vulkan/embed_spirv.cmake"
        DEPENDS "${EMBED_INPUT}" "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src/backends/vulkan/embed_spirv.cmake"
        COMMENT "Embedding SPIR-V: ${EMBED_INPUT} → ${EMBED_OUTPUT}"
        VERBATIM
    )
endfunction()

# ---------------------------------------------------------------------------
# chronon_add_compute_shader — single-source shader compilation
# ---------------------------------------------------------------------------
function(chronon_add_compute_shader)
    set(_options "")
    set(_one_value_args NAME SOURCE)
    set(_multi_value_args "")
    cmake_parse_arguments(SHADER "${_options}" "${_one_value_args}" "${_multi_value_args}" ${ARGN})

    if(NOT SHADER_NAME OR NOT SHADER_SOURCE)
        message(FATAL_ERROR "chronon_add_compute_shader: NAME and SOURCE are required")
    endif()

    if(NOT CHRONON3D_VULKAN_GENERATED_DIR)
        message(FATAL_ERROR "chronon_add_compute_shader: CHRONON3D_VULKAN_GENERATED_DIR is not set. "
                            "Call: set(CHRONON3D_VULKAN_GENERATED_DIR \"${CMAKE_CURRENT_BINARY_DIR}/generated\" CACHE INTERNAL \"\")")
    endif()

    # Canonical output paths
    set(_raw_spv     "${CHRONON3D_VULKAN_GENERATED_DIR}/${SHADER_NAME}.comp.raw.spv")
    set(_valid_spv   "${CHRONON3D_VULKAN_GENERATED_DIR}/${SHADER_NAME}.comp.valid.spv")
    set(_final_spv   "${CHRONON3D_VULKAN_GENERATED_DIR}/${SHADER_NAME}.comp.spv")
    set(_header      "${CHRONON3D_VULKAN_GENERATED_DIR}/${SHADER_NAME}_comp_spv.hpp")
    set(_symbol      "chronon3d_${SHADER_NAME}_comp_spv")

    # Stage 1: GLSL → SPIR-V
    add_custom_command(
        OUTPUT "${_raw_spv}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CHRONON3D_VULKAN_GENERATED_DIR}"
        COMMAND "${CHRONON3D_GLSLANG_VALIDATOR}" -V "${SHADER_SOURCE}" -o "${_raw_spv}"
        DEPENDS "${SHADER_SOURCE}"
        COMMENT "Compiling ${SHADER_NAME}.comp → SPIR-V"
        VERBATIM
    )

    # Stage 2: spirv-val (optional)
    if(CHRONON3D_SPIRV_VALIDATE)
        add_custom_command(
            OUTPUT "${_valid_spv}"
            COMMAND "${CHRONON3D_SPIRV_VAL}" "${_raw_spv}"
            COMMAND ${CMAKE_COMMAND} -E copy "${_raw_spv}" "${_valid_spv}"
            DEPENDS "${_raw_spv}"
            COMMENT "Validating ${SHADER_NAME}.comp SPIR-V"
            VERBATIM
        )
        set(_opt_input "${_valid_spv}")
    else()
        set(_opt_input "${_raw_spv}")
    endif()

    # Stage 3: spirv-opt (optional)
    if(CHRONON3D_SPIRV_OPTIMIZE)
        add_custom_command(
            OUTPUT "${_final_spv}"
            COMMAND "${CHRONON3D_SPIRV_OPT}" -O "${_opt_input}" -o "${_final_spv}"
            DEPENDS "${_opt_input}"
            COMMENT "Optimizing ${SHADER_NAME}.comp SPIR-V"
            VERBATIM
        )
    else()
        add_custom_command(
            OUTPUT "${_final_spv}"
            COMMAND ${CMAKE_COMMAND} -E copy "${_opt_input}" "${_final_spv}"
            DEPENDS "${_opt_input}"
            COMMENT "Copying ${SHADER_NAME}.comp SPIR-V (validation only)"
            VERBATIM
        )
    endif()

    # Stage 4: Embed SPIR-V → C++ header
    chronon_embed_spirv_to_header(
        INPUT  "${_final_spv}"
        OUTPUT "${_header}"
        SYMBOL "${_symbol}"
    )

    # Expose output paths to caller via parent-scope variables
    set(chronon3d_${SHADER_NAME}_spv    "${_final_spv}" PARENT_SCOPE)
    set(chronon3d_${SHADER_NAME}_header "${_header}"    PARENT_SCOPE)
endfunction()

# ---------------------------------------------------------------------------
# chronon_collect_shader_headers — collect all generated headers into a list
# ---------------------------------------------------------------------------
function(chronon_collect_shader_headers OUT_VAR)
    set(_headers "")
    foreach(_name ${ARGN})
        list(APPEND _headers "${chronon3d_${_name}_header}")
    endforeach()
    set(${OUT_VAR} "${_headers}" PARENT_SCOPE)
endfunction()