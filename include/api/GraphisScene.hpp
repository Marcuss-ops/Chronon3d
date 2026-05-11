#pragma once

#include "FilamentContext.hpp"
#include "ResourceWrappers.hpp"
#include "GeometryProvider.hpp"

#include <filament/Engine.h>
#include <filament/Scene.h>
#include <filament/TransformManager.h>
#include <filament/RenderableManager.h>
#include <filament/Material.h>
#include <filament/MaterialInstance.h>

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
     * @brief Adds a simple cube to the scene.
     */
    utils::Entity addCube(filament::math::float3 position, 
                          filament::math::float3 scale = {1,1,1}, 
                          filament::math::float4 color = {1,1,1,1}) {
        
        auto entity = utils::EntityManager::get().create();
        
        // Setup geometry
        filament::VertexBuffer* vb = nullptr;
        filament::IndexBuffer* ib = nullptr;
        GeometryProvider::createCube(*m_engine, &vb, &ib);
        
        // For simplicity in this boilerplate, we'll leak VB/IB for now or store them in a pool.
        // In a real impl, we'd use ResourceWrappers.

        filament::RenderableManager::Builder(1)
            .boundingBox({ {0,0,0}, {0.5,0.5,0.5} })
            .geometry(0, filament::RenderableManager::PrimitiveType::TRIANGLES, vb, ib)
            .build(*m_engine, entity);

        auto& tcm = m_engine->getTransformManager();
        auto instance = tcm.getInstance(entity);
        tcm.setTransform(instance, filament::math::mat4f::translation(position) * filament::math::mat4f::scaling(scale));

        m_scene->addEntity(entity);
        m_entities.push_back(entity);
        
        return entity;
    }

    void setBackgroundColor(filament::math::float4 color) {
        // Implementation for skybox/background
    }

private:
    FilamentContext& m_context;
    filament::Engine* m_engine;
    filament::Scene* m_scene;
    std::vector<utils::Entity> m_entities;
};

} // namespace graphis3d
