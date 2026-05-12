#pragma once

#include <chronon3d/timeline/layer.hpp>
#include <chronon3d/scene/camera.hpp>
#include <vector>
#include <memory>

namespace chronon3d {

class Composition {
public:
    Composition(std::string name, i32 width, i32 height, FrameRate fps)
        : m_name(std::move(name)), m_width(width), m_height(height), m_frame_rate(fps) {}

    template <typename T = Layer, typename... Args>
    T& add_layer(Args&&... args) {
        auto layer = std::make_unique<T>(std::forward<Args>(args)...);
        T& ref = *layer;
        m_layers.push_back(std::move(layer));
        return ref;
    }

    [[nodiscard]] const std::string& name() const { return m_name; }
    [[nodiscard]] i32 width() const { return m_width; }
    [[nodiscard]] i32 height() const { return m_height; }
    [[nodiscard]] FrameRate frame_rate() const { return m_frame_rate; }
    
    [[nodiscard]] const std::vector<std::unique_ptr<Layer>>& layers() const { return m_layers; }

    Frame duration{0};
    Camera camera;

private:
    std::string m_name;
    i32 m_width;
    i32 m_height;
    FrameRate m_frame_rate;
    std::vector<std::unique_ptr<Layer>> m_layers;
};

} // namespace chronon3d
