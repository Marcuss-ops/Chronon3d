#define SDL_MAIN_HANDLED

#include "core/FilamentContext.hpp"
#include "core/ResourceWrappers.hpp"
#include "api/ChrononTestSuite.hpp"
#include "api/GraphisScene.hpp"

#include <SDL.h>
#include <SDL_syswm.h>

#include <cmath>
#include <cstring>
#include <iostream>
#include <spdlog/spdlog.h>

#if defined(__linux__)
#include <X11/Xlib.h>
#endif

namespace {
void* getNativeWindow(SDL_Window* window) {
    SDL_SysWMinfo wmInfo;
    SDL_VERSION(&wmInfo.version);
    if (SDL_GetWindowWMInfo(window, &wmInfo)) {
#if defined(_WIN32)
        return wmInfo.info.win.window;
#elif defined(__linux__)
        if (wmInfo.subsystem == SDL_SYSWM_X11) {
            return reinterpret_cast<void*>(static_cast<uintptr_t>(wmInfo.info.x11.window));
        }
#endif
    }
    return nullptr;
}
} // namespace

int main(int argc, char* argv[]) {
    SDL_SetMainReady();
    spdlog::set_level(spdlog::level::debug);
    spdlog::info("Starting Chronon3d Demo...");

    bool runChrononTests = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--chronon-tests") == 0) {
            runChrononTests = true;
            break;
        }
    }

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cerr << "SDL Init failed: " << SDL_GetError() << '\n';
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow(
            "Chronon3d - Easy Animation",
            SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
            1280, 720,
            SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);

    if (!window) {
        std::cerr << "Window could not be created! SDL_Error: " << SDL_GetError() << '\n';
        SDL_Quit();
        return 1;
    }

    try {
        graphis3d::FilamentContext context(getNativeWindow(window));
        int width = 0;
        int height = 0;
        SDL_GetWindowSize(window, &width, &height);

        if (runChrononTests) {
            context.getView()->setViewport({0, 0, static_cast<uint32_t>(width), static_cast<uint32_t>(height)});

            graphis3d::ChrononTestSuite suite(context);
            const float aspectRatio = height > 0 ? static_cast<float>(width) / static_cast<float>(height) : 16.0f / 9.0f;
            suite.initialize(aspectRatio);

            bool quit = false;
            SDL_Event e;
            Uint32 lastTick = SDL_GetTicks();

            while (!quit) {
                while (SDL_PollEvent(&e) != 0) {
                    if (e.type == SDL_QUIT) {
                        quit = true;
                    }
                }

                const Uint32 now = SDL_GetTicks();
                const float deltaSeconds = static_cast<float>(now - lastTick) / 1000.0f;
                lastTick = now;
                suite.update(deltaSeconds);

                auto renderer = context.getRenderer();
                auto swapChain = context.getSwapChain();
                if (renderer->beginFrame(swapChain)) {
                    renderer->render(context.getView());
                    renderer->endFrame();
                }

                SDL_Delay(16);
            }
        } else {
            graphis3d::GraphisScene scene(context);
            scene.setupLighting();

            [[maybe_unused]] auto background = scene.addCube({0, 0, -2}, {10, 6, 0.1f});
            [[maybe_unused]] auto cube1 = scene.addCube({-1.5f, 0, 0}, {0.8f, 2.0f, 0.8f});
            [[maybe_unused]] auto cube2 = scene.addCube({0.0f, 0, 0}, {0.8f, 2.0f, 0.8f});
            [[maybe_unused]] auto cube3 = scene.addCube({1.5f, 0, 0}, {0.8f, 2.0f, 0.8f});

            auto view = context.getView();
            view->setViewport({0, 0, static_cast<uint32_t>(width), static_cast<uint32_t>(height)});
            view->setPostProcessingEnabled(true);

            context.getCamera()->setProjection(45.0, static_cast<double>(width) / static_cast<double>(height), 0.1, 100.0);
            context.getCamera()->lookAt({0, 2, 8}, {0, 0, 0});

            bool quit = false;
            SDL_Event e;
            float time = 0.0f;

            while (!quit) {
                while (SDL_PollEvent(&e)) {
                    if (e.type == SDL_QUIT) {
                        quit = true;
                    }
                }

                time += 0.016f;
                auto& tcm = context.getEngine()->getTransformManager();
                const float y1 = std::sin(time * 2.0f) * 0.5f;
                tcm.setTransform(tcm.getInstance(cube1),
                        ::filament::math::mat4f::translation(::filament::math::float3{-1.5f, y1, 0.0f}) *
                        ::filament::math::mat4f::scaling(::filament::math::float3{0.8f, 2.0f, 0.8f}));

                if (context.getRenderer()->beginFrame(context.getSwapChain())) {
                    context.getRenderer()->render(view);
                    context.getRenderer()->endFrame();
                }
                SDL_Delay(16);
            }
        }
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << '\n';
    }

    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
