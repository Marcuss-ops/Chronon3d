#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

BOLT = """# Canonical BOLT post-link target for the release-pgo-thinlto-bolt preset.
find_program(LLVM_BOLT_EXECUTABLE NAMES llvm-bolt bolt)
if(NOT LLVM_BOLT_EXECUTABLE)
    message(FATAL_ERROR
        "CHRONON3D_BOLT_POSTPROCESS requires llvm-bolt on PATH. "
        "Use release-pgo-thinlto when BOLT is unavailable.")
endif()

if(NOT TARGET chronon3d_cli)
    message(FATAL_ERROR "CHRONON3D_BOLT_POSTPROCESS requires CHRONON3D_BUILD_CLI=ON")
endif()
if(NOT CHRONON3D_BOLT_DATA_PATH)
    message(FATAL_ERROR "CHRONON3D_BOLT_DATA_PATH must point to a perf2bolt .fdata file")
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
"""

CANARIES = """# Canonical SDK archive canaries: AREA|SYMBOL|GUARD|TARGET.
# Guards must name real root CMake options; retired compatibility flags are forbidden.
set(CHRONON3D_SDK_CANARY_SYMBOLS
    "core|chronon3d::detail::parse_proc_stat|always|chronon3d_core_impl"
    "animations|chronon3d::temporal::generate_temporal_samples|always|chronon3d_animations"
    "scene|chronon3d::camera_v1::register_camera_v1_builtins|always|chronon3d_scene"
    "runtime|chronon3d::RenderSession::arena|always|chronon3d_runtime"
    "graph|chronon3d::graph::register_pipeline_graph_nodes|always|chronon3d_graph_pipeline"
    "software_backend|chronon3d::SoftwareRenderer::buffer_ring|always|chronon3d_backend_software"
    "text_core|chronon3d::build_text_run|CHRONON3D_ENABLE_TEXT|chronon3d_text_core"
    "diagnostics|chronon3d::renderer::diagnostics::draw_bbox_overlay|CHRONON3D_ENABLE_DIAGNOSTICS|chronon3d_backend_software_diagnostics"
    "sdk|chronon3d::sdk::RenderEngine|always|chronon3d_runtime"
    "ar_race|arch:ar_t_post_nm_non_empty|always|chronon3d_sdk_impl"
)
"""

(ROOT / "cmake" / "bolt_postprocess.cmake").write_text(BOLT, encoding="utf-8")
(ROOT / "cmake" / "Chronon3DCanarySymbols.cmake").write_text(CANARIES, encoding="utf-8")
