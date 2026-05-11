cmake_minimum_required(VERSION 3.22)
project(bgfx-premiere-camera)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED libavformat libavcodec libavutil libswscale)

find_package(glfw3 REQUIRED)
find_package(glm REQUIRED)

# bgfx - assume installed via vcpkg or system
find_path(BGFX_INCLUDE_DIR bgfx/bgfx.h)
find_library(BGFX_LIB bgfx)
find_library(BIMG_LIB bimg)
find_library(BX_LIB bx)

# imgui
find_path(IMGUI_INCLUDE_DIR imgui.h PATH_SUFFIXES imgui)
file(GLOB IMGUI_SOURCES ${IMGUI_INCLUDE_DIR}/*.cpp)

add_executable(app
    src/main.cpp
    ${IMGUI_SOURCES}
    ${IMGUI_INCLUDE_DIR}/backends/imgui_impl_glfw.cpp
)

target_include_directories(app PRIVATE
    ${BGFX_INCLUDE_DIR}
    ${IMGUI_INCLUDE_DIR}
    ${IMGUI_INCLUDE_DIR}/backends
    ${FFMPEG_INCLUDE_DIRS}
)

target_link_libraries(app PRIVATE
    ${BGFX_LIB} ${BIMG_LIB} ${BX_LIB}
    glfw
    ${FFMPEG_LIBRARIES}
    dl pthread
)

target_compile_definitions(app PRIVATE BGFX_CONFIG_RENDERER_OPENGL=1)
