#pragma once

#include "core/FilamentContext.hpp"

#include <math/vec3.h>

#include <filesystem>
#include <string>
#include <vector>

namespace filament {
class IndexBuffer;
class MaterialInstance;
class VertexBuffer;
} // namespace filament

namespace graphis3d {

struct ChrononCard {
    std::string filename;
    ::filament::math::float3 position;
};

class ChrononTestSuite {
public:
    explicit ChrononTestSuite(FilamentContext& context,
            std::filesystem::path assetRoot = std::filesystem::path("assets/test"));
    ~ChrononTestSuite();

    void initialize(float aspectRatio);
    void update(float deltaSeconds);

private:
    struct CardHandle {
        utils::Entity entity{};
        ::filament::VertexBuffer* vertexBuffer = nullptr;
        ::filament::IndexBuffer* indexBuffer = nullptr;
    };

    void buildStage();
    void buildSunLight();
    void logAssetStatus() const;
    void updateCameraForCurrentMove();
    void destroyStage();

    static float easeInOut(float t) noexcept;

    ::filament::Engine* m_engine = nullptr;
    ::filament::Scene* m_scene = nullptr;
    ::filament::Camera* m_camera = nullptr;
    std::filesystem::path m_assetRoot;
    ::filament::MaterialInstance* m_sharedMaterial = nullptr;
    utils::Entity m_lightEntity{};
    std::vector<ChrononCard> m_cards;
    std::vector<CardHandle> m_cardHandles;
    float m_elapsed = 0.0f;
    float m_aspectRatio = 16.0f / 9.0f;
};

} // namespace graphis3d
