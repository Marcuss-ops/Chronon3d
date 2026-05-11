#pragma once

#include <filament/Engine.h>
#include <filament/Renderer.h>
#include <filament/Scene.h>
#include <filament/View.h>
#include <filament/Camera.h>
#include <filament/SwapChain.h>
#include <filament/Viewport.h>
#include <utils/Entity.h>
#include <utils/EntityManager.h>

#include <memory>
#include <vector>
#include <optional>

namespace graphis3d {

/**
 * @brief Strict RAII wrapper for Filament core context.
 * 
 * Ensures correct creation and destruction order:
 * Engine -> SwapChain -> Renderer -> View -> Scene -> Camera
 */
class FilamentContext {
public:
    struct Deleter {
        void operator()(filament::Engine* e) const { filament::Engine::destroy(&e); }
    };

    using EnginePtr = std::unique_ptr<filament::Engine, Deleter>;

    explicit FilamentContext(void* nativeWindow) {
        m_engine = EnginePtr(filament::Engine::create());
        if (!m_engine) {
            throw std::runtime_error("Failed to create Filament Engine");
        }

        m_swapChain = m_engine->createSwapChain(nativeWindow);
        m_renderer = m_engine->createRenderer();
        m_scene = m_engine->createScene();
        m_view = m_engine->createView();
        
        m_cameraEntity = utils::EntityManager::get().create();
        m_camera = m_engine->createCamera(m_cameraEntity);

        m_view->setScene(m_scene);
        m_view->setCamera(m_camera);
    }

    ~FilamentContext() {
        // Reverse order of destruction as recommended by user:
        // Entities -> Scene -> View -> Camera -> Renderer -> SwapChain -> Engine
        if (m_engine) {
            m_engine->destroy(m_cameraEntity); // Entities
            m_engine->destroy(m_scene);        // Scene
            m_engine->destroy(m_view);         // View
            // Camera is associated with m_cameraEntity, it's destroyed when entity is destroyed
            m_engine->destroy(m_renderer);     // Renderer
            m_engine->destroy(m_swapChain);    // SwapChain
            
            // Engine itself is destroyed by m_engine's unique_ptr (Deleter calls Engine::destroy)
        }
    }

    // Disable copy
    FilamentContext(const FilamentContext&) = delete;
    FilamentContext& operator=(const FilamentContext&) = delete;

    // Allow move
    FilamentContext(FilamentContext&&) noexcept = default;
    FilamentContext& operator=(FilamentContext&&) noexcept = default;

    [[nodiscard]] filament::Engine* getEngine() const noexcept { return m_engine.get(); }
    [[nodiscard]] filament::Renderer* getRenderer() const noexcept { return m_renderer; }
    [[nodiscard]] filament::SwapChain* getSwapChain() const noexcept { return m_swapChain; }
    [[nodiscard]] filament::View* getView() const noexcept { return m_view; }
    [[nodiscard]] filament::Scene* getScene() const noexcept { return m_scene; }
    [[nodiscard]] filament::Camera* getCamera() const noexcept { return m_camera; }

private:
    EnginePtr m_engine;
    filament::SwapChain* m_swapChain = nullptr;
    filament::Renderer* m_renderer = nullptr;
    filament::View* m_view = nullptr;
    filament::Scene* m_scene = nullptr;
    filament::Camera* m_camera = nullptr;
    utils::Entity m_cameraEntity;
};

} // namespace graphis3d
