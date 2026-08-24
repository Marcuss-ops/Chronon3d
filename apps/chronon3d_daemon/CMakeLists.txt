# ── Chronon3D Daemon — persistent warm-render IPC server (ADR-024) ─────────
# FlatBuffers-based daemon keeping the RenderEngine alive across hundreds of
# jobs.  Clients speak FlatBuffers over a Unix-domain socket.
#
# The daemon migrates the warm-render logic from apps/chronon3d_cli/daemon/
# into a dedicated application boundary with zero CLI dependencies.

if(NOT CHRONON3D_BUILD_DAEMON)
    return()
endif()

add_executable(chronon3d_daemon main.cpp)
target_link_libraries(chronon3d_daemon PRIVATE
    chronon3d_ipc
    chronon3d_pipeline
    chronon3d_backend_software
)
target_compile_features(chronon3d_daemon PRIVATE cxx_std_20)
target_include_directories(chronon3d_daemon PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src/ipc
    ${CHRONON3D_IPC_GENERATED_DIR}
)

# When Vulkan is available, link its backend too so the daemon can use GPU.
if(TARGET chronon3d_backend_vulkan)
    target_link_libraries(chronon3d_daemon PRIVATE chronon3d_backend_vulkan Vulkan::Vulkan)
    target_compile_definitions(chronon3d_daemon PRIVATE CHRONON3D_ENABLE_VULKAN)
endif()
