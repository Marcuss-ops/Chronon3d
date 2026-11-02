#include <chronon3d/render_graph/core/render_graph_hashing.hpp>

namespace chronon3d::graph {

[[nodiscard]] u64 hash_mask(const Mask& m) {
    if (!m.enabled()) {
        return 0;
    }
    u64 seed = hash_value(static_cast<int>(m.type));
    seed = hash_combine(seed, hash_value(static_cast<int>(m.initial_mode)));
    seed = hash_combine(seed, hash_vec3(m.pos));
    seed = hash_combine(seed, hash_vec2(m.size));
    seed = hash_combine(seed, hash_value(m.radius));
    seed = hash_combine(seed, hash_value(m.feather));
    seed = hash_combine(seed, hash_value(m.expansion));
    seed = hash_combine(seed, hash_value(m.opacity));
    seed = hash_combine(seed, hash_value(m.inverted));
    seed = hash_combine(seed, hash_string(m.text_source));
    seed = hash_combine(seed, hash_value(m.field.has_value()));
    if (m.field) seed = hash_combine(seed, hash_value(graphics::field2d_fingerprint(*m.field)));
    seed = hash_combine(seed, hash_value(m.path.size()));
    for (const auto& command : m.path) {
        seed = hash_combine(seed, hash_value(static_cast<int>(command.type)));
        seed = hash_combine(seed, hash_vec2(command.p0));
        seed = hash_combine(seed, hash_vec2(command.p1));
        seed = hash_combine(seed, hash_vec2(command.p2));
    }
    seed = hash_combine(seed, hash_value(m.operations.size()));
    for (const auto& operation : m.operations) {
        seed = hash_combine(seed, hash_value(static_cast<int>(operation.mode)));
        seed = hash_combine(seed, hash_value(static_cast<int>(operation.type)));
        seed = hash_combine(seed, hash_vec3(operation.pos));
        seed = hash_combine(seed, hash_vec2(operation.size));
        seed = hash_combine(seed, hash_value(operation.radius));
        seed = hash_combine(seed, hash_value(operation.feather));
        seed = hash_combine(seed, hash_value(operation.expansion));
        seed = hash_combine(seed, hash_value(operation.opacity));
        seed = hash_combine(seed, hash_value(operation.inverted));
        seed = hash_combine(seed, hash_string(operation.text_source));
        seed = hash_combine(seed, hash_value(operation.field.has_value()));
        if (operation.field)
            seed = hash_combine(seed, hash_value(graphics::field2d_fingerprint(*operation.field)));
        seed = hash_combine(seed, hash_value(operation.path.size()));
        for (const auto& command : operation.path) {
            seed = hash_combine(seed, hash_value(static_cast<int>(command.type)));
            seed = hash_combine(seed, hash_vec2(command.p0));
            seed = hash_combine(seed, hash_vec2(command.p1));
            seed = hash_combine(seed, hash_vec2(command.p2));
        }
    }
    return seed;
}

[[nodiscard]] u64 hash_mask_v2(const Mask& mask) {
    using Builder = core::hash::HashBuilder;
    Builder builder;
    const auto add_float = [&builder](f32 value) {
        builder.add(value == 0.0f ? 0.0f : value);
    };
    const auto add_vec2 = [&](const Vec2& value) {
        add_float(value.x);
        add_float(value.y);
    };
    const auto add_vec3 = [&](const Vec3& value) {
        add_float(value.x);
        add_float(value.y);
        add_float(value.z);
    };
    const auto add_path = [&](const std::vector<PathCommand>& path) {
        builder.add(path.size());
        for (const auto& command : path) {
            builder.add_enum(command.type);
            add_vec2(command.p0);
            add_vec2(command.p1);
            add_vec2(command.p2);
        }
    };
    const auto add_operation = [&](const MaskOperation& operation) {
        builder.add_enum(operation.mode).add_enum(operation.type);
        add_vec3(operation.pos);
        add_vec2(operation.size);
        add_float(operation.radius);
        add_float(operation.feather);
        add_float(operation.expansion);
        add_float(operation.opacity);
        builder.add(operation.inverted).add(operation.text_source)
            .add(operation.field.has_value());
        if (operation.field)
            builder.add(graphics::field2d_fingerprint_v2(*operation.field));
        add_path(operation.path);
    };

    builder.add("chronon.mask.v2").add(mask.enabled());
    if (!mask.enabled()) return builder.finish();
    builder.add_enum(mask.type).add_enum(mask.initial_mode);
    add_vec3(mask.pos);
    add_vec2(mask.size);
    add_float(mask.radius);
    add_float(mask.feather);
    add_float(mask.expansion);
    add_float(mask.opacity);
    builder.add(mask.inverted).add(mask.text_source).add(mask.field.has_value());
    if (mask.field) builder.add(graphics::field2d_fingerprint_v2(*mask.field));
    add_path(mask.path);
    builder.add(mask.operations.size());
    for (const auto& operation : mask.operations) add_operation(operation);
    return builder.finish();
}


} // namespace chronon3d::graph
