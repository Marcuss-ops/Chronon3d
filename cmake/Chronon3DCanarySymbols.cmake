# Canonical SDK archive canaries: AREA|SYMBOL|GUARD|TARGET.
# Guards name real root CMake options; externalized content and retired
# compatibility flags are intentionally absent.
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
