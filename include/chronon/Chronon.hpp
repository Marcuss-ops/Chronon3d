#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <initializer_list>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#if defined(CHRONON_ENABLE_TRACY) && __has_include(<tracy/Tracy.hpp>)
#include <tracy/Tracy.hpp>
#define CHRONON_ZONE_SCOPED ZoneScoped
#else
#define CHRONON_ZONE_SCOPED do {} while (0)
#endif

#if defined(CHRONON_ENABLE_MIMALLOC) && __has_include(<mimalloc-new-delete.h>)
#include <mimalloc-new-delete.h>
#endif

#if defined(CHRONON_ENABLE_HWY) && __has_include(<hwy/highway.h>)
#include <hwy/highway.h>
#endif

namespace ch {

using Seconds = std::chrono::duration<double>;

struct AssetRef {
    std::string path;
    bool embedded = false;

    operator std::string() const {
        return path;
    }
};

inline AssetRef asset(std::string_view name) {
#if defined(_DEBUG) || !defined(NDEBUG)
    return {std::string(name), true};
#else
    return {std::string(name), false};
#endif
}

#define ASSET(name) ::ch::asset(name)

struct Vec2 {
    double x = 0.0;
    double y = 0.0;
};

struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct Color {
    double r = 1.0;
    double g = 1.0;
    double b = 1.0;
    double a = 1.0;
};

inline double clamp01(double value) {
    return std::clamp(value, 0.0, 1.0);
}

inline double lerp(double from, double to, double t) {
    return from + (to - from) * t;
}

inline Vec2 lerp(Vec2 from, Vec2 to, double t) {
    return {lerp(from.x, to.x, t), lerp(from.y, to.y, t)};
}

inline Vec3 lerp(Vec3 from, Vec3 to, double t) {
    return {lerp(from.x, to.x, t), lerp(from.y, to.y, t), lerp(from.z, to.z, t)};
}

inline double easeLinear(double t) {
    return clamp01(t);
}

inline double easeOutCubic(double t) {
    t = clamp01(t);
    return 1.0 - std::pow(1.0 - t, 3.0);
}

inline double easeInOutCubic(double t) {
    t = clamp01(t);
    if (t < 0.5) {
        return 4.0 * t * t * t;
    }
    const double u = -2.0 * t + 2.0;
    return 1.0 - (u * u * u) / 2.0;
}

struct Frame {
    double t = 0.0;
    double duration = 1.0;
    int width = 1920;
    int height = 1080;
    int fps = 60;

    [[nodiscard]] double progress(Seconds start, Seconds end) const {
        const double begin = start.count();
        const double finish = end.count();
        if (finish <= begin) {
            return 0.0;
        }
        return clamp01((t - begin) / (finish - begin));
    }

    [[nodiscard]] double local(Seconds start) const {
        return t - start.count();
    }
};

enum class LayerType {
    Video,
    Text,
    Image,
    Solid,
    Audio
};

struct Layer {
    LayerType type = LayerType::Video;
    std::string source;
    std::string caption;
    std::string fontFile;
    Vec3 pos {0.0, 0.0, 0.0};
    Vec3 rot {0.0, 0.0, 0.0};
    Vec3 scl {1.0, 1.0, 1.0};
    Vec2 anchorPoint {0.5, 0.5};
    Color tint {1.0, 1.0, 1.0, 1.0};
    Seconds startTime {0.0};
    Seconds trimStart {0.0};
    Seconds trimEnd {0.0};
    Seconds fadeIn {0.0};
    double opacityValue = 1.0;
    double fontSize = 72.0;
    double volumeDb = 0.0;

    static Layer video(std::string path) {
        Layer layer;
        layer.type = LayerType::Video;
        layer.source = std::move(path);
        return layer;
    }

    static Layer image(std::string path) {
        Layer layer;
        layer.type = LayerType::Image;
        layer.source = std::move(path);
        return layer;
    }

    static Layer text(std::string value) {
        Layer layer;
        layer.type = LayerType::Text;
        layer.caption = std::move(value);
        return layer;
    }

    static Layer solid(Color value = {}) {
        Layer layer;
        layer.type = LayerType::Solid;
        layer.tint = value;
        return layer;
    }

    static Layer audio(std::string path) {
        Layer layer;
        layer.type = LayerType::Audio;
        layer.source = std::move(path);
        return layer;
    }

    Layer& at(Seconds value) {
        startTime = value;
        return *this;
    }

    Layer& trim(Seconds start, Seconds end) {
        trimStart = start;
        trimEnd = end;
        return *this;
    }

    Layer& fade(Seconds value) {
        fadeIn = value;
        return *this;
    }

    Layer& position(Vec3 value) {
        pos = value;
        return *this;
    }

    Layer& position(double x, double y, double z = 0.0) {
        pos = {x, y, z};
        return *this;
    }

    Layer& rotation(Vec3 value) {
        rot = value;
        return *this;
    }

    Layer& rotation(double x, double y, double z = 0.0) {
        rot = {x, y, z};
        return *this;
    }

    Layer& scale(double value) {
        scl = {value, value, value};
        return *this;
    }

    Layer& scale(Vec3 value) {
        scl = value;
        return *this;
    }

    Layer& z(double value) {
        pos.z = value;
        return *this;
    }

    Layer& opacity(double value) {
        opacityValue = value;
        return *this;
    }

    Layer& anchor(Vec2 value) {
        anchorPoint = value;
        return *this;
    }

    Layer& font(std::string value, double size) {
        fontFile = std::move(value);
        fontSize = size;
        return *this;
    }

    Layer& color(Color value) {
        tint = value;
        return *this;
    }

    Layer& volume(double value) {
        volumeDb = value;
        return *this;
    }

    [[nodiscard]] std::string describe() const {
        std::ostringstream stream;
        stream << layerTypeName() << " {";
        if (!source.empty()) {
            stream << " source='" << source << "'";
        }
        if (!caption.empty()) {
            stream << " text='" << caption << "'";
        }
        stream << " pos=(" << pos.x << ", " << pos.y << ", " << pos.z << ")";
        stream << " rot=(" << rot.x << ", " << rot.y << ", " << rot.z << ")";
        stream << " scale=(" << scl.x << ", " << scl.y << ", " << scl.z << ")";
        stream << " opacity=" << opacityValue;
        if (trimEnd.count() > trimStart.count()) {
            stream << " trim=[" << trimStart.count() << ", " << trimEnd.count() << "]";
        }
        if (fadeIn.count() > 0.0) {
            stream << " fadeIn=" << fadeIn.count();
        }
        if (type == LayerType::Audio) {
            stream << " volumeDb=" << volumeDb;
        }
        stream << " }";
        return stream.str();
    }

private:
    [[nodiscard]] const char* layerTypeName() const {
        switch (type) {
        case LayerType::Video: return "Video";
        case LayerType::Text: return "Text";
        case LayerType::Image: return "Image";
        case LayerType::Solid: return "Solid";
        case LayerType::Audio: return "Audio";
        }
        return "Layer";
    }
};

inline Layer Video(std::string path) {
    return Layer::video(std::move(path));
}

inline Layer Image(std::string path) {
    return Layer::image(std::move(path));
}

inline Layer Text(std::string value) {
    return Layer::text(std::move(value));
}

inline Layer Solid(Color value = {}) {
    return Layer::solid(value);
}

inline Layer Audio(std::string path) {
    return Layer::audio(std::move(path));
}

struct Transition {
    Seconds duration {0.0};
    std::string type = "crossfade";
};

inline Transition CrossFade(Seconds duration) {
    return {duration, "crossfade"};
}

struct Camera {
    Vec3 pos {0.0, 0.0, 1000.0};
    Vec3 rot {0.0, 0.0, 0.0};
    double fov = 30.0;
    double nearPlane = 1.0;
    double farPlane = 10000.0;
    bool autoPush = true;
    double pushSpeed = 50.0;

    Camera& position(Vec3 value) {
        pos = value;
        return *this;
    }

    Camera& position(double x, double y, double z) {
        pos = {x, y, z};
        return *this;
    }

    Camera& rotation(Vec3 value) {
        rot = value;
        return *this;
    }

    Camera& rotation(double x, double y, double z = 0.0) {
        rot = {x, y, z};
        return *this;
    }

    Camera& fieldOfView(double value) {
        fov = value;
        return *this;
    }

    Camera& pushIn(double speed = 50.0) {
        autoPush = true;
        pushSpeed = speed;
        return *this;
    }

    [[nodiscard]] Camera evaluated(const Frame& frame) const {
        Camera copy = *this;
        if (copy.autoPush) {
            const double travel = std::fmod(frame.t * copy.pushSpeed, 400.0);
            copy.pos.z -= travel;
        }
        return copy;
    }

    [[nodiscard]] std::string describe(const Frame& frame) const {
        const Camera active = evaluated(frame);
        std::ostringstream stream;
        stream << "Camera {";
        stream << " pos=(" << active.pos.x << ", " << active.pos.y << ", " << active.pos.z << ")";
        stream << " rot=(" << active.rot.x << ", " << active.rot.y << ", " << active.rot.z << ")";
        stream << " fov=" << active.fov;
        stream << " near=" << active.nearPlane;
        stream << " far=" << active.farPlane;
        stream << " }";
        return stream.str();
    }
};

struct Composition {
    std::vector<Layer> layers;
    std::optional<Camera> camera;

    Composition& add(Layer layer) {
        layers.push_back(std::move(layer));
        return *this;
    }

    Composition with(Camera value) const {
        Composition copy = *this;
        copy.camera = std::move(value);
        return copy;
    }

    [[nodiscard]] std::vector<Layer> sortedLayers() const {
        std::vector<Layer> copy = layers;
        std::sort(copy.begin(), copy.end(), [](const Layer& lhs, const Layer& rhs) {
            return lhs.pos.z < rhs.pos.z;
        });
        return copy;
    }

    [[nodiscard]] std::string describe(const Frame& frame) const {
        std::ostringstream stream;
        stream << "Composition frame=" << frame.t << "s";
        if (camera.has_value()) {
            stream << "\n  " << camera->describe(frame);
        }
        const std::vector<Layer> ordered = sortedLayers();
        for (const Layer& layer : ordered) {
            stream << "\n  " << layer.describe();
        }
        return stream.str();
    }
};

template <typename... Layers>
Composition Compose(Layers&&... layers) {
    Composition composition;
    (composition.add(std::forward<Layers>(layers)), ...);
    return composition;
}

class Scene {
public:
    using Builder = std::function<Composition(Frame)>;

    Scene() = default;

    template <typename Fn, typename = std::enable_if_t<!std::is_same_v<std::decay_t<Fn>, Scene>>>
    Scene(Fn&& builder)
        : m_builder(std::forward<Fn>(builder)) {
    }

    [[nodiscard]] Composition operator()(Frame frame) const {
        if (!m_builder) {
            return {};
        }
        return m_builder(frame);
    }

    [[nodiscard]] Scene duration(Seconds value) const {
        Scene copy = *this;
        copy.m_duration = value;
        return copy;
    }

    [[nodiscard]] Scene transition(Transition value) const {
        Scene copy = *this;
        copy.m_transition = std::move(value);
        return copy;
    }

    [[nodiscard]] Seconds duration() const {
        return m_duration;
    }

    [[nodiscard]] const std::optional<Transition>& transition() const {
        return m_transition;
    }

private:
    Builder m_builder;
    Seconds m_duration {0.0};
    std::optional<Transition> m_transition;
};

struct RenderSize {
    int width = 1920;
    int height = 1080;
};

struct RenderJob {
    RenderSize size;
    int fps = 60;
    std::vector<Scene> scenes;
    std::string output = "output.mp4";
};

struct RenderSettings {
    int width = 1920;
    int height = 1080;
    int fps = 60;
};

class Project {
public:
    explicit Project(RenderSettings settings)
        : m_settings(settings) {
    }

    void preview(const Scene& scene, std::initializer_list<double> sampleTimes) const {
        CHRONON_ZONE_SCOPED;
        for (double sampleTime : sampleTimes) {
            Frame frame;
            frame.t = sampleTime;
            frame.width = m_settings.width;
            frame.height = m_settings.height;
            frame.fps = m_settings.fps;

            const Composition composition = scene(frame);
            std::ostringstream stream;
            stream << "[frame " << sampleTime << "s]\n";
            stream << composition.describe(frame) << "\n";
            std::cout << stream.str();
        }
    }

private:
    RenderSettings m_settings;
};

inline double progress(const Frame& frame, Seconds start, Seconds end, double from = 0.0, double to = 1.0) {
    return lerp(from, to, frame.progress(start, end));
}

} // namespace ch
