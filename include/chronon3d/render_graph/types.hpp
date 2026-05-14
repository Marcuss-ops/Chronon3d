#pragma once

#include <chronon3d/core/frame.hpp>
#include <chronon3d/core/types.hpp>
#include <chronon3d/core/framebuffer.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/math/transform.hpp>
#include <chronon3d/scene/camera/camera.hpp>
#include <string>
#include <string_view>
#include <unordered_map>
#include <memory>
#include <span>

namespace chronon3d {

// Forward declare for RenderGraphExecutionContext
class SoftwareRenderer;

namespace render_graph {

using NodeId = usize;

enum class NodeKind {
    Output,
    Transform,
    Source,
    Effect,
    Composite,
};

enum class RenderPassKind {
    Output,
    Transform,
    Source,
    Effect,
    Composite,
};

struct RenderCacheKey {
    std::string scope;
    Frame frame{0};
    i32 width{0};
    i32 height{0};
    u64 params_hash{0};
    u64 source_hash{0};
    u64 input_hash{0};

    [[nodiscard]] u64 digest() const;
    [[nodiscard]] bool operator==(const RenderCacheKey&) const = default;
};

struct RenderCacheKeyHash {
    [[nodiscard]] size_t operator()(const RenderCacheKey& key) const noexcept;
};

struct RenderPassResult {
    std::shared_ptr<Framebuffer> framebuffer;
    RenderState state{};
    bool has_state{false};
};

struct RenderGraphExecutionContext {
    SoftwareRenderer& renderer;
    const Camera& camera;
    Frame frame{0};
    i32 width{0};
    i32 height{0};
    bool diagnostic{false};
};

// Forward declare RenderNode for instructions or graph nodes
class RenderGraphNode {
public:
    virtual ~RenderGraphNode() = default;

    [[nodiscard]] virtual NodeKind kind() const = 0;
    [[nodiscard]] virtual std::string_view label() const = 0;
    [[nodiscard]] virtual const RenderCacheKey& cache_key() const = 0;
    [[nodiscard]] virtual std::span<const NodeId> inputs() const = 0;
};

u64 hash_bytes(const void* data, usize size);
u64 hash_string(std::string_view value);
u64 hash_combine(u64 seed, u64 value);
u64 hash_vec2(const Vec2& value);
u64 hash_vec3(const Vec3& value);
u64 hash_color(const Color& color);
u64 hash_transform(const Transform& transform);

} // namespace render_graph
} // namespace chronon3d
