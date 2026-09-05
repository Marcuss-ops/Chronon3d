# ── Media/Video Tests ──
#
# These tests exercise chronon3d_media_video and chronon3d_backend_video.
#
# Per-area early-return gate (TICKET-CMAKE-TEST-MANIFEST-UNIFICATION).
if(NOT CHRONON3D_ENABLE_VIDEO)
    return()
endif()
#
# NOTE: chronon3d_backend_software is an OBJECT library whose transitive
# dependencies (chronon3d, chronon3d_graph, chronon3d_cache, chronon3d_effects,
# blend2d, backend_text) do NOT propagate via target_link_libraries.
# We must link chronon3d_pipeline explicitly to resolve all symbols.
# This matches the standard INTEGRATION tier link contract.

set(_media_video_sources
            video/test_frame_converter.cpp
            video/test_encoder_frame_pool.cpp
            video/test_converted_frame_cache.cpp
            video/test_video_diff.cpp
            video/test_long_export.cpp
            video/test_near_static_frames.cpp
            video/test_raw_video_sink_lifecycle.cpp
            video/test_raw_video_sink_submit.cpp
            video/test_raw_video_sink_planar.cpp
            video/test_raw_video_sink_edge.cpp
            video/test_media_probe.cpp
            video/test_frame_rate_video_validation.cpp
            video/reference_yuv_converter.cpp
            video/test_yuv_conversion_params.cpp
            video/test_native_frame_importer.cpp)
if(CHRONON3D_ENABLE_NATIVE_FFMPEG)
    # test_output_contract.cpp exercises MuxSession and calls the libav* C API
    # directly; those symbols only exist when the native FFmpeg mux authority
    # is built.
    list(APPEND _media_video_sources video/test_output_contract.cpp)
endif()

chronon3d_add_test_suite(
    NAME chronon3d_media_video_tests
    TIER INTEGRATION
    LINK_TARGETS chronon3d_pipeline chronon3d_backend_video chronon3d_backend_software chronon3d_media_video chronon3d_scene
    SOURCES ${_media_video_sources})

# P010 coverage has one owner per configuration. Native FFmpeg + CUDA builds
# use the dedicated suite below; CUDA-only builds keep the contract here.
# Non-CUDA builds no longer compile a vacuous CHECK(true) source.
if(CHRONON3D_ENABLE_CUDA_INTEROP AND NOT CHRONON3D_ENABLE_NATIVE_FFMPEG)
    target_sources(chronon3d_media_video_tests PRIVATE video/test_cuda_p010_conversion.cpp)
endif()

if(CHRONON3D_ENABLE_NATIVE_FFMPEG AND TARGET chronon3d_media_native)
    target_link_libraries(chronon3d_media_video_tests PRIVATE chronon3d_media_native)
endif()
if(CHRONON3D_ENABLE_CUDA_INTEROP AND CHRONON3D_CUDA_INCLUDE_DIR)
    target_include_directories(chronon3d_media_video_tests PRIVATE
        "${CHRONON3D_CUDA_INCLUDE_DIR}")
endif()

# Backend text + Blend2D are needed when those features are enabled
# (transitive deps of chronon3d_backend_software that don't propagate
# from the OBJECT library link).
if(CHRONON3D_ENABLE_TEXT AND CHRONON3D_USE_BLEND2D AND TARGET chronon3d_backend_text)
    target_link_libraries(chronon3d_media_video_tests PRIVATE chronon3d_backend_text)
endif()
if(CHRONON3D_USE_BLEND2D AND TARGET blend2d::blend2d)
    target_link_libraries(chronon3d_media_video_tests PRIVATE blend2d::blend2d)
endif()

# Native decoder regression coverage is deliberately kept out of the generic
# video suite when FFmpeg support is disabled.  The test uses two independent
# Y4M sources concurrently, exercising the per-session lock rather than the
# decoder-wide session-map lock.
if(CHRONON3D_ENABLE_NATIVE_FFMPEG AND TARGET chronon3d_media_native)
    chronon3d_add_test_suite(
        NAME chronon3d_native_decoder_tests
        TIER INTEGRATION
        LINK_TARGETS chronon3d_media_native chronon3d_pipeline chronon3d_backend_software chronon3d_core_impl
        SOURCES video/test_native_video_frame_decoder.cpp
            video/test_video_device_runtime.cpp
    )

    if(CHRONON3D_ENABLE_CUDA_INTEROP AND TARGET chronon3d_media_native AND
       CHRONON3D_CUDA_INCLUDE_DIR AND CHRONON3D_NVRTC_INCLUDE_DIR AND
       CHRONON3D_NVRTC_LIBRARY)
        # GPU conversion contract: this executable is opt-in at runtime and
        # skips cleanly when no CUDA/Vulkan device is available.
        chronon3d_add_test_suite(
            NAME chronon3d_cuda_p010_conversion_tests
            TIER INTEGRATION
            LINK_TARGETS chronon3d_media_native chronon3d_pipeline chronon3d_backend_software chronon3d_core_impl
            SOURCES video/test_cuda_p010_conversion.cpp
        )
        target_include_directories(chronon3d_cuda_p010_conversion_tests PRIVATE
            "${CHRONON3D_CUDA_INCLUDE_DIR}" "${CHRONON3D_NVRTC_INCLUDE_DIR}")
        target_link_libraries(chronon3d_cuda_p010_conversion_tests PRIVATE
            "${CHRONON3D_CUDA_DRIVER_LIBRARY}" "${CHRONON3D_NVRTC_LIBRARY}")
    endif()
    if(CHRONON3D_ENABLE_CUDA_INTEROP AND CHRONON3D_CUDA_INCLUDE_DIR)
        target_include_directories(chronon3d_native_decoder_tests PRIVATE "${CHRONON3D_CUDA_INCLUDE_DIR}")
    endif()

    # Isolated decoder teardown stress harness (CASE A-D + bisection matrix).
    # Kept as a separate target so it can be run standalone under ASan:
    #   cmake --build ... --target chronon3d_native_decoder_teardown_tests
    #   ASAN_OPTIONS=halt_on_error=1:detect_leaks=1 .../chronon3d_native_decoder_teardown_tests
    chronon3d_add_test_suite(
        NAME chronon3d_native_decoder_teardown_tests
        TIER INTEGRATION
        LINK_TARGETS chronon3d_media_native chronon3d_pipeline chronon3d_backend_software chronon3d_core_impl
        SOURCES video/test_native_decoder_teardown_stress.cpp
                video/test_native_av_encoder_teardown_stress.cpp
    )
    if(CHRONON3D_ENABLE_CUDA_INTEROP AND CHRONON3D_CUDA_INCLUDE_DIR)
        target_include_directories(chronon3d_native_decoder_teardown_tests PRIVATE
            "${CHRONON3D_CUDA_INCLUDE_DIR}")
    endif()
endif()
