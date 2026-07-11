cmake_minimum_required(VERSION 3.27)

project(install_consumer
    VERSION 0.0.1
    LANGUAGES CXX
)

# --------------------------------------------------------------------------
# End-to-end install boundary check (P3-H + TICKET-011 strict-A → rewrite)
#
# This is a STANDALONE CMake project. It uses ONLY what an installed
# Chronon3D package exposes. It is intentionally NOT included in the in-tree
# tests/CMakeLists.txt: doing so would re-link against the in-tree targets
# and defeat the whole point of the test.
#
# The fact that find_package() resolves against a previously-installed
# <prefix> proves the install boundary produced a valid Chronon3DConfig.cmake
# + Chronon3DTargets.cmake package.
#
# P3-H — manifest-clean consumer:
#   This consumer transits ONLY the V0.1 public-header manifest
#   (cmake/Chronon3DPublicHeaders.cmake). The DIRECT #include set in
#   main.cpp is the explicit public SDK headers (sdk/*, timeline/composition,
#   scene builders, text types, image writer and their transitive closure).
#   No umbrella headers are used.
#
#   The consumer's runtime calls are strictly:
#     * chronon3d::sdk::RenderEngine::render(const Composition&, Frame)
#       → Result<RenderOutput, RenderError>
#     * chronon3d::save_png(Framebuffer, path)
#
#   Plus the manifest-reachable Composition/Scene/Layer/Shape helpers:
#     * Composition::default_camera_descriptor(CameraDescriptor)
#     * Scene::add_layer(Layer); Layer::nodes; RenderNode::shape
#     * Pixel-hash check in the consumer itself (falls BOUNDARY-FAIL if
#       every pixel mean luminance < 5/255 — i.e. PNG would be black).
#
#   Per the rewrite contract (RELEASE_GATE.md §SDK Product V1 / 8 — P3-H.b
#   + SDK Product V1 / 2 — "render almeno una composizione reale fuori-tree
#   (testo + camera → PNG)"), the consumer:
#     (i)  uses sdk::RenderEngine (NOT the legacy chronon3d::RenderEngine);
#     (ii) composes a non-empty Scene with a GridBackground Shape layer
#          AND a Text Shape layer;
#     (iii) sets a CameraDescriptor on the Composition so the OPP compiles
#          it via the V2 camera path at render time;
#     (iv) saves the rendered PNG via save_png;
#     (v)  fails BOUNDARY-FAIL if the produced PNG would be black.
# --------------------------------------------------------------------------

find_package(Chronon3D CONFIG REQUIRED)

# Pre-condition: the documented public API target must be intact.
# If the install script ever strips this target, this fails fast with a clear
# error instead of producing a vague link error further downstream. The
# PUBLIC SURFACE contract is defined in cmake/Chronon3DPublicHeaders.cmake
# (single source of truth for the V0.1 SDK manifest) and propagated through
# Chronon3DSdkTargets.cmake.
if(NOT TARGET Chronon3D::SDK)
    message(FATAL_ERROR
        "Install boundary broken: Chronon3D::SDK target absent. "
        "The V0.1 public-header manifest (cmake/Chronon3DPublicHeaders.cmake) "
        "and the PUBLIC SURFACE contract defined in cmake/Chronon3DSdkTargets.cmake "
        "are not satisfied by the current install.")
endif()

add_executable(check_install main.cpp)

# ── FASE 2.2 — Consumer con testo reale (certificazione prodotto) ──
add_executable(check_text main_text.cpp)
set_target_properties(check_text PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)
target_link_libraries(check_text PRIVATE Chronon3D::SDK)

# ── P3-H + feat(api) public camera facade — external consumer test ──
#
# The `check_camera` consumer validates the public SceneCameraFacade
# (scene.camera().descriptor/program/timeline/preset chain), the public
# `chronon3d::camera()` fluent builder, the public `composition.camera(p)`
# setter, AND the boundary contract: the consumer source MUST NOT see
# `CameraSession` or `RenderGraph` (those types live in `internal/`, which
# is not on the installed SDK's include path).  The output marker
# `[CAMERA-OK]` is consumed by `tools/sdk/run_external_consumer.sh`.
add_executable(check_camera main_camera.cpp)
set_target_properties(check_camera PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)
target_link_libraries(check_camera PRIVATE Chronon3D::SDK)

# C++20 — must match the SDK's compile feature set so the consumer's
# headers behave the same way as in-tree.
set_target_properties(check_install PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

# Single PUBLIC-API link call.  chronon3d_sdk_impl propagates
# transitively through Chronon3D::SDK at link time.
target_link_libraries(check_install PRIVATE Chronon3D::SDK)
