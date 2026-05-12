#include "api/ChrononTestSuite.hpp"

#include <filament/IndexBuffer.h>
#include <filament/LightManager.h>
#include <filament/Material.h>
#include <filament/RenderableManager.h>
#include <filament/TransformManager.h>
#include <filament/VertexBuffer.h>

#include <utils/EntityManager.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <utility>

#include <math/vec2.h>
#include <math/vec3.h>
#include <math/vec4.h>
namespace graphis3d {
namespace {

struct QuadVertex {
    ::filament::math::float3 position;
    ::filament::math::float4 tangent;
    ::filament::math::float2 uv;
};

const std::array<ChrononCard, 10> kDefaultCards{{
    {"01_checkerboard.png", { -4.8f,  1.9f, 0.0f }},
    {"02_color_bars.png",   { -2.4f,  1.9f, 0.0f }},
    {"03_grid.png",         {  0.0f,  1.9f, 0.0f }},
    {"04_radial_gradient.png",{ 2.4f,  1.9f, 0.0f }},
    {"05_resolution_chart.png",{4.8f,  1.9f, 0.0f }},
    {"06_grayscale_ramp.png",{ -4.8f, -1.1f, 0.0f }},
    {"07_circle_target.png", { -2.4f, -1.1f, 0.0f }},
    {"08_uv_grid.png",       {  0.0f, -1.1f, 0.0f }},
    {"09_text_sharpness.png",{  2.4f, -1.1f, 0.0f }},
    {"10_alpha_test.png",    {  4.8f, -1.1f, 0.0f }},
}};

constexpr std::array<const char*, 5> kCameraMoves{{
    "push-in",
    "pan",
    "tilt",
    "orbit",
    "parallax"
}};

} // namespace

ChrononTestSuite::ChrononTestSuite(FilamentContext& context, std::filesystem::path assetRoot)
    : m_engine(context.getEngine())
    , m_scene(context.getScene())
    , m_camera(context.getCamera())
    , m_assetRoot(std::move(assetRoot))
{
    m_cards.assign(kDefaultCards.begin(), kDefaultCards.end());
}

ChrononTestSuite::~ChrononTestSuite() {
    destroyStage();
}

void ChrononTestSuite::initialize(float aspectRatio) {
    m_aspectRatio = aspectRatio;

    m_camera->setProjection(45.0, m_aspectRatio, 0.1, 100.0);
    m_camera->lookAt({0.0, 0.0, 12.0}, {0.0, 0.0, 0.0});

    buildSunLight();
    buildStage();
    logAssetStatus();
}

void ChrononTestSuite::update(float deltaSeconds) {
    m_elapsed += deltaSeconds;
    updateCameraForCurrentMove();
}

void ChrononTestSuite::buildSunLight() {
    if (m_lightEntity) {
        return;
    }

    m_lightEntity = utils::EntityManager::get().create();
    ::filament::LightManager::Builder(::filament::LightManager::Type::SUN)
            .direction({ -0.35f, -1.0f, -0.2f })
            .intensity(110000.0f)
            .castShadows(false)
            .build(*m_engine, m_lightEntity);
    m_scene->addEntity(m_lightEntity);
}

void ChrononTestSuite::buildStage() {
    if (m_sharedMaterial) {
        return;
    }

    const ::filament::Material* defaultMaterial = m_engine->getDefaultMaterial();
    m_sharedMaterial = defaultMaterial->createInstance("chronon-tests-default");

    static constexpr QuadVertex vertices[] = {
        {{-0.95f, -0.54f, 0.0f}, {0.0f, 0.0f, 1.0f, 1.0f}, {0.0f, 0.0f}},
        {{ 0.95f, -0.54f, 0.0f}, {0.0f, 0.0f, 1.0f, 1.0f}, {1.0f, 0.0f}},
        {{ 0.95f,  0.54f, 0.0f}, {0.0f, 0.0f, 1.0f, 1.0f}, {1.0f, 1.0f}},
        {{-0.95f,  0.54f, 0.0f}, {0.0f, 0.0f, 1.0f, 1.0f}, {0.0f, 1.0f}},
    };

    static constexpr uint16_t indices[] = {0, 1, 2, 2, 3, 0};

    m_cardHandles.reserve(m_cards.size());

    for (const auto& card : m_cards) {
        CardHandle handle;
        handle.entity = utils::EntityManager::get().create();

        handle.vertexBuffer = ::filament::VertexBuffer::Builder()
                .vertexCount(4)
                .bufferCount(1)
                .attribute(::filament::VertexAttribute::POSITION, 0,
                        ::filament::VertexBuffer::AttributeType::FLOAT3,
                        offsetof(QuadVertex, position), sizeof(QuadVertex))
                .attribute(::filament::VertexAttribute::TANGENTS, 0,
                        ::filament::VertexBuffer::AttributeType::FLOAT4,
                        offsetof(QuadVertex, tangent), sizeof(QuadVertex))
                .attribute(::filament::VertexAttribute::UV0, 0,
                        ::filament::VertexBuffer::AttributeType::FLOAT2,
                        offsetof(QuadVertex, uv), sizeof(QuadVertex))
                .build(*m_engine);
        handle.vertexBuffer->setBufferAt(*m_engine, 0,
                ::filament::VertexBuffer::BufferDescriptor(vertices, sizeof(vertices)));

        handle.indexBuffer = ::filament::IndexBuffer::Builder()
                .indexCount(6)
                .bufferType(::filament::IndexBuffer::IndexType::USHORT)
                .build(*m_engine);
        handle.indexBuffer->setBuffer(*m_engine,
                ::filament::IndexBuffer::BufferDescriptor(indices, sizeof(indices)));

        ::filament::RenderableManager::Builder(1)
                .boundingBox({{0.0f, 0.0f, -0.01f}, {0.95f, 0.54f, 0.01f}})
                .material(0, m_sharedMaterial)
                .geometry(0, ::filament::RenderableManager::PrimitiveType::TRIANGLES,
                        handle.vertexBuffer, handle.indexBuffer)
                .build(*m_engine, handle.entity);

        auto& transformManager = m_engine->getTransformManager();
        auto instance = transformManager.getInstance(handle.entity);
        transformManager.setTransform(instance,
                ::filament::math::mat4f::translation(card.position));

        m_scene->addEntity(handle.entity);
        m_cardHandles.push_back(handle);
    }
}

void ChrononTestSuite::logAssetStatus() const {
    if (!std::filesystem::exists(m_assetRoot)) {
        std::cerr << "Chronon test asset folder missing: " << m_assetRoot.string() << '\n';
        return;
    }

    for (const auto& card : m_cards) {
        const auto path = m_assetRoot / card.filename;
        if (std::filesystem::exists(path)) {
            std::cout << "Chronon asset ready: " << path.string() << '\n';
        } else {
            std::cerr << "Chronon asset missing: " << path.string() << '\n';
        }
    }
}

float ChrononTestSuite::easeInOut(float t) noexcept {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

void ChrononTestSuite::updateCameraForCurrentMove() {
    constexpr float kMoveDuration = 3.0f;
    const float cycle = kMoveDuration * static_cast<float>(kCameraMoves.size());
    const float phase = std::fmod(m_elapsed, cycle);
    const size_t moveIndex = static_cast<size_t>(phase / kMoveDuration);
    const float localT = easeInOut((phase - (kMoveDuration * static_cast<float>(moveIndex))) / kMoveDuration);

    ::filament::math::double3 eye{0.0, 0.0, 12.0};
    ::filament::math::double3 center{0.0, 0.0, 0.0};

    switch (moveIndex) {
    case 0:
        eye = {0.0, 0.0, 12.0 - (6.5 * localT)};
        break;
    case 1:
        eye = {-5.0 + (10.0 * localT), 0.0, 8.0};
        break;
    case 2:
        eye = {0.0, -3.0 + (6.0 * localT), 8.0};
        break;
    case 3: {
        const double angle = localT * 6.283185307179586;
        eye = {std::cos(angle) * 8.0, 1.5, std::sin(angle) * 8.0};
        break;
    }
    case 4:
        eye = {2.0 + (3.0 * localT), 1.0, 10.0 - (3.0 * localT)};
        center = {0.5 * localT, 0.0, 0.0};
        break;
    default:
        break;
    }

    m_camera->lookAt(eye, center);
}

void ChrononTestSuite::destroyStage() {
    if (m_engine) {
        for (auto& handle : m_cardHandles) {
            if (handle.entity) {
                m_scene->removeEntities(&handle.entity, 1);
                m_engine->destroy(handle.entity);
            }
            if (handle.indexBuffer) {
                m_engine->destroy(handle.indexBuffer);
            }
            if (handle.vertexBuffer) {
                m_engine->destroy(handle.vertexBuffer);
            }
        }
        m_cardHandles.clear();

        if (m_sharedMaterial) {
            m_engine->destroy(m_sharedMaterial);
            m_sharedMaterial = nullptr;
        }

        if (m_lightEntity) {
            m_scene->removeEntities(&m_lightEntity, 1);
            m_engine->destroy(m_lightEntity);
            m_lightEntity = {};
        }
    }
}

} // namespace graphis3d
