#pragma once

#include <chronon3d/timeline/layer.hpp>
#include <chronon3d/scene/camera.hpp>
#include <chronon3d/scene/scene.hpp>
#include <vector>
#include <memory>
#include <string>

namespace chronon3d {

struct CompositionSpec {
    std::string name{"Untitled"};
    i32 width{1920};
    i32 height{1080};
    FrameRate frame_rate{30, 1};
    Frame duration{0};
};

class Composition {
public:
    class Builder {
    public:
        Builder& name(std::string n) { m_spec.name = std::move(n); return *this; }
        Builder& size(i32 w, i32 h) { m_spec.width = w; m_spec.height = h; return *this; }
        Builder& fps(FrameRate fps) { m_spec.frame_rate = fps; return *this; }
        Builder& duration(Frame d) { m_spec.duration = d; return *this; }

        template <typename T = Layer, typename... Args>
        T& add_layer(Args&&... args) {
            auto layer = std::make_unique<T>(std::forward<Args>(args)...);
            T& ref = *layer;
            m_layers.push_back(std::move(layer));
            return ref;
        }

        [[nodiscard]] std::shared_ptr<const Composition> build() {
            return std::shared_ptr<const Composition>(new Composition(m_spec, std::move(m_layers)));
        }

    private:
        CompositionSpec m_spec;
        std::vector<std::unique_ptr<Layer>> m_layers;
    };

    [[nodiscard]] i32 width() const { return m_spec.width; }
    [[nodiscard]] i32 height() const { return m_spec.height; }
    [[nodiscard]] FrameRate frame_rate() const { return m_spec.frame_rate; }
    [[nodiscard]] Frame duration() const { return m_spec.duration; }
    [[nodiscard]] const std::string& name() const { return m_spec.name; }

    [[nodiscard]] const std::vector<std::unique_ptr<Layer>>& layers() const { return m_layers; }

    [[nodiscard]] Scene evaluate(Frame frame) const {
        Scene scene;
        for (const auto& layer : m_layers) {
            if (!layer->is_active(frame)) continue;

            RenderNode node;
            node.name = layer->name();
            node.world_transform = layer->transform.evaluate(frame);
            node.color = Color::white() * layer->opacity.evaluate(frame);
            
            // In a real impl, we'd handle MeshLayer etc.
            // For now, this is a placeholder for the logic.
            scene.add_node(std::move(node));
        }
        return scene;
    }

    Camera camera;

private:
    Composition(CompositionSpec spec, std::vector<std::unique_ptr<Layer>> layers)
        : m_spec(std::move(spec)), m_layers(std::move(layers)) {}

    CompositionSpec m_spec;
    std::vector<std::unique_ptr<Layer>> m_layers;
};

} // namespace chronon3d
