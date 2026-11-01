#include <chronon3d/render_graph/core/render_graph_hashing.hpp>

namespace chronon3d::graph {

namespace detail {

// Digest sink for graphics::write_gradient_identity — the Cat-3 single seat
// for the gradient identity field selection/order (graphics/gradient.hpp).
// Only the encoding lives here; the digest is bit-identical to the
// historical inline hash_gradient: the first field seeds the digest
// directly, every later field folds in with hash_combine.
struct GradientIdentityHashSink {
    u64 seed{0};

    template <typename T>
    void operator()(const T& value) {
        if constexpr (std::is_enum_v<T>) {
            fold(hash_value(static_cast<int>(value)));
        } else if constexpr (std::is_same_v<T, Color>) {
            fold(hash_color(value));
        } else if constexpr (std::is_same_v<T, Vec2>) {
            fold(hash_vec2(value));
        } else {
            fold(hash_value(value));
        }
    }

    void count(usize size) { fold(hash_value(size)); }

private:
    void fold(u64 value) {
        seed = m_first ? value : hash_combine(seed, value);
        m_first = false;
    }

    bool m_first{true};
};

struct SemanticGradientHashSink {
    core::hash::HashBuilder builder;

    template <typename T>
    void operator()(const T& value) {
        if constexpr (std::is_same_v<T, Color>) {
            builder.add(value.r == 0.0f ? 0.0f : value.r)
                   .add(value.g == 0.0f ? 0.0f : value.g)
                   .add(value.b == 0.0f ? 0.0f : value.b)
                   .add(value.a == 0.0f ? 0.0f : value.a);
        } else if constexpr (std::is_same_v<T, Vec2>) {
            builder.add(value.x == 0.0f ? 0.0f : value.x)
                   .add(value.y == 0.0f ? 0.0f : value.y);
        } else if constexpr (std::is_floating_point_v<T>) {
            builder.add(value == 0.0f ? T{0} : value);
        } else if constexpr (std::is_enum_v<T>) {
            builder.add_enum(value);
        } else {
            builder.add(value);
        }
    }

    void count(usize size) { builder.add(size); }
    [[nodiscard]] u64 finish() const { return builder.finish(); }
};

} // namespace detail

[[nodiscard]] u64 hash_combine(u64 seed, u64 value) {
    return core::hash::hash_combine(seed, value);
}

[[nodiscard]] u64 hash_bytes(const void* data, usize size) {
    return core::hash::hash_bytes(data, size);
}

[[nodiscard]] u64 hash_string(std::string_view value) {
    return core::hash::hash_string(value);
}

[[nodiscard]] u64 hash_vec2(const Vec2& value) {
    return hash_bytes(&value, sizeof(value));
}

[[nodiscard]] u64 hash_vec3(const Vec3& value) {
    return hash_bytes(&value, sizeof(value));
}

[[nodiscard]] u64 hash_color(const Color& color) {
    return hash_bytes(&color, sizeof(color));
}

[[nodiscard]] u64 hash_float_v2(f32 value) {
    return core::hash::HashBuilder{}
        .add("chronon.float.v2")
        .add(value == 0.0f ? 0.0f : value)
        .finish();
}

[[nodiscard]] u64 hash_vec2_v2(const Vec2& value) {
    return core::hash::HashBuilder{}
        .add("chronon.vec2.v2")
        .add(value.x == 0.0f ? 0.0f : value.x)
        .add(value.y == 0.0f ? 0.0f : value.y)
        .finish();
}

[[nodiscard]] u64 hash_vec3_v2(const Vec3& value) {
    return core::hash::HashBuilder{}
        .add("chronon.vec3.v2")
        .add(value.x == 0.0f ? 0.0f : value.x)
        .add(value.y == 0.0f ? 0.0f : value.y)
        .add(value.z == 0.0f ? 0.0f : value.z)
        .finish();
}

[[nodiscard]] u64 hash_mat4_v2(const Mat4& value) {
    auto hash = core::hash::HashBuilder{}.add("chronon.mat4.v2");
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            const f32 component = value[column][row];
            hash.add(component == 0.0f ? 0.0f : component);
        }
    }
    return hash.finish();
}

[[nodiscard]] u64 hash_transform(const Transform& transform) {
    u64 h = hash_vec3(transform.position);
    h = hash_combine(h, hash_vec3(transform.anchor));
    h = hash_combine(h, hash_vec3(transform.scale));
    h = hash_combine(h, hash_bytes(&transform.rotation, sizeof(transform.rotation)));
    h = hash_combine(h, hash_bytes(&transform.opacity, sizeof(transform.opacity)));
    return h;
}

[[nodiscard]] u64 hash_surface_policy(SurfacePolicy policy) {
    return hash_bytes(&policy, sizeof(policy));
}

[[nodiscard]] u64 hash_transform_policy(TransformPolicy policy) {
    return hash_bytes(&policy, sizeof(policy));
}

[[nodiscard]] u64 hash_video_source(const video::VideoSource& source) {
    u64 seed = hash_string(source.path);
    seed = hash_combine(seed, hash_bytes(&source.source_start, sizeof(source.source_start)));
    seed = hash_combine(seed, hash_bytes(&source.duration, sizeof(source.duration)));
    seed = hash_combine(seed, hash_bytes(&source.source_fps, sizeof(source.source_fps)));
    seed = hash_combine(seed, hash_bytes(&source.speed, sizeof(source.speed)));
    const u64 loop = static_cast<u64>(source.loop_mode);
    seed = hash_combine(seed, hash_bytes(&loop, sizeof(loop)));
    seed = hash_combine(seed, hash_vec2(source.size));
    seed = hash_combine(seed, hash_string(source.ocio_config_path));
    seed = hash_combine(seed, hash_string(source.ocio_input_space));
    seed = hash_combine(seed, hash_string(source.ocio_working_space));
    return seed;
}

[[nodiscard]] u64 hash_video_source_v2(const video::VideoSource& source) {
    const auto semantic_float = [](f32 value) {
        return value == 0.0f ? 0.0f : value;
    };
    return core::hash::HashBuilder{}
        .add("chronon.video-source.v2")
        .add(source.path)
        .add(semantic_float(source.size.x))
        .add(semantic_float(source.size.y))
        .add(source.source_start)
        .add(source.duration)
        .add(semantic_float(source.source_fps))
        .add(semantic_float(source.speed))
        .add_enum(source.loop_mode)
        .add(source.ocio_config_path)
        .add(source.ocio_input_space)
        .add(source.ocio_working_space)
        .finish();
}

[[nodiscard]] u64 hash_gradient(const graphics::GradientDefinition& gradient) {
    detail::GradientIdentityHashSink sink;
    graphics::write_gradient_identity(gradient, sink);
    return sink.seed;
}

[[nodiscard]] u64 hash_gradient_v2(const graphics::GradientDefinition& gradient) {
    detail::SemanticGradientHashSink sink;
    sink.builder.add("chronon.gradient.v2");
    graphics::write_gradient_identity(gradient, sink);
    return sink.finish();
}

[[nodiscard]] u64 hash_fill(const Fill& f) {
    u64 seed = hash_bytes(&f.type, sizeof(f.type));
    seed = hash_combine(seed, hash_value(f.enabled));
    seed = hash_combine(seed, hash_color(f.solid));
    seed = hash_combine(seed, hash_gradient(f.gradient));
    seed = hash_combine(seed, hash_value(f.field.has_value()));
    if (f.field)
        seed = hash_combine(seed, hash_value(graphics::field2d_fingerprint(*f.field)));
    if (f.field_ramp) {
        seed = hash_combine(seed, hash_value(true));
        seed = hash_combine(seed, hash_gradient(*f.field_ramp));
    }
    if (f.field_grid) {
        seed = hash_combine(seed, hash_value(true));
        seed = hash_combine(seed, hash_value(f.field_grid->width));
        seed = hash_combine(seed, hash_value(f.field_grid->height));
    }
    if (f.gradient_mesh) {
        seed = hash_combine(seed, hash_value(true));
        seed = hash_combine(seed, hash_value(f.gradient_mesh->nodes.size()));
        for (const auto& node : f.gradient_mesh->nodes) {
            seed = hash_combine(seed, hash_vec2(node.position));
            seed = hash_combine(seed, hash_color(node.color));
            seed = hash_combine(seed, hash_value(node.weight));
        }
    }
    return seed;
}

[[nodiscard]] u64 hash_fill_v2(const Fill& fill) {
    detail::SemanticGradientHashSink sink;
    sink.builder.add("chronon.fill.v2")
        .add_enum(fill.type)
        .add(fill.enabled);
    sink(fill.solid);
    graphics::write_gradient_identity(fill.gradient, sink);
    sink(fill.field.has_value());
    if (fill.field) sink.builder.add(graphics::field2d_fingerprint_v2(*fill.field));
    sink(fill.field_ramp.has_value());
    if (fill.field_ramp) graphics::write_gradient_identity(*fill.field_ramp, sink);
    sink(fill.field_grid.has_value());
    if (fill.field_grid) {
        sink.builder.add(fill.field_grid->width).add(fill.field_grid->height)
            .add(fill.field_grid->values.size());
        for (const auto value : fill.field_grid->values) sink(value);
    }
    sink(fill.gradient_mesh.has_value());
    if (fill.gradient_mesh) {
        sink.count(fill.gradient_mesh->nodes.size());
        for (const auto& node : fill.gradient_mesh->nodes) {
            sink(node.position);
            sink(node.color);
            sink(node.weight);
        }
    }
    return sink.finish();
}

[[nodiscard]] u64 hash_glow_params(const GlowParams& p) {
    u64 seed = hash_value(p.radius);
    seed = hash_combine(seed, hash_value(p.intensity));
    seed = hash_combine(seed, hash_color(p.color));
    return seed;
}


} // namespace chronon3d::graph
