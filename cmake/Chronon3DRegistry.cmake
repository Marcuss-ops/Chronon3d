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

# ── SDK public deps — single source of truth ────────────────────────────
# All Chronon3D::SDK link contracts (chronon3d_sdk INTERFACE's
# $<INSTALL_INTERFACE:…> entries) AND all Chronon3DConfig.cmake
# find_dependency() lines are DERIVED from this single list.  Adding
# or removing a public dep is a one-line change HERE; both downstream
# surfaces regenerate from this list automatically and
# tools/check_architecture_boundaries.sh check #16 fail-fasts on
# drift between list-length and the auto-generated find_dependency
# block.
#
# Convention: each entry is the CMake TARGET alias (e.g. `fmt::fmt`).
# For find_dependency() lines, the package name is derived as the
# lower-cased portion BEFORE `::` (matches vcpkg's lowercase
# package-name convention for Config-mode packages).
#
# hwy (PRIVATE to chronon3d_graph_core / chronon3d_backend_software)
# is deliberately kept OUT of this list — its find_dependency() lives
# HAND-WRITTEN below the auto-marker in Chronon3DConfig.cmake.in
# because consumers need to resolve it via $<LINK_ONLY:> propagation.
#
# Conditional deps (meshoptimizer, blend2d, ZLIB, freetype, harfbuzz,
# FRIBIDI, OpenEXR) stay hand-written in Chronon3DConfig.cmake.in
# (gated by CHRONON3D_* / @CHRONON3D_*@ flags, not in this SSoT).
# ── SDK public deps — explicit Target::alias;package_name pairs ───────── │
# Convention: each entry is a ';'-separated pair where index 0 is the
# CMake TARGET alias (used by the SDK link contract in Chronon3DSdkTargets.cmake
# via `$<INSTALL_INTERFACE:${_target_alias}>`) and index 1 is the vcpkg
# package name (used for find_dependency at consumer config-time).
# Storing both explicitly avoids silent string(REPLACE) / TOLOWER
# normalization of package names (TBB stays 'tbb', xxHash stays 'xxhash').
set(CHRONON3D_SDK_PUBLIC_DEPS
    "glm::glm|glm"
    "fmt::fmt|fmt"
    "spdlog::spdlog_header_only|spdlog"
    "TBB::tbb|TBB"
    "magic_enum::magic_enum|magic_enum"
    "nlohmann_json::nlohmann_json|nlohmann_json"
    "xxHash::xxhash|xxHash"
)

# Auto-derive the find_dependency line block (consumed by
# configure_file(... @CHRONON3D_FIND_DEPENDENCY_LINES@) in
# cmake/Chronon3DConfig.cmake.in's auto-marker block; validated by
# tools/check_architecture_boundaries.sh check #16).
set(_chronon3d_find_dep_lines "")
foreach(_entry IN LISTS CHRONON3D_SDK_PUBLIC_DEPS)
    # entry is "Target::alias|package_name"; CMake does NOT treat '|' as a
    # list delimiter, so each entry survives as ONE string.  Inline split
    # via string(REPLACE) then list(GET …) to recover the 2 fields.
    # INVARIANT: neither field of any pair may itself contain '|'.
    string(REPLACE "|" ";" _pair "${_entry}")
    list(GET _pair 1 _pkg_name)
    string(APPEND _chronon3d_find_dep_lines "find_dependency(${_pkg_name} CONFIG)\n")
endforeach()
# Idempotent rebuild: the foreach loop regenerates the string fresh on
# every configure invocation, so cache replacement is unconditional and
# a stale string cannot survive a registry-list edit.
set(CHRONON3D_FIND_DEPENDENCY_LINES "${_chronon3d_find_dep_lines}"
    CACHE INTERNAL "Auto-generated find_dependency lines (idempotent — rebuilt every configure)")
