# CMake generated Testfile for 
# Source directory: /home/pierone/Pyt/Chronon3d
# Build directory: /home/pierone/Pyt/Chronon3d/.tmp_gate10/chronon3d_install_consumer_sdk_build.CekIwc
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[install_consumer_ci]=] "/home/pierone/Pyt/Chronon3d/tools/install_consumer_test.sh")
set_tests_properties([=[install_consumer_ci]=] PROPERTIES  ENVIRONMENT "CMAKE_TOOLCHAIN_FILE=/home/pierone/Pyt/Chronon3d/vcpkg_bootstrap/scripts/buildsystems/vcpkg.cmake;CMAKE_PREFIX_PATH=/home/pierone/Pyt/Chronon3d/vcpkg_installed/linux-ci/x64-linux/debug;/home/pierone/Pyt/Chronon3d/vcpkg_installed/linux-ci/x64-linux" LABELS "boundary;ci" TIMEOUT "900" _BACKTRACE_TRIPLES "/home/pierone/Pyt/Chronon3d/CMakeLists.txt;235;add_test;/home/pierone/Pyt/Chronon3d/CMakeLists.txt;0;")
subdirs("src")
subdirs("content")
subdirs("tests")
subdirs("apps/chronon3d_cli")
