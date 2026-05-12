#pragma once

#include <chronon3d/core/time.hpp>
#include <chronon3d/animation/animated_transform.hpp>
#include <string>

namespace chronon3d {

enum class LayerType {
    Null,
    Shape,
    Mesh,
    Camera,
    Light
};

class Layer {
public:
    Layer(std::string name, LayerType type) 
        : m_name(std::move(name)), m_type(type) {}
    
    virtual ~Layer() = default;

    [[nodiscard]] const std::string& name() const { return m_name; }
    [[nodiscard]] LayerType type() const { return m_type; }

    [[nodiscard]] bool is_active(Frame frame) const {
        return range.contains(frame) && visible;
    }

    // Properties
    TimeRange range{0, 0};
    AnimatedTransform transform;
    AnimatedValue<f32> opacity{1.0f};
    bool visible{true};

private:
    std::string m_name;
    LayerType m_type;
};

} // namespace chronon3d
