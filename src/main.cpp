#define SDL_MAIN_HANDLED
#include "core/FilamentContext.hpp"
#include "core/ResourceWrappers.hpp"

#include <SDL.h>
#include <SDL_syswm.h>

#include <spdlog/spdlog.h>
#include <iostream>
#include <memory>

#include <windows.h>

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
    spdlog::info("Initializing Graphis3d...");

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        spdlog::error("SDL could not initialize! SDL_Error: {}", SDL_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow(
        "Graphis3d - Filament Engine",
        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        1280, 720,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );

    if (!window) {
        spdlog::error("Window could not be created! SDL_Error: {}", SDL_GetError());
        return 1;
    }

    try {
        void* nativeWindow = getNativeWindow(window);
        graphis3d::FilamentContext context(nativeWindow);

        // Setup dual view
        auto engine = context.getEngine();
        auto scene = context.getScene();
        
        // Second View and Camera
        auto view2 = engine->createView();
        view2->setScene(scene);
        
        utils::Entity camera2Entity = utils::EntityManager::get().create();
        auto camera2 = engine->createCamera(camera2Entity);
        view2->setCamera(camera2);

        // Split screen: View 1 on left, View 2 on right
        context.getView()->setViewport({0, 0, 640, 720});
        view2->setViewport({640, 0, 640, 720});

        spdlog::info("Dual views initialized.");

        bool quit = false;
        SDL_Event e;

        while (!quit) {
            while (SDL_PollEvent(&e) != 0) {
                if (e.type == SDL_QUIT) {
                    quit = true;
                }
            }

            // Render loop
            auto renderer = context.getRenderer();
            auto swapChain = context.getSwapChain();

            if (renderer->beginFrame(swapChain)) {
                renderer->render(context.getView());
                renderer->render(view2);
                renderer->endFrame();
            }

            SDL_Delay(16); 
        }

        // Cleanup second view/camera (FilamentContext handles the rest)
        engine->destroy(view2);
        engine->destroy(camera2Entity);

    } catch (const std::exception& ex) {
        spdlog::error("Critical error: {}", ex.what());
    }

    SDL_DestroyWindow(window);
    SDL_Quit();

    spdlog::info("Graphis3d shutdown complete.");
    return 0;
}
