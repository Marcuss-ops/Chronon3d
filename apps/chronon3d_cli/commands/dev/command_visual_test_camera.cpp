#include "../../commands.hpp"

#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/image/image_writer.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/timeline/composition.hpp>

#include <iostream>
#include <filesystem>

namespace chronon3d::cli {

int command_visual_test_camera(const VisualTestCameraArgs& args) {
    const std::filesystem::path out_dir = args.out_dir;
    std::filesystem::create_directories(out_dir);

    // Print instructions: the actual visual tests are run via the test binary.
    // This CLI command is a thin wrapper that documents how to run/update golden images.
    std::cout << "Chronon3D Camera Visual Tests\n";
    std::cout << "=============================\n\n";

    if (args.update_golden) {
        std::cout << "Mode: UPDATE GOLDEN\n";
        std::cout << "To regenerate golden images, run the test binary with the environment set:\n";
        std::cout << "  CHRONON3D_UPDATE_GOLDEN=1 ./build/chronon3d_camera_visual_tests\n";
        std::cout << "\nOr use ctest:\n";
        std::cout << "  CHRONON3D_UPDATE_GOLDEN=1 ctest -R chronon3d_camera_visual_tests --output-on-failure\n";
    } else {
        std::cout << "Mode: VERIFY\n";
        std::cout << "Golden dir: tests/golden/camera/\n";
        std::cout << "Artifact dir: " << out_dir.string() << "/\n";
        std::cout << "\nRun the test binary:\n";
        std::cout << "  ./build/chronon3d_camera_visual_tests\n";
        std::cout << "\nOr use ctest:\n";
        std::cout << "  ctest -R chronon3d_camera_visual_tests --output-on-failure\n";
    }

    if (!args.case_name.empty()) {
        std::cout << "\nRequested case: " << args.case_name << "\n";
        std::cout << "(Per-case filtering is supported by the test binary via -tc=\"*case_name*\")\n";
    }

    return 0;
}

} // namespace chronon3d::cli
