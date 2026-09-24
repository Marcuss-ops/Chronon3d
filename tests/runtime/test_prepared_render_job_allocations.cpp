#include <doctest/doctest.h>

#include <chronon3d/api/render_engine.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/registry/shape_registry.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>

#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <memory>
#include <memory_resource>
#include <new>

namespace chronon3d::test::allocation_guard {

inline std::atomic<bool> enabled{false};
inline std::atomic<std::size_t> allocations{0};
inline std::atomic<std::size_t> bytes{0};

void reset() noexcept {
    allocations.store(0, std::memory_order_relaxed);
    bytes.store(0, std::memory_order_relaxed);
}

void start() noexcept {
    reset();
    enabled.store(true, std::memory_order_release);
}

void stop() noexcept {
    enabled.store(false, std::memory_order_release);
}

} // namespace chronon3d::test::allocation_guard

namespace {

void record_allocation(std::size_t size) noexcept {
    if (chronon3d::test::allocation_guard::enabled.load(std::memory_order_acquire)) {
        chronon3d::test::allocation_guard::allocations.fetch_add(
            1, std::memory_order_relaxed);
        chronon3d::test::allocation_guard::bytes.fetch_add(
            size, std::memory_order_relaxed);
    }
}

void* allocate(std::size_t size) {
    if (void* p = std::malloc(size == 0 ? 1 : size)) {
        record_allocation(size);
        return p;
    }
    throw std::bad_alloc{};
}

void* allocate_aligned(std::size_t size, std::size_t alignment) {
    if (alignment <= alignof(std::max_align_t)) {
        return allocate(size);
    }
    void* p = nullptr;
    if (posix_memalign(&p, alignment, size == 0 ? alignment : size) != 0) {
        throw std::bad_alloc{};
    }
    record_allocation(size);
    return p;
}

} // namespace

void* operator new(std::size_t size) {
    return allocate(size);
}
void* operator new[](std::size_t size) {
    return allocate(size);
}
void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    try { return allocate(size); } catch (...) { return nullptr; }
}
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
    try { return allocate(size); } catch (...) { return nullptr; }
}
void* operator new(std::size_t size, std::align_val_t alignment) {
    return allocate_aligned(size, static_cast<std::size_t>(alignment));
}
void* operator new[](std::size_t size, std::align_val_t alignment) {
    return allocate_aligned(size, static_cast<std::size_t>(alignment));
}
void* operator new(std::size_t size, std::align_val_t alignment,
                   const std::nothrow_t&) noexcept {
    try { return allocate_aligned(size, static_cast<std::size_t>(alignment)); }
    catch (...) { return nullptr; }
}
void* operator new[](std::size_t size, std::align_val_t alignment,
                     const std::nothrow_t&) noexcept {
    try { return allocate_aligned(size, static_cast<std::size_t>(alignment)); }
    catch (...) { return nullptr; }
}

void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void operator delete(void* p, std::align_val_t) noexcept { std::free(p); }
void operator delete[](void* p, std::align_val_t) noexcept { std::free(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept {
    std::free(p);
}
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept {
    std::free(p);
}

namespace {

chronon3d::Composition empty_composition() {
    return chronon3d::Composition{
        chronon3d::CompositionSpec{
            .name = "prepared-render-job-allocation-guard",
            .width = 32,
            .height = 32,
            .frame_rate = {30, 1},
            .duration = chronon3d::Frame{320}},
        [](const chronon3d::FrameContext&) { return chronon3d::Scene{}; }};
}

chronon3d::Composition static_rect_composition() {
    // The compatibility Composition callback is still allowed to exist, but
    // it must not rebuild its shape registry on every frame.  Keep that
    // registry with the composition so the allocation guard measures the
    // actual prepared render path rather than callback-local setup.
    struct StaticSceneState {
        chronon3d::registry::ShapeRegistry shape_registry =
            chronon3d::registry::make_default_shape_registry();
        std::pmr::unsynchronized_pool_resource scene_resource;
    };
    auto state = std::make_shared<StaticSceneState>();
    return chronon3d::Composition{
        chronon3d::CompositionSpec{
            .name = "prepared-render-job-static-allocation-guard",
            .width = 32,
            .height = 32,
            .frame_rate = {30, 1},
            .duration = chronon3d::Frame{120}},
        [state](const chronon3d::FrameContext& context) {
            // This is the explicitly quarantined compatibility callback. Its
            // scene graph uses a persistent PMR pool, so the guard can prove
            // that callback setup does not leak heap allocations into the
            // prepared render loop after warm-up.
            chronon3d::SceneBuilder scene(
                &state->scene_resource, &state->shape_registry);
            scene.rect("static-rect", {
                .size = {16.0f, 16.0f},
                .color = chronon3d::Color{0.2f, 0.5f, 0.8f, 1.0f},
            });
            return scene.build();
        }};
}

} // namespace

TEST_CASE("PreparedRenderJob core hot loop allocation guard") {
    chronon3d::RenderEngine engine;
    auto job = engine.prepare(empty_composition());

    // Warm up runtime/TBB/cache state before observing the core frame loop.
    for (int frame = 0; frame < 10; ++frame) {
        REQUIRE(job.render(chronon3d::Frame{frame}) != nullptr);
    }

    chronon3d::test::allocation_guard::start();
    for (int frame = 10; frame < 310; ++frame) {
        REQUIRE(job.render(chronon3d::Frame{frame}) != nullptr);
    }
    chronon3d::test::allocation_guard::stop();

    INFO("heap allocations = ",
         chronon3d::test::allocation_guard::allocations.load());
    INFO("heap bytes = ",
         chronon3d::test::allocation_guard::bytes.load());
    CHECK(chronon3d::test::allocation_guard::allocations.load() == 0);
    CHECK(chronon3d::test::allocation_guard::bytes.load() == 0);
}

TEST_CASE("PreparedRenderJob compatibility scene allocation is bounded") {
    chronon3d::RenderEngine engine;
    auto job = engine.prepare(static_rect_composition());

    for (int frame = 0; frame < 10; ++frame) {
        REQUIRE(job.render(chronon3d::Frame{frame}) != nullptr);
    }

    chronon3d::test::allocation_guard::start();
    for (int frame = 10; frame < 110; ++frame) {
        REQUIRE(job.render(chronon3d::Frame{frame}) != nullptr);
    }
    chronon3d::test::allocation_guard::stop();

    const auto allocations =
        chronon3d::test::allocation_guard::allocations.load();
    const auto bytes = chronon3d::test::allocation_guard::bytes.load();
    INFO("compatibility callback heap allocations = ", allocations);
    INFO("compatibility callback heap bytes = ", bytes);

    // This test is deliberately a quarantine contract for the deprecated
    // Composition::SceneFunction adapter.  The zero-allocation gate applies
    // to the compiled program path; this legacy callback is allowed one
    // bounded scene-materialization allocation per frame until demolition.
    CHECK(allocations <= 100);
    // The quarantined Scene materialization is currently 1,232 bytes per
    // callback on this ABI. Keep a small per-frame margin while the exact
    // one-allocation-per-frame bound above remains the primary invariant.
    CHECK(bytes <= 128000);
}
