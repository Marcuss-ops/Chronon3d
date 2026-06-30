
####### Expanded from @PACKAGE_INIT@ by configure_package_config_file() #######
####### Any changes to this file will be overwritten by the next CMake run ####
####### The input file was Chronon3DConfig.cmake.in                            ########

get_filename_component(PACKAGE_PREFIX_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../" ABSOLUTE)

macro(set_and_check _var _file)
  set(${_var} "${_file}")
  if(NOT EXISTS "${_file}")
    message(FATAL_ERROR "File or directory ${_file} referenced by variable ${_var} does not exist !")
  endif()
endmacro()

macro(check_required_components _NAME)
  foreach(comp ${${_NAME}_FIND_COMPONENTS})
    if(NOT ${_NAME}_${comp}_FOUND)
      if(${_NAME}_FIND_REQUIRED_${comp})
        set(${_NAME}_FOUND FALSE)
      endif()
    endif()
  endforeach()
endmacro()

####################################################################################

include(CMakeFindDependencyMacro)

# Required third-party dependencies that propagate through the SDK link graph.
# find_dependency() resolves them at config-time so consumers don't have to
# call find_package() for each one themselves.
find_dependency(spdlog CONFIG)
find_dependency(fmt CONFIG)
find_dependency(TBB CONFIG)
find_dependency(glm CONFIG)
find_dependency(xxHash CONFIG)
find_dependency(nlohmann_json CONFIG)
find_dependency(magic_enum CONFIG)

# hwy (highway) is linked PRIVATE by chronon3d_graph_core and
# chronon3d_backend_software, but CMake propagates it into the
# installed INTERFACE_LINK_LIBRARIES of the static archive's
# transitive closure via $<LINK_ONLY:hwy::hwy>.  find_dependency
# ensures consumers can resolve it.
find_dependency(hwy CONFIG)

# Stb and Taskflow are intentionally omitted: they are either
# header-only (Stb) or referenced only via PRIVATE link interfaces
# that do not leak into the installed export (Taskflow).
# Taskflow's situation: OBJECT libraries use it only in .cpp files;
# it never appears in INTERFACE_LINK_LIBRARIES. Stb is header-only
# and never emitted in link interfaces.

# Optional dependencies are gated by the same CHRONON3D_* flags that drove
# the build, substituted into this template at configure time.
if(ON)
    find_dependency(meshoptimizer CONFIG)
endif()
if(ON)
    find_dependency(blend2d CONFIG)
endif()
if(ON)
    # ZLIB is a transitive dependency of freetype (referenced via
    # $<LINK_ONLY:ZLIB::ZLIB> in freetype-targets.cmake).  Resolve it
    # FIRST so the ZLIB::ZLIB target exists before freetype's config
    # tries to link against it.  Without this, find_dependency(freetype)
    # fails with "target ZLIB::ZLIB was not found".
    find_dependency(ZLIB)  # Find-module (no CONFIG) — vcpkg provides FindZLIB.cmake
    find_dependency(freetype CONFIG)
    find_dependency(harfbuzz CONFIG)
endif()
if(ON AND ON)
    # FriBidi is found via pkg-config; resolve it for the consumer so
    # PkgConfig::FRIBIDI (linked by chronon3d_backend_text PUBLIC)
    # does not break find_package(Chronon3D).
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(FRIBIDI IMPORTED_TARGET fribidi)
    endif()
endif()
if(ON)
    find_dependency(OpenEXR CONFIG)
endif()

include("${CMAKE_CURRENT_LIST_DIR}/Chronon3DTargets.cmake")

# ── Public API alias ─────────────────────────────────────────────────
# CMake does not preserve ALIAS targets across install(EXPORT …), so the
# in-tree `add_library(Chronon3D::SDK ALIAS chronon3d_sdk)` in
# src/CMakeLists.txt is lost at install time.  Recreate it here so
# downstream consumers can write the documented form:
#   target_link_libraries(my_app PRIVATE Chronon3D::SDK)
# The underlying IMPORTED target `chronon3d_sdk` is already defined by
# Chronon3DTargets.cmake included above.
if(NOT TARGET Chronon3D::SDK)
    add_library(Chronon3D::SDK ALIAS SDK)
endif()

check_required_components(Chronon3D)
