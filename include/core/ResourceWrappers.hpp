#pragma once

#include <filament/Engine.h>
#include <utils/Entity.h>
#include <utils/EntityManager.h>

namespace graphis3d {

/**
 * @brief RAII wrapper for Filament Entities.
 * 
 * Ensures the entity is destroyed by the engine when the wrapper goes out of scope.
 */
class EntityWrapper {
public:
    EntityWrapper(filament::Engine& engine) : m_engine(&engine) {
        m_entity = utils::EntityManager::get().create();
    }

    ~EntityWrapper() {
        if (m_engine && m_entity) {
            m_engine->destroy(m_entity);
        }
    }

    // Non-copyable
    EntityWrapper(const EntityWrapper&) = delete;
    EntityWrapper& operator=(const EntityWrapper&) = delete;

    // Movable
    EntityWrapper(EntityWrapper&& other) noexcept 
        : m_engine(other.m_engine), m_entity(other.m_entity) {
        other.m_entity = {};
    }

    EntityWrapper& operator=(EntityWrapper&& other) noexcept {
        if (this != &other) {
            if (m_engine && m_entity) m_engine->destroy(m_entity);
            m_engine = other.m_engine;
            m_entity = other.m_entity;
            other.m_entity = {};
        }
        return *this;
    }

    [[nodiscard]] utils::Entity get() const noexcept { return m_entity; }
    operator utils::Entity() const noexcept { return m_entity; }

private:
    filament::Engine* m_engine;
    utils::Entity m_entity;
};

/**
 * @brief Template RAII wrapper for Filament objects (Material, VertexBuffer, etc.).
 */
template <typename T>
class FilamentResource {
public:
    FilamentResource(filament::Engine& engine, T* resource) 
        : m_engine(&engine), m_resource(resource) {}

    ~FilamentResource() {
        if (m_engine && m_resource) {
            m_engine->destroy(m_resource);
        }
    }

    // Non-copyable
    FilamentResource(const FilamentResource&) = delete;
    FilamentResource& operator=(const FilamentResource&) = delete;

    // Movable
    FilamentResource(FilamentResource&& other) noexcept 
        : m_engine(other.m_engine), m_resource(other.m_resource) {
        other.m_resource = nullptr;
    }

    FilamentResource& operator=(FilamentResource&& other) noexcept {
        if (this != &other) {
            if (m_engine && m_resource) m_engine->destroy(m_resource);
            m_engine = other.m_engine;
            m_resource = other.m_resource;
            other.m_resource = nullptr;
        }
        return *this;
    }

    [[nodiscard]] T* get() const noexcept { return m_resource; }
    T* operator->() const noexcept { return m_resource; }
    explicit operator bool() const noexcept { return m_resource != nullptr; }

private:
    filament::Engine* m_engine;
    T* m_resource;
};

} // namespace graphis3d
