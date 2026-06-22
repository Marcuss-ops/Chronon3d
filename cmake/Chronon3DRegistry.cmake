# ==============================================================================
# cmake/Chronon3DRegistry.cmake — Central module registry
#
# PURPOSE
#   Single source of truth for every OBJECT and INTERFACE library target
#   in the Chronon3D build graph.  Three downstream concerns derive from
#   this one file:
#
#     1) chronon3d_sdk_impl archive aggregation
#        (target_link_libraries in src/CMakeLists.txt)
#     2) In-tree $<TARGET_OBJECTS:…> propagation
#        (target_sources on chronon3d_sdk in src/CMakeLists.txt)
#     3) Install / export target list
#        (_chronon3d_install_targets in root CMakeLists.txt)
#
#   Each of the three blocks above was previously a manually-maintained
#   list that MUST stay in sync with every add_library(… OBJECT) call
#   across ~15 src/ subdirectories.  This registry replaces all three
#   with foreach-if-TARGET loops — a single entry is all that's needed.
#
# MAINTENANCE CONTRACT (replaces the old contract in src/CMakeLists.txt)
#   When you add a new OBJECT or INTERFACE library anywhere in src/,
#   add its target name to the appropriate list below.
#
#   • Unconditional targets go in the main list.
#   • Conditional targets (gated by CHRONON3D_ENABLE_*, USE_*, BUILD_*
#     options) also go in the main list — the downstream foreach loops
#     use `if(TARGET …)` to skip targets that were never created in
#     the current configuration, so the condition is handled naturally.
#
#   After adding a name here, you are DONE.  The sdk_impl archive, the
#   in-tree link closure, and the install/export set all pick it up
#   automatically.  No other file needs updating.
#
# INCLUSION ORDER
#   This file is included near the top of the root CMakeLists.txt,
#   BEFORE add_subdirectory(src), so the variable lists are available
#   when src/CMakeLists.txt runs (the foreach loops consume the lists
#   AFTER all add_subdirectory calls have created the targets).
#   The install/export section at the bottom of the root CMakeLists.txt
#   also consumes the same lists.
# ==============================================================================

# ── OBJECT libraries ──────────────────────────────────────────────────
# Every add_library(xxx OBJECT …) in src/ must appear here exactly once.
# Conditional targets are included unconditionally in this list — the
# downstream foreach-if-TARGET loop handles the gating.
set(CHRONON3D_REGISTRY_OBJECT_LIBS
    # ── Core (src/core) ──────────────────────────────────────────────
    chronon3d_core_impl

    # ── Animations (src/animations) ──────────────────────────────────
    chronon3d_animations

    # ── Cache (src/cache) ────────────────────────────────────────────
    chronon3d_cache

    # ── Effects (src/effects) ────────────────────────────────────────
    chronon3d_effects

    # ── Registry (src/registry) ──────────────────────────────────────
    chronon3d_registry

    # ── Assets (src/assets) ──────────────────────────────────────────
    chronon3d_assets

    # ── Scene (src/scene) ────────────────────────────────────────────
    chronon3d_scene

    # ── Runtime (src/runtime) ────────────────────────────────────────
    chronon3d_runtime

    # ── Extension (src/extension) ────────────────────────────────────
    chronon3d_extension

    # ── Backend: Image (src/backends/image) ──────────────────────────
    chronon3d_backend_image

    # ── Backend: Software (src/backends/software) ────────────────────
    chronon3d_backend_software

    # ── Backend: Assets (src/backends/assets) ────────────────────────
    chronon3d_backend_assets

    # ── Render graph sub-targets (src/render_graph) ──────────────────
    chronon3d_graph_core
    chronon3d_graph_nodes
    chronon3d_graph_builder
    chronon3d_graph_compiler
    chronon3d_graph_executor
    chronon3d_graph_preflight
    chronon3d_graph_cache
    chronon3d_graph_pipeline

    # ── Conditional: Text core (src/text) ────────────────────────────
    #       Guard: CHRONON3D_ENABLE_TEXT
    chronon3d_text_core

    # ── Conditional: Video backend (src/media) ───────────────────────
    #       Guard: CHRONON3D_ENABLE_VIDEO
    chronon3d_backend_video
    chronon3d_media_video

    # ── Conditional: Text backend (src/backends/text) ────────────────
    #       Guard: CHRONON3D_ENABLE_TEXT AND CHRONON3D_USE_BLEND2D
    chronon3d_backend_text

    # ── Conditional: Blend2D paint (src/backends/software/blend2d) ───
    #       Guard: CHRONON3D_USE_BLEND2D
    chronon3d_blend2d_paint

    # ── Conditional: Software diagnostics ─────────────────────────────
    #       Guard: CHRONON3D_BUILD_DIAGNOSTICS
    chronon3d_backend_software_diagnostics

    # ── Conditional: Content (content/) ───────────────────────────────
    #       Guard: CHRONON3D_BUILD_CONTENT OR CHRONON3D_BUILD_DIAGNOSTICS
    chronon3d_content

    # ── Conditional: Diagnostics (content/) ───────────────────────────
    #       Guard: CHRONON3D_BUILD_DIAGNOSTICS
    chronon3d_diagnostics
)

# ── INTERFACE / STATIC / aggregate libraries (not OBJECT) ────────────────
# These targets are NOT OBJECT libraries (they have no .o from themselves
# — chronon3d_sdk_impl is STATIC, the others are INTERFACE).  They still
# need to appear in the install/export set so `install(EXPORT …)` does
# not flag them as missing.  Keeping them in a separate list avoids
# polluting the $<TARGET_OBJECTS:…> and target_link_libraries(sdk_impl …)
# blocks, which only deal with actual OBJECT libraries.
set(CHRONON3D_REGISTRY_INTERFACE_LIBS
    # ── Consumer-facing SDK alias ────────────────────────────────────
    chronon3d_sdk
    chronon3d_sdk_impl

    # ── Aggregate INTERFACE groupers ─────────────────────────────────
    chronon3d_pipeline
    chronon3d_core
    chronon3d_software
    chronon3d_graph
    chronon3d_media_interface
    chronon3d

    # ── Conditional: FFmpeg shim targets ─────────────────────────────
    #       Guard: CHRONON3D_ENABLE_NATIVE_FFMPEG
    chronon3d_ffmpeg_light
    chronon3d_ffmpeg_full
)
