#define SDL_MAIN_HANDLED
#include "core/FilamentContext.hpp"
#include "core/ResourceWrappers.hpp"
#include "api/GraphisScene.hpp"

#include <SDL.h>
#include <SDL_syswm.h>
#include <spdlog/spdlog.h>
#include <cmath>

namespace {
    void* getNativeWindow(SDL_Window* window) {
        SDL_SysWMinfo wmInfo;
        SDL_VERSION(&wmInfo.version);
        if (SDL_GetWindowWMInfo(window, &wmInfo)) {
            return wmInfo.info.win.window;
        }
        return nullptr;
    }
}

int main(int argc, char* argv[]) {
    SDL_SetMainReady();
    spdlog::set_level(spdlog::level::debug);
    spdlog::info("Starting Chronon3d Demo...");

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        spdlog::error("SDL Init failed: {}", SDL_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow("Chronon3d - Easy Animation", 
        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 1280, 720, SDL_WINDOW_SHOWN);

    if (!window) return 1;

    try {
        graphis3d::FilamentContext context(getNativeWindow(window));
        graphis3d::GraphisScene scene(context);

        // Setup the "TILT" scene
        scene.setupLighting();
        
        // Background Plane (Orange)
        auto background = scene.addCube({0, 0, -2}, {10, 6, 0.1});

        // "TILT" Letters (Simulated with cubes)
        auto cube1 = scene.addCube({-1.5f, 0, 0}, {0.8f, 2.0f, 0.8f});
        auto cube2 = scene.addCube({ 0.0f, 0, 0}, {0.8f, 2.0f, 0.8f});
        auto cube3 = scene.addCube({ 1.5f, 0, 0}, {0.8f, 2.0f, 0.8f});

        // View configuration for Bloom and Post-processing
        auto view = context.getView();
        view->setPostProcessingEnabled(true);
        // Bloom settings in Filament require a BloomOptions struct
        // Note: For simplicity, we are using default bloom if enabled is set.
        
        context.getCamera()->setProjection(45.0, 1280.0/720.0, 0.1, 100.0);
        context.getCamera()->lookAt({0, 2, 8}, {0, 0, 0});

        bool quit = false;
        SDL_Event e;
        float time = 0.0f;

        while (!quit) {
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_QUIT) quit = true;
            }

            // Easy Animation: Wave effect on cubes
            time += 0.016f;
            auto& tcm = context.getEngine()->getTransformManager();
            
            float y1 = std::sin(time * 2.0f) * 0.5f;
            tcm.setTransform(tcm.getInstance(cube1), 
                filament::math::mat4f::translation(filament::math::float3{-1.5f, y1, 0.0f}) * 
                filament::math::mat4f::scaling(filament::math::float3{0.8f, 2.0f, 0.8f}));

            if (context.getRenderer()->beginFrame(context.getSwapChain())) {
                context.getRenderer()->render(view);
                context.getRenderer()->endFrame();
            }
            SDL_Delay(16);
        }
    } catch (const std::exception& ex) {
        spdlog::error("Error: {}", ex.what());
    }

    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
