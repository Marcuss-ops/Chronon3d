#pragma once

#include <chronon3d/core/serialization.hpp>
#include <fstream>

namespace chronon3d {

class SceneLoader {
public:
    static std::shared_ptr<const Composition> load_from_file(const std::string& path) {
        std::ifstream file(path);
        if (!file.is_open()) throw std::runtime_error("Could not open file: " + path);
        
        nlohmann::json j;
        file >> j;
        return load_from_json(j);
    }

    static std::shared_ptr<const Composition> load_from_json(const nlohmann::json& j) {
        Composition::Builder builder;
        
        if (j.contains("name")) builder.name(j["name"]);
        if (j.contains("width") && j.contains("height")) {
            builder.size(j["width"], j["height"]);
        }
        if (j.contains("duration")) builder.duration(Frame{j["duration"]});
        
        if (j.contains("layers")) {
            for (const auto& lj : j["layers"]) {
                std::string name = lj.value("name", "Layer");
                LayerType type = LayerType::Null; // Simplified mapping
                
                auto& layer = builder.add_layer(name, type);
                if (lj.contains("range")) {
                    layer.range.start.index = lj["range"][0];
                    layer.range.end.index = lj["range"][1];
                }
                
                if (lj.contains("position")) {
                    load_animated(lj["position"], layer.transform.position);
                }
            }
        }
        
        return builder.build();
    }

private:
    template <typename T>
    static void load_animated(const nlohmann::json& j, AnimatedValue<T>& target) {
        if (j.is_object() && j.contains("keyframes")) {
            for (const auto& kj : j["keyframes"]) {
                Keyframe<T> k;
                from_json(kj, k);
                target.add_keyframe(k.frame, k.value);
            }
        } else {
            T val;
            from_json(j, val);
            target.set(val);
        }
    }
};

} // namespace chronon3d
