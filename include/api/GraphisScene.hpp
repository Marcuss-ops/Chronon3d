#pragma once

#include "core/FilamentContext.hpp"
#include "core/ResourceWrappers.hpp"
#include "core/GeometryProvider.hpp"

#include <filament/Engine.h>
#include <filament/Scene.h>
#include <filament/TransformManager.h>
#include <filament/RenderableManager.h>
#include <filament/MaterialInstance.h>
#include <filament/LightManager.h>
#include <filament/Color.h>

#include <math/vec3.h>
#include <math/mat4.h>

#include <vector>
#include <string>
#include <memory>

namespace graphis3d {

class GraphisScene {
public:
    explicit GraphisScene(FilamentContext& context) : m_context(context) {
        m_engine = context.getEngine();
        m_scene = context.getScene();
    }

    /**
     * @brief Adds a simple cube to the scene with a color.
     */
    utils::Entity addCube(filament::math::float3 position, 
                          filament::math::float3 scale = {1,1,1}, 
                          filament::math::float4 color = {1,0.5,0,1}) {
        
        auto entity = utils::EntityManager::get().create();
        
        // Setup geometry
        filament::VertexBuffer* vb = nullptr;
        filament::IndexBuffer* ib = nullptr;
        GeometryProvider::createCube(*m_engine, &vb, &ib);
        
        // Simple Lit Material (Using a placeholder for now, or building a simple one)
        // In a full impl, we'd compile this with filamat
        
        filament::RenderableManager::Builder(1)
            .boundingBox({ {0,0,0}, {0.5,0.5,0.5} })
            .geometry(0, filament::RenderableManager::PrimitiveType::TRIANGLES, vb, ib)
            .build(*m_engine, entity);

        auto& tcm = m_engine->getTransformManager();
        auto instance = tcm.getInstance(entity);
        tcm.setTransform(instance, filament::math::mat4f::translation(position) * filament::math::mat4f::scaling(scale));

        m_scene->addEntity(entity);
        return entity;
    }

    void setupLighting() {
        // Add a directional light
        auto light = utils::EntityManager::get().create();
        filament::LightManager::Builder(filament::LightManager::Type::DIRECTIONAL)
            .color(filament::math::float3{1.0f, 1.0f, 1.0f})
            .intensity(100000.0f)
            .direction({0.0f, -1.0f, -1.0f})
            .castShadows(true)
            .build(*m_engine, light);
        m_scene->addEntity(light);
    }

private:
    FilamentContext& m_context;
    filament::Engine* m_engine;
    filament::Scene* m_scene;
    std::vector<utils::Entity> m_entities;
};

} // namespace graphis3d
