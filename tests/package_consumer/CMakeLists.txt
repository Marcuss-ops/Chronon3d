cmake_minimum_required(VERSION 3.27)

project(package_consumer_test
    VERSION 0.0.1
    LANGUAGES CXX
)

# External consumer — uses ONLY what an installed Chronon3D package exposes.
# This file is the Definition-of-Done gate for the package-install refactor.
find_package(Chronon3D CONFIG REQUIRED)

add_executable(package_consumer_test main.cpp)

# The whole point: a single CMake call that resolves everything.
target_link_libraries(package_consumer_test PRIVATE Chronon3D::SDK)

set_target_properties(package_consumer_test PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)
