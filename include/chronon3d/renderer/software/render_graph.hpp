#pragma once

#include <chronon3d/core/frame.hpp>
#include <chronon3d/core/types.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/math/transform.hpp>
#include <chronon3d/renderer/software/framebuffer.hpp>
#include <chronon3d/scene/camera.hpp>
#include <chronon3d/scene/effect_stack.hpp>
#include <chronon3d/scene/layer.hpp>
#include <chronon3d/scene/layer_effect.hpp>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace chronon3d {
class SoftwareRenderer;
class RenderNode;

namespace rendergraph {

using NodeId = usize;

enum class RenderNodeKind {
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

struct RenderGraphExecutionState {
    std::unordered_map<NodeId, RenderPassResult> results;
    std::unordered_map<RenderCacheKey, RenderPassResult, RenderCacheKeyHash> cache;
};

struct RenderGraphExecutionContext {
    SoftwareRenderer& renderer;
    const Camera& camera;
    Frame frame{0};
    i32 width{0};
    i32 height{0};
    bool diagnostic{false};
};

class RenderNode {
public:
    virtual ~RenderNode() = default;

    [[nodiscard]] virtual RenderNodeKind kind() const = 0;
    [[nodiscard]] virtual std::string_view label() const = 0;
    [[nodiscard]] virtual const RenderCacheKey& cache_key() const = 0;
    [[nodiscard]] virtual std::span<const NodeId> inputs() const = 0;
};

class RenderPass {
public:
    virtual ~RenderPass() = default;

    [[nodiscard]] virtual RenderPassKind kind() const = 0;
    [[nodiscard]] virtual std::string_view label() const = 0;
    [[nodiscard]] virtual const RenderCacheKey& cache_key() const = 0;
    [[nodiscard]] virtual bool cacheable() const = 0;
    [[nodiscard]] virtual RenderPassResult execute(RenderGraphExecutionContext& ctx,
                                                   RenderGraphExecutionState& state,
                                                   NodeId self) const = 0;
};

class RenderGraph {
public:
    using PassExecutor = std::function<RenderPassResult(RenderGraphExecutionContext&, RenderGraphExecutionState&, NodeId)>;

    [[nodiscard]] NodeId add_output(std::string label,
                                    RenderCacheKey key);

    [[nodiscard]] NodeId add_transform(std::string label,
                                       RenderCacheKey key,
                                       NodeId input,
                                       Transform transform,
                                       RenderState base_state);

    [[nodiscard]] NodeId add_source(std::string label,
                                    RenderCacheKey key,
                                    NodeId input,
                                    const ::chronon3d::RenderNode& node);

    [[nodiscard]] NodeId add_layer_source(std::string label,
                                          RenderCacheKey key,
                                          NodeId input,
                                          const Layer& layer);

    [[nodiscard]] NodeId add_effect(std::string label,
                                    RenderCacheKey key,
                                    NodeId input,
                                    const Layer& layer);

    [[nodiscard]] NodeId add_composite(std::string label,
                                       RenderCacheKey key,
                                       NodeId bottom,
                                       NodeId top,
                                       BlendMode mode);

    [[nodiscard]] std::string debug_dot() const;

    [[nodiscard]] std::unique_ptr<Framebuffer> execute(RenderGraphExecutionContext& ctx) const;

    [[nodiscard]] bool empty() const { return m_passes.empty(); }
    [[nodiscard]] usize size() const { return m_passes.size(); }

    struct Entry {
        std::unique_ptr<RenderNode> node;
        std::unique_ptr<RenderPass> pass;
    };

    [[nodiscard]] const Entry& get_entry(usize index) const { return m_passes[index]; }

private:
    [[nodiscard]] NodeId add_entry(std::unique_ptr<RenderNode> node,
                                   std::unique_ptr<RenderPass> pass);

    std::vector<Entry> m_passes;
};


[[nodiscard]] u64 hash_combine(u64 seed, u64 value);
[[nodiscard]] u64 hash_string(std::string_view value);
[[nodiscard]] u64 hash_bytes(const void* data, usize size);
[[nodiscard]] u64 hash_transform(const Transform& transform);
[[nodiscard]] u64 hash_color(const Color& color);
[[nodiscard]] u64 hash_vec2(const Vec2& value);
[[nodiscard]] u64 hash_vec3(const Vec3& value);

} // namespace rendergraph
} // namespace chronon3d
