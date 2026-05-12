#include <chronon3d/chronon3d.hpp>
#include <spdlog/spdlog.h>
#include <iostream>

int main(int argc, char** argv) {
    spdlog::info("Chronon3d CLI version 0.1.0");
    
    if (argc > 1) {
        std::string cmd = argv[1];
        if (cmd == "info") {
            std::cout << "Chronon3d: A modular C++20 3D engine/library" << std::endl;
        }
    } else {
        std::cout << "Usage: chronon3d_cli [command]" << std::endl;
        std::cout << "Commands: info" << std::endl;
    }
    
    return 0;
}
