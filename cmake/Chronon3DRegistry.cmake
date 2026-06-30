# ==============================================================================
# cmake/Chronon3DRegistry.cmake — Central module registry
#
# PURPOSE
#   Single source of truth for every OBJECT and INTERFACE library target
#   in the Chronon3D build graph.  Four downstream concerns derive from
#   this one file:
#
#     1) chronon3d_sdk_impl archive aggregation
#        (target_link_libraries in src/CMakeLists.txt)
#     2) In-tree $<TARGET_OBJECTS:…> propagation
#        (target_sources on chronon3d_sdk in src/CMakeLists.txt)
#     3) Install / export target list
#        (_chronon3d_install_targets in root CMakeLists.txt)
#     4) Chronon3D::SDK INTERFACE link contract  (forward-compat §14.5/§14.6)
#        (CHRONON3D_SDK_LINK_DEPS_LIST — third-party link closure
#         that is wrapped into the consumer-facing INTERFACE target)
#
#   Concerns (1)–(3) were previously manually-maintained lists that
#   MUST stay in sync with every add_library(… OBJECT) call across
#   ~15 src/ subdirectories.  This registry replaces them with
#   foreach-if-TARGET loops — a single entry is all that's needed.
#   Concern (4) is forward-compat placeholder (§14.6 populates the
#   list, §14.5 wraps it into Chronon3D::SDK).  Kept here so the final
#   §14.5 step reads its SSoT from this file rather than duplicating
#   the link closure in src/ or install/ CMakeLists.
#
# MAINTENANCE CONTRACT (replaces the old contract in src/CMakeLists.txt)
#   When you add a new OBJECT or INTERFACE library anywhere in src/,
#   add its target name to the appropriate list below.
#
#   • Unconditional targets go in their subsystem bucket in
#     CHRONON3D_REGISTRY_OBJECT_LIBS.  Order within a bucket is
#     strict-ASCII-alphabetical and enforced by §14.2; cross-bucket
#     order is the canonical subsystem order
#     (core / animations / cache / effects / registry / assets /
#      scene / runtime / extension / backends / render_graph /
#      conditional).
#   • Conditional targets (gated by CHRONON3D_ENABLE_*, USE_*, BUILD_*
#     options) also go in the bucket list — the downstream foreach
#     loops use `if(TARGET …)` to skip targets that were never created
#     in the current configuration, so the condition is handled
#     naturally.
#   • Third-party link deps for the consumer-facing SDK go in
#     CHRONON3D_SDK_LINK_DEPS_LIST (forward-compat §14.5/§14.6).
#
#   After adding a name here, you are DONE for (1)–(3).  The sdk_impl
#   archive, the in-tree link closure, the install/export set, and
#   (eventually) the Chronon3D::SDK INTERFACE link contract all pick
#   it up automatically.  No other file needs updating.
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
#
# Ordering contract (§14.2): strict-ASCII-alphabetical within each
# subsystem bucket; cross-bucket order is the canonical 12-bucket
# ordering documented in the MAINTENANCE CONTRACT above.  Both axes
# are stable: §14.13-style diff checks compare sorted vs on-disk and
# must yield an empty diff.
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

    # ── Backends (src/backends/{assets,image,software}) ───────────────
    chronon3d_backend_assets
    chronon3d_backend_image
    chronon3d_backend_software

    # ── Render graph sub-targets (src/render_graph) ──────────────────
    chronon3d_graph_builder
    chronon3d_graph_cache
    chronon3d_graph_compiler
    chronon3d_graph_core
    chronon3d_graph_executor
    chronon3d_graph_nodes
    chronon3d_graph_pipeline
    chronon3d_graph_preflight

    # ── Conditional OBJECT targets ───────────────────────────────────
    #       Downstream foreach-if-TARGET loop handles the gating;
    #       per-target guard documented inline so the disambiguation
    #       of AND-gated / OR-gated / single-gated conditionals is
    #       visible at the registry call site.
    chronon3d_backend_software_diagnostics   # CHRONON3D_BUILD_DIAGNOSTICS
    chronon3d_backend_text                  # CHRONON3D_ENABLE_TEXT AND CHRONON3D_USE_BLEND2D
    chronon3d_backend_video                 # CHRONON3D_ENABLE_VIDEO
    chronon3d_blend2d_paint                 # CHRONON3D_USE_BLEND2D
    chronon3d_content                       # CHRONON3D_BUILD_CONTENT OR CHRONON3D_BUILD_DIAGNOSTICS
    chronon3d_diagnostics                   # CHRONON3D_BUILD_DIAGNOSTICS
    chronon3d_media_video                   # CHRONON3D_ENABLE_VIDEO
    chronon3d_text_core                     # CHRONON3D_ENABLE_TEXT
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

# ── SDK link-deps (forward-compat §14.5 / §14.6) ─────────────────────────
# Canonical third-party link contract that the consumer-facing
# Chronon3D::SDK INTERFACE target will wrap.  §14.6 populates this
# with the canonical dependency set (glm / fmt / spdlog / tbb /
# magic_enum / nlohmann_json / xxHash / hwy + conditionals); §14.5
# reads it to construct the final INTERFACE target so there is ONE
# in-tree/installed path and ZERO duplication of the link closure.
#
# Empty in §14.2 — pure placeholder, no consumer reads it yet.
# File-scope (not target-scope) so the list is visible to
# src/CMakeLists.txt and the install/export block equally.
set(CHRONON3D_SDK_LINK_DEPS_LIST "")
