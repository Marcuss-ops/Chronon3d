# Install script for directory: /home/pierone/Pyt/Chronon3d/src

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "/home/pierone/Pyt/Chronon3d/.tmp_gate10/chronon3d_install_consumer_prefix.n6RFw8")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "Debug")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Install shared libraries without execute permission?
if(NOT DEFINED CMAKE_INSTALL_SO_NO_EXE)
  set(CMAKE_INSTALL_SO_NO_EXE "1")
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "FALSE")
endif()

# Set path to fallback-tool for dependency-resolution.
if(NOT DEFINED CMAKE_OBJDUMP)
  set(CMAKE_OBJDUMP "/usr/bin/objdump")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/pierone/Pyt/Chronon3d/.tmp_gate10/chronon3d_install_consumer_sdk_build.CekIwc/src/core/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/pierone/Pyt/Chronon3d/.tmp_gate10/chronon3d_install_consumer_sdk_build.CekIwc/src/cache/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/pierone/Pyt/Chronon3d/.tmp_gate10/chronon3d_install_consumer_sdk_build.CekIwc/src/effects/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/pierone/Pyt/Chronon3d/.tmp_gate10/chronon3d_install_consumer_sdk_build.CekIwc/src/registry/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/pierone/Pyt/Chronon3d/.tmp_gate10/chronon3d_install_consumer_sdk_build.CekIwc/src/assets/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/pierone/Pyt/Chronon3d/.tmp_gate10/chronon3d_install_consumer_sdk_build.CekIwc/src/backends/image/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/pierone/Pyt/Chronon3d/.tmp_gate10/chronon3d_install_consumer_sdk_build.CekIwc/src/media/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/pierone/Pyt/Chronon3d/.tmp_gate10/chronon3d_install_consumer_sdk_build.CekIwc/src/backends/text/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/pierone/Pyt/Chronon3d/.tmp_gate10/chronon3d_install_consumer_sdk_build.CekIwc/src/backends/assets/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/pierone/Pyt/Chronon3d/.tmp_gate10/chronon3d_install_consumer_sdk_build.CekIwc/src/render_graph/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/pierone/Pyt/Chronon3d/.tmp_gate10/chronon3d_install_consumer_sdk_build.CekIwc/src/runtime/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/pierone/Pyt/Chronon3d/.tmp_gate10/chronon3d_install_consumer_sdk_build.CekIwc/src/extension/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/pierone/Pyt/Chronon3d/.tmp_gate10/chronon3d_install_consumer_sdk_build.CekIwc/src/backends/software/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/pierone/Pyt/Chronon3d/.tmp_gate10/chronon3d_install_consumer_sdk_build.CekIwc/src/scene/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/pierone/Pyt/Chronon3d/.tmp_gate10/chronon3d_install_consumer_sdk_build.CekIwc/src/timeline/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/pierone/Pyt/Chronon3d/.tmp_gate10/chronon3d_install_consumer_sdk_build.CekIwc/src/animations/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/home/pierone/Pyt/Chronon3d/.tmp_gate10/chronon3d_install_consumer_sdk_build.CekIwc/src/text/cmake_install.cmake")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  execute_process(COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target sdk_archive_merge)
endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
if(CMAKE_INSTALL_LOCAL_ONLY)
  file(WRITE "/home/pierone/Pyt/Chronon3d/.tmp_gate10/chronon3d_install_consumer_sdk_build.CekIwc/src/install_local_manifest.txt"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
endif()
