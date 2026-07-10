# ═══════════════════════════════════════════════════════════════════════════
# Chronon3D — Getting Started Example
#
# ~30 lines of C++ that demonstrate the full Project + Composition + Text
# + Render + PNG flow.  This is the Remotion-like simplicity vision.
#
# Build:
#   mkdir build && cd build
#   cmake .. -DCMAKE_PREFIX_PATH=/path/to/chronon3d-install
#   cmake --build .
# ═══════════════════════════════════════════════════════════════════════════

cmake_minimum_required(VERSION 3.27)
project(getting_started VERSION 1.0.0 LANGUAGES CXX)

find_package(Chronon3D CONFIG REQUIRED)

add_executable(getting_started main.cpp)
set_target_properties(getting_started PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
target_link_libraries(getting_started PRIVATE Chronon3D::SDK)
