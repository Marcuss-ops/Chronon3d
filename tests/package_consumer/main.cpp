// tests/package_consumer/main.cpp
//
// External consumer that proves the Chronon3D package install works:
//   cmake --install <build_dir> --prefix <install-test>
//   cmake -S tests/package_consumer -B consumer-build \
//         -DCMAKE_PREFIX_PATH="$PWD/<install-test>"
//   cmake --build consumer-build && ./consumer-build/package_consumer_test
//
// If this file compiles + links + runs, the package install gate is open.

#include <chronon3d/core/types/types.hpp>
#include <chronon3d/math/glm_types.hpp>

#include <cstdio>

int main() {
    chronon3d::Vec3 v{1.0f, 2.0f, 3.0f};
    std::printf("package_consumer_test ok: Vec3={%f,%f,%f}\n", v.x, v.y, v.z);
    return 0;
}
