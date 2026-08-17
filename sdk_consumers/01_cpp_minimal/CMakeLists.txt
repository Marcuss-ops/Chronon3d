cmake_minimum_required(VERSION 3.27)

project(chronon_sdk_cpp_minimal LANGUAGES CXX)

# Consume ONLY the installed package (headers + exported targets).  The
# documented public link surface is exactly one target: Chronon3D::SDK.
find_package(Chronon3D CONFIG REQUIRED)

if(NOT TARGET Chronon3D::SDK)
    message(FATAL_ERROR "install boundary broken: Chronon3D::SDK absent")
endif()

add_executable(cpp_minimal main.cpp)
target_compile_features(cpp_minimal PRIVATE cxx_std_20)
target_link_libraries(cpp_minimal PRIVATE Chronon3D::SDK)
