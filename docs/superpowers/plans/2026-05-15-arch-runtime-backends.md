# Chronon3d Architectural Refactor — Runtime & Backends

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Physically separate Runtime orchestration and Backend implementations into dedicated directories, removing both from their current ambiguous locations.

**Architecture:** Two rounds. Round 1 isolates `src/runtime/` (evaluation, pipeline, graph executor). Round 2 isolates `src/backends/` (software renderer, IO, FFmpeg). Each round ends with a green build and a commit.

**Tech Stack:** C++20, CMake, no new dependencies.

---

## Round 1 — Runtime Isolation

Files moving IN to `src/runtime/` and `include/chronon3d/runtime/`:
- `src/effects/evaluation/legacy_scene_adapter.cpp`
- `src/effects/evaluation/timeline_evaluator.cpp`
- `src/core/pipeline.cpp`
- `src/render_graph/graph_executor.cpp`
- `include/chronon3d/evaluation/evaluated_layer.hpp`
- `include/chronon3d/evaluation/evaluated_scene.hpp`
- `include/chronon3d/evaluation/legacy_scene_adapter.hpp`
- `include/chronon3d/evaluation/timeline_evaluator.hpp`
- `include/chronon3d/core/pipeline.hpp`
- `include/chronon3d/render_graph/graph_executor.hpp`

Directories deleted after Round 1: `include/chronon3d/evaluation/`, `src/effects/evaluation/`, `src/core/`.

CMake changes: `chronon3d_core` deleted, `chronon3d_runtime` created, `chronon3d_renderer` renamed to `chronon3d_scene`, `chronon3d_effects` loses evaluation glob.

---

### Task 1 — Create `include/chronon3d/runtime/` and write the 6 moved headers

**Files:**
- Create: `include/chronon3d/runtime/evaluated_layer.hpp`
- Create: `include/chronon3d/runtime/evaluated_scene.hpp`
- Create: `include/chronon3d/runtime/legacy_scene_adapter.hpp`
- Create: `include/chronon3d/runtime/timeline_evaluator.hpp`
- Create: `include/chronon3d/runtime/graph_executor.hpp`
- Create: `include/chronon3d/runtime/pipeline.hpp`
- Delete: `include/chronon3d/evaluation/evaluated_layer.hpp`
- Delete: `include/chronon3d/evaluation/evaluated_scene.hpp`
- Delete: `include/chronon3d/evaluation/legacy_scene_adapter.hpp`
- Delete: `include/chronon3d/evaluation/timeline_evaluator.hpp`
- Delete: `include/chronon3d/core/pipeline.hpp`
- Delete: `include/chronon3d/render_graph/graph_executor.hpp`

- [ ] **Step 1: Write `include/chronon3d/runtime/evaluated_layer.hpp`**

```cpp
#pragma once

#include <chronon3d/math/transform.hpp>
#include <chronon3d/scene/layer/depth_role.hpp>
#include <chronon3d/scene/effects/effect_stack.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/description/visual_desc.hpp>
#include <string>
#include <vector>

namespace chronon3d {

struct EvaluatedLayer {
    std::string name;

    bool      visible{true};
    Transform world_transform;
    f32       opacity{1.0f};

    bool      is_3d{false};
    DepthRole depth_role{DepthRole::None};
    f32       resolved_z{0.0f};

    BlendMode blend_mode{BlendMode::Normal};

    std::vector<VisualDesc> visuals;

    EffectStack resolved_effects;
};

} // namespace chronon3d
```

- [ ] **Step 2: Write `include/chronon3d/runtime/evaluated_scene.hpp`**

```cpp
#pragma once

#include <chronon3d/core/time.hpp>
#include <chronon3d/core/types.hpp>
#include <chronon3d/scene/camera/camera_2_5d.hpp>
#include <chronon3d/runtime/evaluated_layer.hpp>
#include <optional>
#include <vector>

namespace chronon3d {

struct EvaluatedScene {
    Frame frame{0};
    i32   width{1280};
    i32   height{720};

    std::vector<EvaluatedLayer> layers;

    std::optional<Camera2_5D> camera;
};

} // namespace chronon3d
```

- [ ] **Step 3: Write `include/chronon3d/runtime/legacy_scene_adapter.hpp`**

```cpp
#pragma once

#include <chronon3d/runtime/evaluated_scene.hpp>
#include <chronon3d/scene/scene.hpp>

namespace chronon3d {

class LegacySceneAdapter {
public:
    [[nodiscard]] Scene convert(const EvaluatedScene& evaluated,
                                std::pmr::memory_resource* res = std::pmr::get_default_resource()) const;
};

} // namespace chronon3d
```

- [ ] **Step 4: Write `include/chronon3d/runtime/timeline_evaluator.hpp`**

```cpp
#pragma once

#include <chronon3d/description/scene_description.hpp>
#include <chronon3d/runtime/evaluated_scene.hpp>

namespace chronon3d {

class TimelineEvaluator {
public:
    [[nodiscard]] EvaluatedScene evaluate(const SceneDescription& scene, Frame frame) const;
};

} // namespace chronon3d
```

- [ ] **Step 5: Write `include/chronon3d/runtime/graph_executor.hpp`**

```cpp
#pragma once

#include <chronon3d/render_graph/render_graph.hpp>
#include <unordered_map>

namespace chronon3d::graph {

class GraphExecutor {
public:
    GraphExecutor();

    std::shared_ptr<Framebuffer> execute(
        RenderGraph& graph,
        GraphNodeId output,
        RenderGraphContext& ctx
    );

    std::shared_ptr<Framebuffer> execute(RenderGraph& graph, RenderGraphContext& ctx) {
        return execute(graph, graph.output(), ctx);
    }

private:
    std::unordered_map<GraphNodeId, std::shared_ptr<Framebuffer>> m_temp;
    std::unordered_map<GraphNodeId, u64> m_resolved_key_digest;

    std::shared_ptr<Framebuffer> execute_node(
        RenderGraph& graph,
        GraphNodeId id,
        RenderGraphContext& ctx
    );
};

} // namespace chronon3d::graph
```

- [ ] **Step 6: Write `include/chronon3d/runtime/pipeline.hpp`**

```cpp
#pragma once

#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/renderer/software/renderer.hpp>
#include <chronon3d/core/arena.hpp>
#include <chronon3d/core/framebuffer.hpp>
#include <functional>
#include <memory>
#include <vector>

namespace chronon3d {

struct EvaluatedFrame {
    Frame frame;
    std::shared_ptr<FrameArena> arena;
    Scene scene;
};

struct RenderedFrame {
    Frame frame;
    std::shared_ptr<Framebuffer> framebuffer;
    std::shared_ptr<FrameArena> arena;
};

class RenderPipeline {
public:
    RenderPipeline(std::shared_ptr<const Composition> composition,
                   std::shared_ptr<Renderer> renderer)
        : m_composition(std::move(composition))
        , m_renderer(std::move(renderer)) {}

    void run(Frame start, Frame end, std::function<void(RenderedFrame)> output_callback);

private:
    std::shared_ptr<const Composition> m_composition;
    std::shared_ptr<Renderer> m_renderer;
};

} // namespace chronon3d
```

- [ ] **Step 7: Delete the 6 old headers**

```
del include\chronon3d\evaluation\evaluated_layer.hpp
del include\chronon3d\evaluation\evaluated_scene.hpp
del include\chronon3d\evaluation\legacy_scene_adapter.hpp
del include\chronon3d\evaluation\timeline_evaluator.hpp
del include\chronon3d\core\pipeline.hpp
del include\chronon3d\render_graph\graph_executor.hpp
rmdir include\chronon3d\evaluation
```

---

### Task 2 — Create `src/runtime/` and write the 4 moved sources

**Files:**
- Create: `src/runtime/evaluated_layer.cpp` (note: no .cpp exists, this task is just the 4 existing .cpp files)
- Create: `src/runtime/legacy_scene_adapter.cpp`
- Create: `src/runtime/timeline_evaluator.cpp`
- Create: `src/runtime/pipeline.cpp`
- Create: `src/runtime/graph_executor.cpp`
- Delete: `src/effects/evaluation/legacy_scene_adapter.cpp`
- Delete: `src/effects/evaluation/timeline_evaluator.cpp`
- Delete: `src/core/pipeline.cpp`
- Delete: `src/render_graph/graph_executor.cpp`

- [ ] **Step 1: Write `src/runtime/legacy_scene_adapter.cpp`** (same body, updated include)

```cpp
#include <chronon3d/runtime/legacy_scene_adapter.hpp>
#include <chronon3d/scene/layer/layer.hpp>
#include <chronon3d/scene/layer/render_node.hpp>
#include <chronon3d/scene/shape.hpp>
#include <variant>

namespace chronon3d {

namespace {

RenderNode make_render_node(const VisualDesc& vd,
                            const Transform& layer_transform,
                            std::pmr::memory_resource* res) {
    RenderNode node(res);

    std::visit([&node, &layer_transform, res](const auto& v) {
        using T = std::decay_t<decltype(v)>;

        if constexpr (std::is_same_v<T, RectParams>) {
            node.name            = std::pmr::string{"rect", res};
            node.shape.type      = ShapeType::Rect;
            node.shape.rect.size = v.size;
            node.color           = v.color;
            node.world_transform.position = layer_transform.position + v.pos;
            node.world_transform.anchor   = {v.size.x * 0.5f, v.size.y * 0.5f, 0.0f};

        } else if constexpr (std::is_same_v<T, RoundedRectParams>) {
            node.name                         = std::pmr::string{"rrect", res};
            node.shape.type                   = ShapeType::RoundedRect;
            node.shape.rounded_rect.size      = v.size;
            node.shape.rounded_rect.radius    = v.radius;
            node.color                        = v.color;
            node.world_transform.position     = layer_transform.position + v.pos;
            node.world_transform.anchor       = {v.size.x * 0.5f, v.size.y * 0.5f, 0.0f};

        } else if constexpr (std::is_same_v<T, CircleParams>) {
            node.name                     = std::pmr::string{"circle", res};
            node.shape.type               = ShapeType::Circle;
            node.shape.circle.radius      = v.radius;
            node.color                    = v.color;
            node.world_transform.position = layer_transform.position + v.pos;
            node.world_transform.anchor   = {v.radius, v.radius, 0.0f};

        } else if constexpr (std::is_same_v<T, LineParams>) {
            node.name                     = std::pmr::string{"line", res};
            node.shape.type               = ShapeType::Line;
            node.shape.line.to            = v.to - v.from;
            node.shape.line.thickness     = v.thickness;
            node.color                    = v.color;
            node.world_transform.position = layer_transform.position + v.from;
            node.world_transform.anchor   = {0.0f, 0.0f, 0.0f};

        } else if constexpr (std::is_same_v<T, TextParams>) {
            node.name                     = std::pmr::string{"text", res};
            node.shape.type               = ShapeType::Text;
            node.shape.text.text          = v.content;
            node.shape.text.style         = v.style;
            node.color                    = v.style.color;
            node.world_transform.position = layer_transform.position + v.pos;

        } else if constexpr (std::is_same_v<T, ImageParams>) {
            node.name                     = std::pmr::string{"image", res};
            node.shape.type               = ShapeType::Image;
            node.shape.image.path         = v.path;
            node.shape.image.size         = v.size;
            node.shape.image.opacity      = v.opacity;
            node.color                    = Color{1.0f, 1.0f, 1.0f, v.opacity};
            node.world_transform.position = layer_transform.position + v.pos;
            node.world_transform.anchor   = {v.size.x * 0.5f, v.size.y * 0.5f, 0.0f};
        } else if constexpr (std::is_same_v<T, FakeExtrudedTextParams>) {
            node.name                     = std::pmr::string{"fake_extruded_text", res};
            node.shape.type               = ShapeType::FakeExtrudedText;
            auto& s                       = node.shape.fake_extruded_text;
            s.text                        = v.text;
            s.font_path                   = v.font_path;
            s.font_size                   = v.font_size;
            s.align                       = v.align;
            s.world_pos                   = layer_transform.position + v.pos;
            s.depth                       = v.depth;
            s.extrude_dir                 = v.extrude_dir;
            s.extrude_z_step              = v.extrude_z_step;
            s.front_color                 = v.front_color;
            s.side_color                  = v.side_color;
            s.side_fade                   = v.side_fade;
            s.highlight_opacity           = v.highlight_opacity;
            s.bevel_size                  = v.bevel_size;
            node.color                    = v.front_color;
            node.world_transform.position  = layer_transform.position + v.pos;
        }

        node.world_transform.scale    = layer_transform.scale;
        node.world_transform.rotation = layer_transform.rotation;
    }, vd);

    return node;
}

} // namespace

Scene LegacySceneAdapter::convert(const EvaluatedScene& evaluated,
                                   std::pmr::memory_resource* res) const {
    Scene scene(res);

    if (evaluated.camera) {
        scene.set_camera_2_5d(*evaluated.camera);
    }

    for (const auto& el : evaluated.layers) {
        if (!el.visible) continue;

        Layer layer(res);
        layer.name         = std::pmr::string{el.name, res};
        layer.transform    = el.world_transform;
        layer.transform.opacity = el.opacity;
        layer.is_3d        = el.is_3d;
        layer.depth_role   = el.depth_role;
        layer.blend_mode   = el.blend_mode;
        layer.effects      = el.resolved_effects;
        layer.visible      = true;

        for (const auto& vd : el.visuals) {
            layer.nodes.push_back(make_render_node(vd, el.world_transform, res));
        }

        scene.add_layer(std::move(layer));
    }

    return scene;
}

} // namespace chronon3d
```

- [ ] **Step 2: Write `src/runtime/timeline_evaluator.cpp`** (same body, updated include)

```cpp
#include <chronon3d/runtime/timeline_evaluator.hpp>
#include <chronon3d/math/quat.hpp>
#include <chronon3d/math/expression.hpp>
#include <variant>
#include <unordered_map>

namespace chronon3d {

namespace {

inline Quat euler_deg_to_quat(Vec3 euler_deg) {
    const Vec3 r = glm::radians(glm::vec3(euler_deg.x, euler_deg.y, euler_deg.z));
    return Quat(r);
}

inline f32 resolve_z(const LayerDesc& l, Vec3 evaluated_pos) {
    if (l.is_3d && l.depth_role != DepthRole::None) {
        return DepthRoleResolver::z_for(l.depth_role) + l.depth_offset;
    }
    return evaluated_pos.z;
}

inline EffectStack resolve_effects(const std::vector<EffectDesc>& descs) {
    EffectStack stack;
    for (const auto& d : descs) {
        std::visit([&stack](const auto& e) {
            using T = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<T, BlurEffectDesc>) {
                stack.push_back(EffectInstance{BlurParams{e.radius}});
            } else if constexpr (std::is_same_v<T, TintEffectDesc>) {
                stack.push_back(EffectInstance{TintParams{e.color, 1.0f}});
            } else if constexpr (std::is_same_v<T, BrightnessContrastEffectDesc>) {
                if (e.brightness != 0.0f)
                    stack.push_back(EffectInstance{BrightnessParams{e.brightness}});
                if (e.contrast != 1.0f)
                    stack.push_back(EffectInstance{ContrastParams{e.contrast}});
            }
        }, d);
    }
    return stack;
}

inline f32 eval_expr(const std::string& expr, double frame, double time, double w, double h, f32 fallback) {
    const std::unordered_map<std::string, double> vars{
        {"frame", frame}, {"time", time}, {"width", w}, {"height", h},
    };
    return static_cast<f32>(math::evaluate_expression(expr, vars, fallback));
}

} // namespace

EvaluatedScene TimelineEvaluator::evaluate(const SceneDescription& scene, Frame frame) const {
    EvaluatedScene result;
    result.frame  = frame;
    result.width  = scene.width;
    result.height = scene.height;

    for (const auto& ld : scene.layers) {
        if (!ld.time_range.contains(frame)) continue;

        EvaluatedLayer el;
        el.name       = ld.name;
        el.visible    = true;

        double time = static_cast<double>(frame) / (static_cast<double>(scene.frame_rate.numerator) / scene.frame_rate.denominator);

        if (ld.opacity.has_expression()) {
            el.opacity = eval_expr(ld.opacity.expression(), (double)frame, time, (double)scene.width, (double)scene.height, ld.opacity.value_at(frame));
        } else {
            el.opacity = ld.opacity.value_at(frame);
        }

        el.is_3d      = ld.is_3d;
        el.depth_role = ld.depth_role;
        el.blend_mode = ld.blend_mode;

        Vec3 pos = ld.position.value_at(frame);
        Vec3 rot = ld.rotation.value_at(frame);
        Vec3 scl = ld.scale.value_at(frame);

        el.resolved_z      = resolve_z(ld, pos);
        pos.z              = el.resolved_z;

        el.world_transform.position = pos;
        el.world_transform.rotation = euler_deg_to_quat(rot);
        el.world_transform.scale    = scl;
        el.world_transform.opacity  = el.opacity;

        el.visuals = ld.visuals;
        el.resolved_effects = resolve_effects(ld.effects);

        result.layers.push_back(std::move(el));
    }

    if (scene.camera && scene.camera->enabled) {
        Camera2_5D cam;
        cam.enabled            = true;
        cam.position           = scene.camera->position.value_at(frame);
        cam.point_of_interest  = scene.camera->point_of_interest;
        cam.point_of_interest_enabled = scene.camera->point_of_interest_enabled;

        double time = static_cast<double>(frame) / (static_cast<double>(scene.frame_rate.numerator) / scene.frame_rate.denominator);
        if (scene.camera->zoom.has_expression()) {
            cam.zoom = eval_expr(scene.camera->zoom.expression(), (double)frame, time, (double)scene.width, (double)scene.height, scene.camera->zoom.value_at(frame));
        } else {
            cam.zoom = scene.camera->zoom.value_at(frame);
        }
        result.camera = cam;
    }

    return result;
}

} // namespace chronon3d
```

- [ ] **Step 3: Write `src/runtime/pipeline.cpp`** (same body, updated include)

```cpp
#include <chronon3d/runtime/pipeline.hpp>
#include <taskflow/taskflow.hpp>
#include <chronon3d/core/profiling.hpp>

namespace chronon3d {

void RenderPipeline::run(Frame start, Frame end, std::function<void(RenderedFrame)> output_callback) {
    ZoneScoped;
    const i32 count = static_cast<i32>(end - start);
    if (count <= 0) return;

    std::vector<EvaluatedFrame> eval_frames;
    eval_frames.reserve(static_cast<size_t>(count));
    for (Frame f = start; f < end; ++f) {
        ZoneScopedN("EvaluateFrame");
        auto arena = std::make_shared<FrameArena>();
        auto scene = m_composition->evaluate(f, arena->resource());
        eval_frames.push_back({f, std::move(arena), std::move(scene)});
    }

    std::vector<RenderedFrame> frame_buffer(static_cast<size_t>(count));
    {
        tf::Executor executor;
        tf::Taskflow taskflow;
        for (i32 i = 0; i < count; ++i) {
            const size_t idx = static_cast<size_t>(i);
            taskflow.emplace([&, idx]() {
                ZoneScopedN("RenderFrame");
                auto& ef = eval_frames[idx];
                frame_buffer[idx] = RenderedFrame{
                    ef.frame,
                    m_renderer->render_scene(ef.scene, m_composition->camera,
                                             m_composition->width(), m_composition->height()),
                    ef.arena
                };
            });
        }
        executor.run(taskflow).wait();
    }

    for (auto& rf : frame_buffer) {
        ZoneScopedN("OutputFrame");
        output_callback(std::move(rf));
    }
}

} // namespace chronon3d
```

- [ ] **Step 4: Write `src/runtime/graph_executor.cpp`** (same body, updated include)

```cpp
#include <chronon3d/runtime/graph_executor.hpp>
#include <chronon3d/render_graph/graph_profiler.hpp>
#include <chronon3d/render_graph/render_graph_hashing.hpp>
#include <future>
#include <mutex>

namespace chronon3d::graph {

static std::mutex g_exec_mutex;

GraphExecutor::GraphExecutor() = default;

std::shared_ptr<Framebuffer> GraphExecutor::execute(
    RenderGraph& graph,
    GraphNodeId output,
    RenderGraphContext& ctx
) {
    m_temp.clear();
    m_resolved_key_digest.clear();
    return execute_node(graph, output, ctx);
}

std::shared_ptr<Framebuffer> GraphExecutor::execute_node(
    RenderGraph& graph,
    GraphNodeId id,
    RenderGraphContext& ctx
) {
    {
        std::lock_guard<std::mutex> lock(g_exec_mutex);
        if (auto it = m_temp.find(id); it != m_temp.end()) {
            return it->second;
        }
    }

    auto& node = graph.node(id);

    auto input_ids = graph.inputs(id);
    std::vector<std::shared_ptr<Framebuffer>> inputs(input_ids.size());

    if (!input_ids.empty()) {
        std::vector<std::future<void>> futures;
        futures.reserve(input_ids.size());
        for (size_t i = 0; i < input_ids.size(); ++i) {
            futures.emplace_back(std::async(std::launch::async, [&, i] {
                inputs[i] = execute_node(graph, input_ids[i], ctx);
            }));
        }
        for (auto& f : futures) {
            f.get();
        }
    }

    u64 input_hash = 0;
    for (size_t i = 0; i < input_ids.size(); ++i) {
        auto input_id = input_ids[i];

        std::lock_guard<std::mutex> lock(g_exec_mutex);
        auto digest_it = m_resolved_key_digest.find(input_id);
        u64 input_digest = (digest_it != m_resolved_key_digest.end())
            ? digest_it->second
            : graph.node(input_id).cache_key(ctx).digest();
        input_hash = hash_combine(input_hash, input_digest);
    }

    auto start_time = std::chrono::high_resolution_clock::now();
    auto key = node.cache_key(ctx);
    key.input_hash = input_hash;

    const bool use_cache = node.cacheable() && ctx.cache_enabled && ctx.node_cache;

    if (use_cache) {
        if (auto cached = ctx.node_cache->find(key)) {
            if (ctx.profiler) {
                ctx.profiler->record_node({
                    .name = node.name(),
                    .kind = node.kind(),
                    .execution_time_ms = 0.0,
                    .cache_hit = true,
                    .memory_bytes = cached->size_bytes()
                });
            }
            {
                std::lock_guard<std::mutex> lock(g_exec_mutex);
                m_resolved_key_digest[id] = key.digest();
                m_temp[id] = cached;
            }
            return cached;
        }
    }

    auto result = node.execute(ctx, inputs);
    auto end_time = std::chrono::high_resolution_clock::now();
    double duration = std::chrono::duration<double, std::milli>(end_time - start_time).count();

    if (ctx.profiler) {
        ctx.profiler->record_node({
            .name = node.name(),
            .kind = node.kind(),
            .execution_time_ms = duration,
            .cache_hit = false,
            .memory_bytes = result ? result->size_bytes() : 0
        });
    }

    {
        std::lock_guard<std::mutex> lock(g_exec_mutex);
        m_resolved_key_digest[id] = key.digest();

        if (use_cache && result) {
            ctx.node_cache->store(key, result);
        }

        m_temp[id] = result;
    }
    return result;
}

} // namespace chronon3d::graph
```

- [ ] **Step 5: Delete the 4 old source files and now-empty directories**

```
del src\effects\evaluation\legacy_scene_adapter.cpp
del src\effects\evaluation\timeline_evaluator.cpp
rmdir src\effects\evaluation
del src\core\pipeline.cpp
rmdir src\core
del src\render_graph\graph_executor.cpp
```

---

### Task 3 — Update all consumer files: include paths

Every file that included an old path gets the new path. One Edit per file.

**Files to modify:**

| File | Old include | New include |
|------|------------|-------------|
| `src/specscene/specscene.cpp` | `chronon3d/evaluation/legacy_scene_adapter.hpp` | `chronon3d/runtime/legacy_scene_adapter.hpp` |
| `src/specscene/specscene.cpp` | `chronon3d/evaluation/timeline_evaluator.hpp` | `chronon3d/runtime/timeline_evaluator.hpp` |
| `src/renderer/software/software_scene_pipeline.cpp` | `chronon3d/render_graph/graph_executor.hpp` | `chronon3d/runtime/graph_executor.hpp` |
| `include/chronon3d/render_graph.hpp` | `chronon3d/render_graph/graph_executor.hpp` | `chronon3d/runtime/graph_executor.hpp` |
| `include/chronon3d/renderer/software/software_renderer.hpp` | `chronon3d/render_graph/graph_executor.hpp` | `chronon3d/runtime/graph_executor.hpp` |
| `include/chronon3d/render_graph/nodes/precomp_node.hpp` | `chronon3d/render_graph/graph_executor.hpp` | `chronon3d/runtime/graph_executor.hpp` |
| `apps/chronon3d_cli/commands/command_proofs.cpp` | `chronon3d/core/pipeline.hpp` | `chronon3d/runtime/pipeline.hpp` |
| `tests/render_graph/test_graph_executor.cpp` | `chronon3d/render_graph/graph_executor.hpp` | `chronon3d/runtime/graph_executor.hpp` |
| `tests/evaluation/test_timeline_evaluator.cpp` | `chronon3d/evaluation/timeline_evaluator.hpp` | `chronon3d/runtime/timeline_evaluator.hpp` |
| `tests/evaluation/test_legacy_scene_adapter.cpp` | `chronon3d/evaluation/legacy_scene_adapter.hpp` | `chronon3d/runtime/legacy_scene_adapter.hpp` |
| `tests/evaluation/test_legacy_scene_adapter.cpp` | `chronon3d/evaluation/timeline_evaluator.hpp` | `chronon3d/runtime/timeline_evaluator.hpp` |
| `docs/RENDER_GRAPH.md` | `include/chronon3d/render_graph/graph_executor.hpp` | `include/chronon3d/runtime/graph_executor.hpp` |

- [ ] **Step 1: Update `src/specscene/specscene.cpp`**

Replace:
```cpp
#include <chronon3d/evaluation/legacy_scene_adapter.hpp>
#include <chronon3d/evaluation/timeline_evaluator.hpp>
```
With:
```cpp
#include <chronon3d/runtime/legacy_scene_adapter.hpp>
#include <chronon3d/runtime/timeline_evaluator.hpp>
```

- [ ] **Step 2: Update `src/renderer/software/software_scene_pipeline.cpp`**

Replace:
```cpp
#include <chronon3d/render_graph/graph_executor.hpp>
```
With:
```cpp
#include <chronon3d/runtime/graph_executor.hpp>
```

- [ ] **Step 3: Update `include/chronon3d/render_graph.hpp`**

Replace:
```cpp
#include <chronon3d/render_graph/graph_executor.hpp>
```
With:
```cpp
#include <chronon3d/runtime/graph_executor.hpp>
```

- [ ] **Step 4: Update `include/chronon3d/renderer/software/software_renderer.hpp`**

Replace:
```cpp
#include <chronon3d/render_graph/graph_executor.hpp>
```
With:
```cpp
#include <chronon3d/runtime/graph_executor.hpp>
```

- [ ] **Step 5: Update `include/chronon3d/render_graph/nodes/precomp_node.hpp`**

Replace:
```cpp
#include <chronon3d/render_graph/graph_executor.hpp>
```
With:
```cpp
#include <chronon3d/runtime/graph_executor.hpp>
```

- [ ] **Step 6: Update `apps/chronon3d_cli/commands/command_proofs.cpp`**

Replace:
```cpp
#include <chronon3d/core/pipeline.hpp>
```
With:
```cpp
#include <chronon3d/runtime/pipeline.hpp>
```

- [ ] **Step 7: Update `tests/render_graph/test_graph_executor.cpp`**

Replace:
```cpp
#include <chronon3d/render_graph/graph_executor.hpp>
```
With:
```cpp
#include <chronon3d/runtime/graph_executor.hpp>
```

- [ ] **Step 8: Update `tests/evaluation/test_timeline_evaluator.cpp`**

Replace:
```cpp
#include <chronon3d/evaluation/timeline_evaluator.hpp>
```
With:
```cpp
#include <chronon3d/runtime/timeline_evaluator.hpp>
```

- [ ] **Step 9: Update `tests/evaluation/test_legacy_scene_adapter.cpp`**

Replace:
```cpp
#include <chronon3d/evaluation/legacy_scene_adapter.hpp>
#include <chronon3d/evaluation/timeline_evaluator.hpp>
```
With:
```cpp
#include <chronon3d/runtime/legacy_scene_adapter.hpp>
#include <chronon3d/runtime/timeline_evaluator.hpp>
```

- [ ] **Step 10: Update `docs/RENDER_GRAPH.md`**

Replace the path reference `include/chronon3d/render_graph/graph_executor.hpp` with `include/chronon3d/runtime/graph_executor.hpp`.

---

### Task 4 — Update CMakeLists.txt for Round 1

**File:** `CMakeLists.txt`

- [ ] **Step 1: Delete `chronon3d_core` block (lines 84–87)**

Remove:
```cmake
# 2. Core Implementation
file(GLOB CHRONON3D_CORE_SOURCES CONFIGURE_DEPENDS "src/core/*.cpp")
add_library(chronon3d_core STATIC ${CHRONON3D_CORE_SOURCES})
target_link_libraries(chronon3d_core PUBLIC chronon3d Taskflow::Taskflow)
```

- [ ] **Step 2: Update `chronon3d_effects` to remove evaluation glob (lines 106–112)**

Replace:
```cmake
# 6. Effects
file(GLOB CHRONON3D_EFFECTS_SOURCES CONFIGURE_DEPENDS 
    "src/effects/*.cpp"
    "src/effects/evaluation/*.cpp"
)
add_library(chronon3d_effects STATIC ${CHRONON3D_EFFECTS_SOURCES})
target_link_libraries(chronon3d_effects PUBLIC chronon3d meshoptimizer::meshoptimizer)
```
With:
```cmake
# 6. Effects
file(GLOB CHRONON3D_EFFECTS_SOURCES CONFIGURE_DEPENDS "src/effects/*.cpp")
add_library(chronon3d_effects STATIC ${CHRONON3D_EFFECTS_SOURCES})
target_link_libraries(chronon3d_effects PUBLIC chronon3d meshoptimizer::meshoptimizer)
```

- [ ] **Step 3: Add new `chronon3d_runtime` target after `chronon3d_effects`**

Insert after the Effects block:
```cmake
# 6b. Runtime
file(GLOB CHRONON3D_RUNTIME_SOURCES CONFIGURE_DEPENDS "src/runtime/*.cpp")
add_library(chronon3d_runtime STATIC ${CHRONON3D_RUNTIME_SOURCES})
target_link_libraries(chronon3d_runtime PUBLIC
    chronon3d chronon3d_graph chronon3d_effects chronon3d_cache
    Taskflow::Taskflow
)
```

- [ ] **Step 4: Add `chronon3d_runtime` to `chronon3d_software` PUBLIC deps**

Replace:
```cmake
target_link_libraries(chronon3d_software 
    PUBLIC chronon3d chronon3d_graph chronon3d_cache chronon3d_effects
    PRIVATE spdlog::spdlog hwy::hwy fmt::fmt xxHash::xxhash tomlplusplus::tomlplusplus
)
```
With:
```cmake
target_link_libraries(chronon3d_software 
    PUBLIC chronon3d chronon3d_graph chronon3d_cache chronon3d_effects chronon3d_runtime
    PRIVATE spdlog::spdlog hwy::hwy fmt::fmt xxHash::xxhash tomlplusplus::tomlplusplus
)
```

- [ ] **Step 5: Rename `chronon3d_renderer` → `chronon3d_scene`, replace `chronon3d_core` dep with `chronon3d_runtime`**

Replace the Renderer Implementation block:
```cmake
# 8. Renderer Implementation (Scene, Layout)
file(GLOB CHRONON3D_RENDERER_SOURCES CONFIGURE_DEPENDS 
    "src/scene/*.cpp"
    "src/layout/*.cpp"
    "src/specscene/*.cpp"
)
add_library(chronon3d_renderer OBJECT ${CHRONON3D_RENDERER_SOURCES})
target_link_libraries(chronon3d_renderer
    PUBLIC chronon3d chronon3d_core chronon3d_graph chronon3d_software chronon3d_registry chronon3d_cache chronon3d_effects chronon3d_io
    PRIVATE spdlog::spdlog fmt::fmt tomlplusplus::tomlplusplus
)
```
With:
```cmake
# 8. Scene Assembly (Scene, Layout, SpecScene)
file(GLOB CHRONON3D_SCENE_SOURCES CONFIGURE_DEPENDS 
    "src/scene/*.cpp"
    "src/layout/*.cpp"
    "src/specscene/*.cpp"
)
add_library(chronon3d_scene OBJECT ${CHRONON3D_SCENE_SOURCES})
target_link_libraries(chronon3d_scene
    PUBLIC chronon3d chronon3d_runtime chronon3d_graph chronon3d_software chronon3d_registry chronon3d_cache chronon3d_effects chronon3d_io
    PRIVATE spdlog::spdlog fmt::fmt tomlplusplus::tomlplusplus
)
```

- [ ] **Step 6: Update Profiling block — `chronon3d_renderer` → `chronon3d_scene`**

Replace:
```cmake
    target_link_libraries(chronon3d_renderer PRIVATE Tracy::TracyClient)
    target_compile_definitions(chronon3d_renderer PRIVATE CHRONON_PROFILING TRACY_ON_DEMAND)
```
With:
```cmake
    target_link_libraries(chronon3d_scene PRIVATE Tracy::TracyClient)
    target_compile_definitions(chronon3d_scene PRIVATE CHRONON_PROFILING TRACY_ON_DEMAND)
```

- [ ] **Step 7: Update `chronon3d_pipeline` INTERFACE — replace `chronon3d_renderer`/`chronon3d_core` with `chronon3d_scene`/`chronon3d_runtime`**

Replace:
```cmake
add_library(chronon3d_pipeline INTERFACE)
target_link_libraries(chronon3d_pipeline INTERFACE 
    chronon3d_renderer chronon3d_core chronon3d_graph chronon3d_software chronon3d_registry chronon3d_cache 
    Taskflow::Taskflow concurrentqueue::concurrentqueue
)
```
With:
```cmake
add_library(chronon3d_pipeline INTERFACE)
target_link_libraries(chronon3d_pipeline INTERFACE 
    chronon3d_scene chronon3d_runtime chronon3d_graph chronon3d_software chronon3d_registry chronon3d_cache 
    Taskflow::Taskflow concurrentqueue::concurrentqueue
)
```

- [ ] **Step 8: Update `tests/CMakeLists.txt` — `chronon3d_renderer` → `chronon3d_scene`**

In `tests/CMakeLists.txt`, replace:
```cmake
        $<TARGET_OBJECTS:chronon3d_renderer>
```
With:
```cmake
        $<TARGET_OBJECTS:chronon3d_scene>
```

- [ ] **Step 9: Update `apps/chronon3d_cli/CMakeLists.txt` — `chronon3d_renderer` → `chronon3d_scene`**

In `apps/chronon3d_cli/CMakeLists.txt`, replace:
```cmake
        $<TARGET_OBJECTS:chronon3d_renderer>
```
With:
```cmake
        $<TARGET_OBJECTS:chronon3d_scene>
```

- [ ] **Step 10: Update ARCHITECTURE.md**

In `ARCHITECTURE.md`, replace the Current Topology section with:

```markdown
## Current Topology

- `include/chronon3d/` exposes the public API and domain model.
- `src/runtime/` contains the render pipeline, frame evaluation, and graph execution.
- `src/render_graph/` defines the render graph model and builder passes.
- `src/scene/` turns scene builders into renderable scene structures.
- `src/effects/` provides the effect registry and effect descriptors.
- `src/registry/` provides sampler, shape, and source registries.
- `src/cache/` provides frame and node caches.
- `src/io/` handles image output.
- `src/video/` contains video domain types (source descriptor, frame mapping).
- `src/renderer/` contains the software renderer backend and asset/text/video utilities.
- `apps/chronon3d_cli/` provides the command-line interface.
- `templates/` contains reusable scene templates and composition helpers.
- `tests/` validates the engine behavior.
```

---

### Task 5 — Build and commit Round 1

- [ ] **Step 1: Configure and build**

```
cmake --preset windows-release   (or your preset)
cmake --build build/... -j8
```

Expected: zero errors, zero warnings about missing files.

- [ ] **Step 2: Run tests**

```
ctest --test-dir build/... --output-on-failure
```

Expected: all previously passing tests still pass.

- [ ] **Step 3: Commit**

```bash
git add -A
git commit -m "refactor: isolate runtime layer — evaluation, pipeline, graph_executor -> src/runtime/

- Move src/effects/evaluation/ -> src/runtime/
- Move src/core/pipeline.cpp -> src/runtime/
- Move src/render_graph/graph_executor.cpp -> src/runtime/
- Move include/chronon3d/evaluation/ -> include/chronon3d/runtime/
- Move include/chronon3d/core/pipeline.hpp -> include/chronon3d/runtime/
- Move include/chronon3d/render_graph/graph_executor.hpp -> include/chronon3d/runtime/
- Delete chronon3d_core CMake target (now empty)
- Add chronon3d_runtime CMake target
- Rename chronon3d_renderer -> chronon3d_scene in CMake

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

## Round 2 — Backends Isolation

Files moving into `src/backends/` and `include/chronon3d/backends/`:

| From | To |
|------|----|
| `src/renderer/software/` (all) | `src/backends/software/` |
| `src/renderer/text/` | `src/backends/text/` |
| `src/renderer/assets/` | `src/backends/assets/` |
| `src/renderer/video/` | `src/backends/video/` |
| `src/io/` | `src/backends/image/` |
| `src/video/ffmpeg_encoder.cpp` | `src/backends/ffmpeg/` |
| `src/video/ffmpeg_frame_extractor.cpp` | `src/backends/ffmpeg/` |
| `src/video/video_frame_cache.cpp` | `src/backends/ffmpeg/` |
| `include/chronon3d/renderer/software/` (all) | `include/chronon3d/backends/software/` |
| `include/chronon3d/renderer/text/` | `include/chronon3d/backends/text/` |
| `include/chronon3d/renderer/assets/` | `include/chronon3d/backends/assets/` |
| `include/chronon3d/renderer/video/` | `include/chronon3d/backends/video/` |
| `include/chronon3d/io/` | `include/chronon3d/backends/image/` |
| `include/chronon3d/video/ffmpeg_encoder.hpp` | `include/chronon3d/backends/ffmpeg/` |
| `include/chronon3d/video/ffmpeg_frame_extractor.hpp` | `include/chronon3d/backends/ffmpeg/` |
| `include/chronon3d/video/video_frame_cache.hpp` | `include/chronon3d/backends/ffmpeg/` |
| `include/chronon3d/renderer/materials.hpp` | `include/chronon3d/scene/materials.hpp` |

**Does NOT move (domain types):**
- `src/video/video_source.cpp` → moves to `src/scene/video_source.cpp`
- `include/chronon3d/video/video_source.hpp` → stays
- `include/chronon3d/video/video_decoder.hpp` → stays

Directories deleted after Round 2: `src/renderer/`, `src/io/`, `src/video/` (empty after video_source.cpp moves), `include/chronon3d/renderer/`.

CMake: `chronon3d_software` → `chronon3d_backend_software`, `chronon3d_io` → `chronon3d_backend_image`, `chronon3d_video` → `chronon3d_backend_ffmpeg` (optional). `video_source.cpp` folds into `chronon3d_scene`.

---

### Task 6 — Write new backend headers

Write each header with updated internal includes. Only includes that referenced old `renderer/`, `io/`, or `video/` paths need updating; all other includes are unchanged.

**`include/chronon3d/backends/image/image_loader.hpp`** — copy from `include/chronon3d/io/image_loader.hpp` (no include changes needed)

**`include/chronon3d/backends/image/image_writer.hpp`** — copy from `include/chronon3d/io/image_writer.hpp` (no include changes needed)

**`include/chronon3d/backends/assets/font_cache.hpp`** — copy from `include/chronon3d/renderer/assets/font_cache.hpp` (no include changes needed)

**`include/chronon3d/backends/assets/image_cache.hpp`** — copy from `include/chronon3d/renderer/assets/image_cache.hpp` (no include changes needed)

**`include/chronon3d/backends/assets/image_renderer.hpp`** — copy from `include/chronon3d/renderer/assets/image_renderer.hpp`, update internal include:
- `chronon3d/renderer/assets/image_cache.hpp` → `chronon3d/backends/assets/image_cache.hpp`

**`include/chronon3d/backends/text/text_layout_engine.hpp`** — copy from `include/chronon3d/renderer/text/text_layout_engine.hpp` (no include changes)

**`include/chronon3d/backends/text/text_renderer.hpp`** — copy from `include/chronon3d/renderer/text/text_renderer.hpp`, update:
- `chronon3d/renderer/software/renderer.hpp` → `chronon3d/backends/software/renderer.hpp`
- `chronon3d/renderer/assets/font_cache.hpp` → `chronon3d/backends/assets/font_cache.hpp`

**`include/chronon3d/backends/video/video_frame_provider.hpp`** — copy from `include/chronon3d/renderer/video/video_frame_provider.hpp` (no include changes)

**`include/chronon3d/backends/software/renderer.hpp`** — copy from `include/chronon3d/renderer/software/renderer.hpp` (no include changes)

**`include/chronon3d/backends/software/render_settings.hpp`** — copy from `include/chronon3d/renderer/software/render_settings.hpp` (no include changes needed — read this file first to check)

**`include/chronon3d/backends/software/fake_extruded_text_renderer.hpp`** — copy from `include/chronon3d/renderer/software/fake_extruded_text_renderer.hpp`, update:
- `chronon3d/renderer/text/text_renderer.hpp` → `chronon3d/backends/text/text_renderer.hpp`

**`include/chronon3d/backends/software/software_renderer.hpp`** — copy from `include/chronon3d/renderer/software/software_renderer.hpp`, update:
- `chronon3d/renderer/software/renderer.hpp` → `chronon3d/backends/software/renderer.hpp`
- `chronon3d/renderer/text/text_renderer.hpp` → `chronon3d/backends/text/text_renderer.hpp`
- `chronon3d/renderer/assets/image_renderer.hpp` → `chronon3d/backends/assets/image_renderer.hpp`
- `chronon3d/renderer/software/render_settings.hpp` → `chronon3d/backends/software/render_settings.hpp`
- `chronon3d/renderer/software/fake_extruded_text_renderer.hpp` → `chronon3d/backends/software/fake_extruded_text_renderer.hpp`

**`include/chronon3d/backends/ffmpeg/ffmpeg_encoder.hpp`** — copy from `include/chronon3d/video/ffmpeg_encoder.hpp` (no include changes)

**`include/chronon3d/backends/ffmpeg/video_frame_cache.hpp`** — copy from `include/chronon3d/video/video_frame_cache.hpp` (no include changes)

**`include/chronon3d/backends/ffmpeg/ffmpeg_frame_extractor.hpp`** — copy from `include/chronon3d/video/ffmpeg_frame_extractor.hpp`, update:
- `chronon3d/video/video_decoder.hpp` → stays (video_decoder.hpp does NOT move)
- `chronon3d/video/video_frame_cache.hpp` → `chronon3d/backends/ffmpeg/video_frame_cache.hpp`

**`include/chronon3d/scene/materials.hpp`** — copy from `include/chronon3d/renderer/materials.hpp` (no consumers to update)

- [ ] **Step 1: Read these source headers before writing to capture exact content**

Read each of these (they may have includes we haven't seen):
- `include/chronon3d/renderer/assets/font_cache.hpp`
- `include/chronon3d/renderer/assets/image_cache.hpp`
- `include/chronon3d/renderer/assets/image_renderer.hpp`
- `include/chronon3d/renderer/text/text_layout_engine.hpp`
- `include/chronon3d/renderer/text/text_renderer.hpp`
- `include/chronon3d/renderer/video/video_frame_provider.hpp`
- `include/chronon3d/renderer/software/render_settings.hpp`
- `include/chronon3d/renderer/software/fake_extruded_text_renderer.hpp`
- `include/chronon3d/renderer/materials.hpp`
- `include/chronon3d/video/ffmpeg_encoder.hpp`
- `include/chronon3d/video/video_frame_cache.hpp`
- `include/chronon3d/video/ffmpeg_frame_extractor.hpp`
- `include/chronon3d/io/image_loader.hpp`
- `include/chronon3d/io/image_writer.hpp`

Then write each new header to its destination with the appropriate include-path substitutions.

- [ ] **Step 2: Delete all old headers once new ones are written**

```
rmdir /s /q include\chronon3d\renderer
del include\chronon3d\video\ffmpeg_encoder.hpp
del include\chronon3d\video\ffmpeg_frame_extractor.hpp
del include\chronon3d\video\video_frame_cache.hpp
del include\chronon3d\io\image_loader.hpp
del include\chronon3d\io\image_writer.hpp
```

---

### Task 7 — Move backend source files

For each source file, read it, write it to the new path with updated includes, delete the original. The logic is unchanged; only `#include` paths change from old `renderer/`, `io/`, `video/` prefixes to `backends/`.

- [ ] **Step 1: Move all `src/renderer/software/` sources to `src/backends/software/`**

Files (preserve subdirectory structure):
```
src/renderer/software/software_renderer.cpp        → src/backends/software/software_renderer.cpp
src/renderer/software/software_frame_pipeline.cpp  → src/backends/software/software_frame_pipeline.cpp
src/renderer/software/software_scene_pipeline.cpp  → src/backends/software/software_scene_pipeline.cpp
src/renderer/software/software_node_dispatch.cpp   → src/backends/software/software_node_dispatch.cpp
src/renderer/software/rasterizers/line_rasterizer.cpp    → src/backends/software/rasterizers/line_rasterizer.cpp
src/renderer/software/rasterizers/scanline_rasterizer.cpp → src/backends/software/rasterizers/scanline_rasterizer.cpp
src/renderer/software/rasterizers/shape_rasterizer.cpp   → src/backends/software/rasterizers/shape_rasterizer.cpp
src/renderer/software/specialized/fake_box3d_renderer.cpp  → src/backends/software/specialized/fake_box3d_renderer.cpp
src/renderer/software/specialized/fake_extruded_text_renderer.cpp → src/backends/software/specialized/fake_extruded_text_renderer.cpp
src/renderer/software/specialized/glass_panel_renderer.cpp → src/backends/software/specialized/glass_panel_renderer.cpp
src/renderer/software/specialized/grid_plane_renderer.cpp  → src/backends/software/specialized/grid_plane_renderer.cpp
src/renderer/software/specialized/mesh_renderer.cpp        → src/backends/software/specialized/mesh_renderer.cpp
src/renderer/software/utils/projection_utils.cpp      → src/backends/software/utils/projection_utils.cpp
src/renderer/software/utils/render_effects_processor.cpp → src/backends/software/utils/render_effects_processor.cpp
src/renderer/software/utils/render_hash_utils.cpp     → src/backends/software/utils/render_hash_utils.cpp
```

For each file: read, update any `chronon3d/renderer/` includes to `chronon3d/backends/`, write to new path.

Also move `.hpp` files that live alongside `.cpp` (primitive_renderer.hpp, etc. in `src/renderer/software/`):
```
src/renderer/software/primitive_renderer.hpp → src/backends/software/primitive_renderer.hpp
src/renderer/software/rasterizers/line_rasterizer.hpp    → src/backends/software/rasterizers/
src/renderer/software/rasterizers/scanline_rasterizer.hpp → src/backends/software/rasterizers/
src/renderer/software/rasterizers/shape_rasterizer.hpp   → src/backends/software/rasterizers/
src/renderer/software/specialized/fake_box3d_renderer.hpp  → src/backends/software/specialized/
src/renderer/software/specialized/glass_panel_renderer.hpp → src/backends/software/specialized/
src/renderer/software/specialized/mesh_renderer.hpp        → src/backends/software/specialized/
src/renderer/software/utils/projection_utils.hpp      → src/backends/software/utils/
src/renderer/software/utils/render_effects_processor.hpp → src/backends/software/utils/
src/renderer/software/utils/render_hash_utils.hpp     → src/backends/software/utils/
```

- [ ] **Step 2: Move `src/renderer/text/`, `src/renderer/assets/`, `src/renderer/video/`**

```
src/renderer/text/text_renderer.cpp        → src/backends/text/text_renderer.cpp
src/renderer/text/text_layout_engine.cpp   → src/backends/text/text_layout_engine.cpp
src/renderer/assets/font_cache.cpp         → src/backends/assets/font_cache.cpp
src/renderer/assets/image_cache.cpp        → src/backends/assets/image_cache.cpp
src/renderer/assets/image_renderer.cpp     → src/backends/assets/image_renderer.cpp
src/renderer/video/video_frame_provider.cpp → src/backends/video/video_frame_provider.cpp
```

Update includes in each moved file: `chronon3d/renderer/` → `chronon3d/backends/`.

- [ ] **Step 3: Move `src/io/` sources**

```
src/io/image_loader.cpp → src/backends/image/image_loader.cpp
src/io/image_writer.cpp → src/backends/image/image_writer.cpp
```

Update includes: `chronon3d/io/` → `chronon3d/backends/image/`.

- [ ] **Step 4: Move FFmpeg sources from `src/video/`**

```
src/video/ffmpeg_encoder.cpp        → src/backends/ffmpeg/ffmpeg_encoder.cpp
src/video/ffmpeg_frame_extractor.cpp → src/backends/ffmpeg/ffmpeg_frame_extractor.cpp
src/video/video_frame_cache.cpp     → src/backends/ffmpeg/video_frame_cache.cpp
```

Update includes: `chronon3d/video/ffmpeg*` → `chronon3d/backends/ffmpeg/`, `chronon3d/io/` → `chronon3d/backends/image/`.

- [ ] **Step 5: Move `src/video/video_source.cpp` → `src/scene/video_source.cpp`**

No include changes needed (it includes `chronon3d/video/video_source.hpp` which stays).

- [ ] **Step 6: Delete emptied source directories**

```
rmdir /s /q src\renderer
rmdir /s /q src\io
rmdir /s /q src\video
```

---

### Task 8 — Update all consumer files: include paths (Round 2)

Update all files that include any of the moved headers.

**Systematic replacements** (apply to every file that contains these strings):

| Old include | New include |
|------------|-------------|
| `chronon3d/renderer/software/software_renderer.hpp` | `chronon3d/backends/software/software_renderer.hpp` |
| `chronon3d/renderer/software/renderer.hpp` | `chronon3d/backends/software/renderer.hpp` |
| `chronon3d/renderer/software/render_settings.hpp` | `chronon3d/backends/software/render_settings.hpp` |
| `chronon3d/renderer/software/fake_extruded_text_renderer.hpp` | `chronon3d/backends/software/fake_extruded_text_renderer.hpp` |
| `chronon3d/renderer/text/text_renderer.hpp` | `chronon3d/backends/text/text_renderer.hpp` |
| `chronon3d/renderer/text/text_layout_engine.hpp` | `chronon3d/backends/text/text_layout_engine.hpp` |
| `chronon3d/renderer/assets/font_cache.hpp` | `chronon3d/backends/assets/font_cache.hpp` |
| `chronon3d/renderer/assets/image_cache.hpp` | `chronon3d/backends/assets/image_cache.hpp` |
| `chronon3d/renderer/assets/image_renderer.hpp` | `chronon3d/backends/assets/image_renderer.hpp` |
| `chronon3d/renderer/video/video_frame_provider.hpp` | `chronon3d/backends/video/video_frame_provider.hpp` |
| `chronon3d/io/image_loader.hpp` | `chronon3d/backends/image/image_loader.hpp` |
| `chronon3d/io/image_writer.hpp` | `chronon3d/backends/image/image_writer.hpp` |
| `chronon3d/video/ffmpeg_encoder.hpp` | `chronon3d/backends/ffmpeg/ffmpeg_encoder.hpp` |
| `chronon3d/video/ffmpeg_frame_extractor.hpp` | `chronon3d/backends/ffmpeg/ffmpeg_frame_extractor.hpp` |
| `chronon3d/video/video_frame_cache.hpp` | `chronon3d/backends/ffmpeg/video_frame_cache.hpp` |

Files confirmed to need these changes (from grep output in Round 1 research):

`include/chronon3d/rendering.hpp` — update both renderer lines  
`include/chronon3d/runtime/pipeline.hpp` — update `renderer/software/renderer.hpp` line  
`include/chronon3d/scene/builders/layer_builder.hpp` — update `renderer/video/video_frame_provider.hpp`  
`apps/chronon3d_cli/utils/cli_render_utils.hpp` — update both renderer lines  
`apps/chronon3d_cli/commands/command_video.cpp` — update renderer + video lines  
`apps/chronon3d_cli/commands/command_graph.cpp` — update renderer line  
`apps/chronon3d_cli/commands/command_bench.cpp` — update renderer line  
`tests/test_utils.hpp` — update io line  
`tests/io/test_png_validity.cpp` — update io line  
`tests/io/test_image_writer.cpp` — update io line  
`tests/renderer/cache/test_cache.cpp` — update assets lines  
`tests/renderer/media/test_video_frame_provider.cpp` — update video line  
`tests/renderer/text/test_text_layout.cpp` — update text line  
`tests/renderer/media/test_image_rendering.cpp` — update renderer + assets lines  
All `tests/renderer/basics/*.cpp`, `tests/renderer/camera/*.cpp`, `tests/renderer/effects/*.cpp`, `tests/renderer/perf/*.cpp`, `tests/renderer/text/test_text_rendering.cpp` — update `software_renderer.hpp` line  
`tests/render_graph/test_precompositions.cpp`, `test_parallel_determinism.cpp`, `test_modular_features.cpp`, `test_camera_2_5d_graph.cpp`, `test_camera25d_graph.cpp` — update software_renderer line  
`tests/scene/test_circle_primitive.cpp`, `test_line_primitive.cpp` — update software_renderer line  
`apps/chronon3d_cli/commands/command_render.cpp` — update io line  
`docs/PlaybookElonMuskProgramming.md` — update path references  
`docs/superpowers/specs/2026-05-14-extruded-text-renderer-correctness-design.md` — update path

- [ ] **Step 1: Run a grep to find any remaining consumer files not in the list above**

```
grep -r "chronon3d/renderer/" include src tests apps --include="*.hpp" --include="*.cpp" -l
grep -r "chronon3d/io/" include src tests apps --include="*.hpp" --include="*.cpp" -l
grep -r "chronon3d/video/ffmpeg" include src tests apps --include="*.hpp" --include="*.cpp" -l
```

Update every file in the output.

- [ ] **Step 2: Apply all include substitutions**

For each file identified, use Edit to replace old include paths with new ones.

---

### Task 9 — Update CMakeLists.txt for Round 2

- [ ] **Step 1: Rename `chronon3d_software` → `chronon3d_backend_software`, update source glob**

Replace:
```cmake
# 7. Software Renderer
file(GLOB CHRONON3D_SOFTWARE_SOURCES CONFIGURE_DEPENDS 
    "src/renderer/software/*.cpp"
    "src/renderer/software/rasterizers/*.cpp"
    "src/renderer/software/specialized/*.cpp"
    "src/renderer/software/utils/*.cpp"
    "src/renderer/text/*.cpp"
    "src/renderer/assets/*.cpp"
    "src/renderer/video/*.cpp"
)
add_library(chronon3d_software OBJECT ${CHRONON3D_SOFTWARE_SOURCES})
target_link_libraries(chronon3d_software 
    PUBLIC chronon3d chronon3d_graph chronon3d_cache chronon3d_effects chronon3d_runtime
    PRIVATE spdlog::spdlog hwy::hwy fmt::fmt xxHash::xxhash tomlplusplus::tomlplusplus
)
target_include_directories(chronon3d_software PRIVATE ${Stb_INCLUDE_DIR})
```
With:
```cmake
# 7. Backend — Software Renderer
file(GLOB CHRONON3D_BACKEND_SOFTWARE_SOURCES CONFIGURE_DEPENDS 
    "src/backends/software/*.cpp"
    "src/backends/software/rasterizers/*.cpp"
    "src/backends/software/specialized/*.cpp"
    "src/backends/software/utils/*.cpp"
    "src/backends/text/*.cpp"
    "src/backends/assets/*.cpp"
    "src/backends/video/*.cpp"
)
add_library(chronon3d_backend_software OBJECT ${CHRONON3D_BACKEND_SOFTWARE_SOURCES})
target_link_libraries(chronon3d_backend_software 
    PUBLIC chronon3d chronon3d_graph chronon3d_cache chronon3d_effects chronon3d_runtime
    PRIVATE spdlog::spdlog hwy::hwy fmt::fmt xxHash::xxhash tomlplusplus::tomlplusplus
)
target_include_directories(chronon3d_backend_software PRIVATE ${Stb_INCLUDE_DIR})
```

- [ ] **Step 2: Update `chronon3d_scene` to reference `chronon3d_backend_software` and add `src/scene/video_source.cpp`**

Replace in `chronon3d_scene` block:
```cmake
file(GLOB CHRONON3D_SCENE_SOURCES CONFIGURE_DEPENDS 
    "src/scene/*.cpp"
    "src/layout/*.cpp"
    "src/specscene/*.cpp"
)
add_library(chronon3d_scene OBJECT ${CHRONON3D_SCENE_SOURCES})
target_link_libraries(chronon3d_scene
    PUBLIC chronon3d chronon3d_runtime chronon3d_graph chronon3d_software chronon3d_registry chronon3d_cache chronon3d_effects chronon3d_io
    PRIVATE spdlog::spdlog fmt::fmt tomlplusplus::tomlplusplus
)
```
With:
```cmake
file(GLOB CHRONON3D_SCENE_SOURCES CONFIGURE_DEPENDS 
    "src/scene/*.cpp"
    "src/layout/*.cpp"
    "src/specscene/*.cpp"
)
add_library(chronon3d_scene OBJECT ${CHRONON3D_SCENE_SOURCES})
target_link_libraries(chronon3d_scene
    PUBLIC chronon3d chronon3d_runtime chronon3d_graph chronon3d_backend_software chronon3d_registry chronon3d_cache chronon3d_effects chronon3d_backend_image
    PRIVATE spdlog::spdlog fmt::fmt tomlplusplus::tomlplusplus
)
```

- [ ] **Step 3: Rename `chronon3d_io` → `chronon3d_backend_image`, update glob**

Replace:
```cmake
# 9. IO
file(GLOB CHRONON3D_IO_SOURCES CONFIGURE_DEPENDS "src/io/*.cpp")
add_library(chronon3d_io STATIC ${CHRONON3D_IO_SOURCES})
target_link_libraries(chronon3d_io PUBLIC chronon3d)
target_include_directories(chronon3d_io PRIVATE ${Stb_INCLUDE_DIR})
```
With:
```cmake
# 9. Backend — Image IO
file(GLOB CHRONON3D_BACKEND_IMAGE_SOURCES CONFIGURE_DEPENDS "src/backends/image/*.cpp")
add_library(chronon3d_backend_image STATIC ${CHRONON3D_BACKEND_IMAGE_SOURCES})
target_link_libraries(chronon3d_backend_image PUBLIC chronon3d)
target_include_directories(chronon3d_backend_image PRIVATE ${Stb_INCLUDE_DIR})
```

- [ ] **Step 4: Rename `chronon3d_video` → `chronon3d_backend_ffmpeg`, update glob and deps**

Replace the Video block:
```cmake
if(CHRONON3D_ENABLE_VIDEO)
    file(GLOB CHRONON3D_VIDEO_SOURCES CONFIGURE_DEPENDS "src/video/*.cpp")
    add_library(chronon3d_video STATIC ${CHRONON3D_VIDEO_SOURCES})
    target_link_libraries(chronon3d_video PUBLIC chronon3d chronon3d_io chronon3d_ffmpeg xxHash::xxhash fmt::fmt spdlog::spdlog)
    target_compile_definitions(chronon3d_video PUBLIC CHRONON_WITH_VIDEO)
    target_link_libraries(chronon3d_renderer PUBLIC chronon3d_video)
    target_compile_definitions(chronon3d_renderer PUBLIC CHRONON_WITH_VIDEO)
endif()
```
With:
```cmake
if(CHRONON3D_ENABLE_VIDEO)
    file(GLOB CHRONON3D_BACKEND_FFMPEG_SOURCES CONFIGURE_DEPENDS "src/backends/ffmpeg/*.cpp")
    add_library(chronon3d_backend_ffmpeg STATIC ${CHRONON3D_BACKEND_FFMPEG_SOURCES})
    target_link_libraries(chronon3d_backend_ffmpeg PUBLIC chronon3d chronon3d_backend_image chronon3d_ffmpeg xxHash::xxhash fmt::fmt spdlog::spdlog)
    target_compile_definitions(chronon3d_backend_ffmpeg PUBLIC CHRONON_WITH_VIDEO)
    target_link_libraries(chronon3d_backend_software PUBLIC chronon3d_backend_ffmpeg)
    target_compile_definitions(chronon3d_backend_software PUBLIC CHRONON_WITH_VIDEO)
endif()
```

- [ ] **Step 5: Update `chronon3d_pipeline` to use new names**

Replace:
```cmake
target_link_libraries(chronon3d_pipeline INTERFACE 
    chronon3d_scene chronon3d_runtime chronon3d_graph chronon3d_software chronon3d_registry chronon3d_cache 
    Taskflow::Taskflow concurrentqueue::concurrentqueue
)
```
With:
```cmake
target_link_libraries(chronon3d_pipeline INTERFACE 
    chronon3d_scene chronon3d_runtime chronon3d_graph chronon3d_backend_software chronon3d_registry chronon3d_cache 
    Taskflow::Taskflow concurrentqueue::concurrentqueue
)
```

- [ ] **Step 6: Update `tests/CMakeLists.txt` — rename object lib references**

Replace:
```cmake
        $<TARGET_OBJECTS:chronon3d_graph>
        $<TARGET_OBJECTS:chronon3d_software>
        $<TARGET_OBJECTS:chronon3d_scene>
```
With:
```cmake
        $<TARGET_OBJECTS:chronon3d_graph>
        $<TARGET_OBJECTS:chronon3d_backend_software>
        $<TARGET_OBJECTS:chronon3d_scene>
```

Also replace `chronon3d_io` with `chronon3d_backend_image` in the PRIVATE link list.

- [ ] **Step 7: Update `apps/chronon3d_cli/CMakeLists.txt` — rename references**

Replace:
```cmake
        $<TARGET_OBJECTS:chronon3d_graph>
        $<TARGET_OBJECTS:chronon3d_software>
        $<TARGET_OBJECTS:chronon3d_scene>
```
With:
```cmake
        $<TARGET_OBJECTS:chronon3d_graph>
        $<TARGET_OBJECTS:chronon3d_backend_software>
        $<TARGET_OBJECTS:chronon3d_scene>
```

Also replace `chronon3d_io` with `chronon3d_backend_image`.

- [ ] **Step 8: Update ARCHITECTURE.md final topology**

Replace the Current Topology section with the final state:

```markdown
## Current Topology

- `include/chronon3d/` — public API: domain types, contracts, animation, math, scene model.
- `src/runtime/` — render pipeline, frame evaluation, graph execution.
- `src/render_graph/` — render graph model and builder passes.
- `src/scene/` — scene assembly, layer builders, layout, SpecScene.
- `src/effects/` — effect registry and effect descriptors.
- `src/registry/` — sampler, shape, and source registries.
- `src/cache/` — frame and node caches.
- `src/backends/software/` — CPU rasterizer (shapes, text, effects, compositing).
- `src/backends/text/` — text layout engine and text renderer.
- `src/backends/assets/` — image and font caches, image renderer.
- `src/backends/video/` — video frame provider.
- `src/backends/image/` — PNG/JPEG image IO.
- `src/backends/ffmpeg/` — FFmpeg encoder and frame extractor (optional, CHRONON_WITH_VIDEO).
- `apps/chronon3d_cli/` — CLI: parses human intent, calls engine APIs.
- `templates/` — reusable scene templates.
- `tests/` — validates engine behavior.
```

---

### Task 10 — Build and commit Round 2

- [ ] **Step 1: Configure and build**

```
cmake --preset windows-release
cmake --build build/... -j8
```

Expected: zero errors.

- [ ] **Step 2: Run tests**

```
ctest --test-dir build/... --output-on-failure
```

Expected: all tests pass.

- [ ] **Step 3: Commit**

```bash
git add -A
git commit -m "refactor: isolate backend layer — software, IO, FFmpeg -> src/backends/

- Move src/renderer/software/ -> src/backends/software/
- Move src/renderer/text/ -> src/backends/text/
- Move src/renderer/assets/ -> src/backends/assets/
- Move src/renderer/video/ -> src/backends/video/
- Move src/io/ -> src/backends/image/
- Move src/video/ffmpeg*.cpp + video_frame_cache.cpp -> src/backends/ffmpeg/
- Move src/video/video_source.cpp -> src/scene/
- Move include/chronon3d/renderer/ -> include/chronon3d/backends/
- Move include/chronon3d/io/ -> include/chronon3d/backends/image/
- Move include/chronon3d/video/ffmpeg*.hpp -> include/chronon3d/backends/ffmpeg/
- Rename CMake targets: chronon3d_software->chronon3d_backend_software, chronon3d_io->chronon3d_backend_image, chronon3d_video->chronon3d_backend_ffmpeg
- Delete src/renderer/, src/io/, src/video/, include/chronon3d/renderer/

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```
