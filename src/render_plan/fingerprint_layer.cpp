#include "fingerprint_detail.hpp"

namespace chronon3d::render_plan::fingerprint_detail {

void add_layer(CanonicalPlanWriter& out, const LayerPlan& layer) {
    out.add_string(layer.id);
    out.add_enum(layer.type);
    add_optional(out, layer.flow_layout, [](auto& w, const LayerPlan::FlowLayoutPlan& p) {
        w.add_string(p.group_id); w.add_string(p.axis); w.add_string(p.align); w.add_string(p.justify);
        w.add_float(p.gap); w.add_bool(p.wrap); w.add_float(p.cell_size[0]); w.add_float(p.cell_size[1]);
        add_optional(w, p.stagger, [](auto& writer, const auto& stagger) { add_stagger(writer, stagger); });
    });
    add_optional(out, layer.grid_layout, [](auto& w, const LayerPlan::GridLayoutPlan& p) {
        w.add_string(p.group_id); w.add_string(p.fit); w.add_uint64(static_cast<std::uint64_t>(p.columns)); w.add_uint64(static_cast<std::uint64_t>(p.rows));
        w.add_float(p.gap[0]); w.add_float(p.gap[1]); w.add_float(p.cell_size[0]); w.add_float(p.cell_size[1]);
        add_optional(w, p.stagger, [](auto& writer, const auto& stagger) { add_stagger(writer, stagger); });
    });
    add_optional(out, layer.stagger_group, [](auto& w, const LayerPlan::StaggerPlan& stagger) {
        w.add_string(stagger.group_id);
        add_stagger(w, stagger);
    });
    add_optional(out, layer.follow_path, [](auto& w, const LayerPlan::FollowPathDriverPlan& p) {
        w.add_string(p.path_id);
        w.add_size(p.path_commands.size());
        for (const auto& command : p.path_commands) {
            w.add_enum(command.type);
            for (const auto value : command.point) w.add_float(value);
            for (const auto value : command.control1) w.add_float(value);
            for (const auto value : command.control2) w.add_float(value);
        }
        add_track(w, p.progress);
        w.add_bool(p.auto_orient);
        w.add_float(p.offset_deg);
        w.add_bool(p.clamp_end);
    });
    add_optional(out, layer.text_counter, [](auto& writer, const TextCounterPlan& value) {
        writer.add_string(value.token);
        writer.add_float(value.counter.from);
        writer.add_float(value.counter.to);
        writer.add_frame(value.counter.start_frame);
        writer.add_frame(value.counter.duration_frames);
        writer.add_string(value.counter.easing);
        writer.add_string(value.counter.format);
        writer.add_string(value.counter.currency);
        writer.add_i64(value.counter.decimals);
        writer.add_string(value.counter.prefix);
        writer.add_string(value.counter.suffix);
    });
    add_optional(out, layer.parent, [](auto& writer, const std::string& value) {
        writer.add_string(value);
    });
    add_optional(out, layer.track_matte, [](auto& writer, const TrackMattePlan& value) {
        writer.add_string(value.source);
        writer.add_enum(value.type);
    });
    add_optional(out, layer.time_remap, [](auto& writer, const TimeRemapPlan& value) {
        add_optional(writer, value.speed, [](auto& nested, float speed) {
            nested.add_float(speed);
        });
        writer.add_bool(value.reverse);
        add_optional(writer, value.freeze_frame, [](auto& nested, Frame frame) {
            nested.add_frame(frame);
        });
        add_optional(writer, value.animation, [](auto& nested, const AnimationTrackPlan& track) {
            add_track(nested, track);
        });
    });
    out.add_string(layer.asset);
    out.add_string(layer.input_color_space);
    out.add_string(layer.ocio_input_space);
    out.add_string(layer.source);
    out.add_string(layer.text);
    out.add_bool(layer.inline_mesh.has_value());
    if (layer.inline_mesh) {
        out.add_size(layer.inline_mesh->vertices.size());
        for (const auto& vertex : layer.inline_mesh->vertices) {
            for (const float value : vertex.position) out.add_float(value);
            for (const float value : vertex.normal) out.add_float(value);
            for (const float value : vertex.uv) out.add_float(value);
        }
        out.add_size(layer.inline_mesh->indices.size());
        for (const std::uint32_t index : layer.inline_mesh->indices) out.add_uint64(index);
        add_optional(out, layer.inline_mesh->material, [](auto& writer, const InlineMeshMaterialPlan& material) {
            writer.add_float(material.diffuse);
            writer.add_float(material.specular);
            writer.add_float(material.shininess);
            writer.add_float(material.rim_intensity);
            add_optional(writer, material.base_color, [](auto& nested, const auto& color) {
                for (const float value : color) nested.add_float(value);
            });
        });
    }
    if (!layer.text_spans.empty()) {
      out.add_size(layer.text_spans.size());
      for (const auto& span : layer.text_spans) {
        out.add_size(span.byte_start);
        out.add_size(span.byte_end);
        add_optional(out, span.color, [](auto& writer, const Color& color) {
            writer.add_float(color.r); writer.add_float(color.g);
            writer.add_float(color.b); writer.add_float(color.a);
        });
        add_optional(out, span.font_size_multiplier, [](auto& writer, float value) {
            writer.add_float(value);
        });
        add_optional(out, span.tracking, [](auto& writer, float value) {
            writer.add_float(value);
        });
        add_optional(out, span.stroke, [](auto& writer, const TextStrokeStyle& stroke) {
            writer.add_float(stroke.width_em);
            add_optional(writer, stroke.color, [](auto& nested, const Color& color) {
                nested.add_float(color.r); nested.add_float(color.g);
                nested.add_float(color.b); nested.add_float(color.a);
            });
            add_optional(writer, stroke.dash_period_em, [](auto& nested, float value) {
                nested.add_float(value);
            });
        });
        add_optional(out, span.semantic_id, [](auto& writer, const std::string& value) {
            writer.add_string(value);
        });
        add_optional(out, span.highlight, [](auto& writer, const TextHighlightStyle& highlight) {
            add_optional(writer, highlight.background, [](auto& nested, const Color& color) {
                nested.add_float(color.r); nested.add_float(color.g);
                nested.add_float(color.b); nested.add_float(color.a);
            });
            add_optional(writer, highlight.padding_em, [](auto& nested, float value) {
                nested.add_float(value);
            });
            add_optional(writer, highlight.radius_em, [](auto& nested, float value) {
                nested.add_float(value);
            });
        });
      }
    }
    if (layer.motion_blur) {
        out.add_float(layer.motion_blur->shutter_angle);
        out.add_size(layer.motion_blur->samples);
    }
    out.add_string(layer.font);
    add_optional(out, layer.style,
                 [](auto& writer, const LayerStylePlan& value) {
                     add_style(writer, value);
                 });
    // Preserve the historical optional-presence byte for every layer; only
    // present shape payloads append the additive shape contract fields.
    out.add_bool(layer.shape.has_value());
    if (layer.shape) {
        const auto& shape = *layer.shape;
        out.add_enum(shape.type);
        if (shape.type == ShapeContentTypePlan::DeviceFrame) {
            out.add_enum(shape.device_frame_kind);
            out.add_bool(shape.device_frame_chrome);
        }
        add_optional(out, shape.svg_asset, [](auto& nested, const std::string& asset) {
            nested.add_string(asset);
        });
        add_optional(out, shape.lottie_asset, [](auto& nested, const std::string& asset) {
            nested.add_string(asset);
        });
        out.add_float(shape.radius);
        if (shape.corner_radius) {
            out.add_bool(true);
            for (const auto radius : *shape.corner_radius) out.add_float(radius);
        }
        add_optional(out, shape.fill_color, [](auto& nested, const auto& color) {
            for (const auto component : color) nested.add_float(component);
        });
        add_optional(out, shape.gradient, [](auto& nested,
                                                 const graphics::GradientDefinition& value) {
            add_gradient(nested, value);
        });
        add_optional(out, shape.field, [](auto& nested, const auto& field) {
            add_field2d(nested, field);
        });
        // Append ramp identity only when present to preserve fingerprints for
        // legacy plans without a field color ramp.
        if (shape.field_ramp) {
            out.add_bool(true);
            add_gradient(out, *shape.field_ramp);
        }
        if (shape.field_render_scale != 1) {
            out.add_bool(true);
            out.add_uint64(shape.field_render_scale);
        }
        if (shape.field_drift) {
            out.add_bool(true);
            out.add_float(shape.field_drift->x);
            out.add_float(shape.field_drift->y);
        }
        // Append mesh identity only when present to preserve fingerprints for
        // every legacy plan that predates mesh fills.
        if (shape.gradient_mesh) {
            out.add_bool(true);
            out.add_size(shape.gradient_mesh->nodes.size());
            for (const auto& node : shape.gradient_mesh->nodes) {
                out.add_float(node.position.x);
                out.add_float(node.position.y);
                out.add_float(node.color.r);
                out.add_float(node.color.g);
                out.add_float(node.color.b);
                out.add_float(node.color.a);
                out.add_float(node.weight);
            }
        }
        add_optional(out, shape.stroke, [](auto& nested, const StrokeStyle& stroke) {
            nested.add_string(stroke.color);
            add_optional(nested, stroke.width, [](auto& w, float v) { w.add_float(v); });
            add_optional(nested, stroke.solid_color, [](auto& w, const auto& color) {
                for (const auto component : color) w.add_float(component);
            });
            add_optional(nested, stroke.trim_start, [](auto& w, float v) { w.add_float(v); });
            add_optional(nested, stroke.trim_end, [](auto& w, float v) { w.add_float(v); });
            nested.add_size(stroke.dash_array.size());
            for (const float dash : stroke.dash_array) nested.add_float(dash);
            nested.add_float(stroke.dash_offset);
            nested.add_string(stroke.cap);
            nested.add_string(stroke.join);
            nested.add_float(stroke.taper_start);
            nested.add_float(stroke.taper_end);
            add_optional(nested, stroke.gradient, [](auto& w,
                    const graphics::GradientDefinition& value) {
                add_gradient(w, value);
            });
            add_optional(nested, stroke.brush, [](auto& w,
                    const graphics::BrushProfile& brush) {
                w.add_uint64(brush.fingerprint());
            });
        });
        out.add_size(shape.path.size());
        for (const auto& command : shape.path) {
            out.add_enum(command.type);
            for (const auto value : command.point) out.add_float(value);
            for (const auto value : command.control1) out.add_float(value);
            for (const auto value : command.control2) out.add_float(value);
        }
        out.add_size(shape.imported_subpaths.size());
        for (const auto& subpath : shape.imported_subpaths) {
            add_optional(out, subpath.fill_color, [](auto& writer, const auto& color) {
                for (const auto component : color) writer.add_float(component);
            });
            add_optional(out, subpath.fill_gradient, [](auto& writer, const auto& gradient) {
                add_gradient(writer, gradient);
            });
            add_optional(out, subpath.fill_gradient_animation,
                [](auto& writer, const GradientAnimationPlan& animation) {
                    add_gradient_animation(writer, animation);
                });
            add_optional(out, subpath.stroke, [](auto& writer, const StrokeStyle& stroke) {
                add_optional(writer, stroke.width, [](auto& nested, float value) {
                    nested.add_float(value);
                });
                add_optional(writer, stroke.solid_color, [](auto& nested, const auto& color) {
                    for (const auto component : color) nested.add_float(component);
                });
                add_optional(writer, stroke.brush, [](auto& nested,
                        const graphics::BrushProfile& brush) {
                    nested.add_uint64(brush.fingerprint());
                });
            });
            out.add_size(subpath.path.size());
            for (const auto& command : subpath.path) {
                out.add_enum(command.type);
                for (const auto value : command.point) out.add_float(value);
                for (const auto value : command.control1) out.add_float(value);
                for (const auto value : command.control2) out.add_float(value);
            }
            if (!subpath.morph_target.empty() || subpath.morph_animation) {
                out.add_bool(true);
                out.add_size(subpath.morph_target.size());
                for (const auto& command : subpath.morph_target) {
                    out.add_enum(command.type);
                    for (const auto value : command.point) out.add_float(value);
                    for (const auto value : command.control1) out.add_float(value);
                    for (const auto value : command.control2) out.add_float(value);
                }
                add_optional(out, subpath.morph_animation,
                    [](auto& writer, const AnimationTrackPlan& track) { add_track(writer, track); });
            }
        }
        // ── Additive web-cool fields (v12): only emitted when present, so legacy v11 plans stay byte-identical.
        if (shape.points || shape.device_frame ||
            shape.arrow_head_length || shape.arrow_head_width ||
            shape.type == ShapeContentTypePlan::Star || shape.type == ShapeContentTypePlan::Polygon ||
            shape.type == ShapeContentTypePlan::Arrow || shape.type == ShapeContentTypePlan::Grid ||
            shape.type == ShapeContentTypePlan::DotGrid || shape.type == ShapeContentTypePlan::Arc) {
            out.add_bool(true);
            add_optional(out, shape.points, [](auto& writer, int value) { writer.add_i64(value); });
            out.add_float(shape.inner_radius_ratio);
            out.add_float(shape.shape_rotation_deg);
            add_optional(out, shape.arrow_head_length, [](auto& writer, float value) { writer.add_float(value); });
            add_optional(out, shape.arrow_head_width, [](auto& writer, float value) { writer.add_float(value); });
            out.add_float(shape.arrow_shaft_width);
            out.add_string(shape.arrow_direction);
            out.add_float(shape.grid_spacing);
            out.add_float(shape.grid_line_width);
            out.add_float(shape.dot_radius);
            out.add_float(shape.arc_start_deg);
            out.add_float(shape.arc_sweep_deg);
            out.add_float(shape.arc_thickness);
            add_optional(out, shape.device_frame, [](auto& writer, const DeviceFramePlan& df) {
                writer.add_string(df.frame_type);
                writer.add_float(df.radius);
                writer.add_float(df.border_width);
                writer.add_string(df.border_color);
                writer.add_bool(df.show_chrome);
            });
        }
        out.add_size(shape.operators.size());
        for (const auto& op : shape.operators) {
            out.add_enum(op.kind);
            if (op.kind == PathOperatorKindPlan::RoundCorners) {
                out.add_float(op.radius);
                continue;
            }
            if (op.kind == PathOperatorKindPlan::ZigZag) {
                out.add_float(op.size);
                out.add_float(op.points_per_unit);
                out.add_bool(op.smooth);
                continue;
            }
            if (op.kind == PathOperatorKindPlan::OffsetPath) {
                out.add_float(op.amount);
                out.add_float(op.miter_limit);
                continue;
            }
            if (op.kind == PathOperatorKindPlan::PuckerBloat) {
                out.add_float(op.amount);
                for (const auto value : op.anchor) out.add_float(value);
                continue;
            }
            if (op.kind == PathOperatorKindPlan::Twist) {
                out.add_float(op.degrees);
                for (const auto value : op.center) out.add_float(value);
                continue;
            }
            if (op.kind == PathOperatorKindPlan::WigglePath) {
                out.add_float(op.amount);
                out.add_float(op.frequency);
                out.add_uint64(op.seed);
                continue;
            }
            if (op.kind == PathOperatorKindPlan::Trim) {
                out.add_float(op.trim_start);
                out.add_float(op.trim_end);
                out.add_float(op.trim_offset);
                if (op.trim_animation) {
                    // Keep the v11 byte stream for static trim operators.
                    out.add_bool(true);
                    add_track(out, *op.trim_animation);
                }
                continue;
            }
            if (op.kind == PathOperatorKindPlan::Morph) {
                out.add_size(op.morph_target.size());
                for (const auto& command : op.morph_target) {
                    out.add_enum(command.type);
                    for (const auto value : command.point) out.add_float(value);
                    for (const auto value : command.control1) out.add_float(value);
                    for (const auto value : command.control2) out.add_float(value);
                }
                out.add_float(op.morph_progress);
                add_optional(out, op.morph_animation, [](auto& nested, const AnimationTrackPlan& track) {
                    add_track(nested, track);
                });
                continue;
            }
            if (op.kind == PathOperatorKindPlan::Merge) {
                out.add_size(op.paths.size());
                for (const auto& subpath : op.paths) {
                    out.add_size(subpath.size());
                    for (const auto& command : subpath) {
                        out.add_enum(command.type);
                        for (const auto value : command.point) out.add_float(value);
                        for (const auto value : command.control1) out.add_float(value);
                        for (const auto value : command.control2) out.add_float(value);
                    }
                }
                continue;
            }
            const auto& repeater = op.repeater;
            out.add_size(repeater.copies);
            for (const auto value : repeater.position) out.add_float(value);
            for (const auto value : repeater.scale) out.add_float(value);
            out.add_float(repeater.rotation_degrees);
            for (const auto value : repeater.anchor) out.add_float(value);
            out.add_float(repeater.opacity_decay);
        }
    }
    if (layer.particle_emitter) {
        const auto& particles = *layer.particle_emitter;
        out.add_uint64(particles.count);
        out.add_uint64(particles.seed);
        out.add_float(particles.emitter_size.x);
        out.add_float(particles.emitter_size.y);
        out.add_float(particles.velocity_min.x);
        out.add_float(particles.velocity_min.y);
        out.add_float(particles.velocity_max.x);
        out.add_float(particles.velocity_max.y);
        out.add_float(particles.lifetime_min);
        out.add_float(particles.lifetime_max);
        out.add_float(particles.size_min);
        out.add_float(particles.size_max);
        out.add_float(particles.gravity);
        out.add_float(particles.turbulence);
        out.add_uint64(static_cast<std::uint64_t>(particles.shape));
        out.add_string(particles.path_id); out.add_string(particles.sprite_asset);
        out.add_float(particles.sprite_scale_min); out.add_float(particles.sprite_scale_max);
        out.add_float(particles.rotation_min); out.add_float(particles.rotation_max);
        out.add_float(particles.angular_velocity_min); out.add_float(particles.angular_velocity_max);
        out.add_enum(particles.blend_mode);
        out.add_size(particles.path_points.size());
        for (const auto point : particles.path_points) { out.add_float(point.x); out.add_float(point.y); }
        add_gradient(out, particles.color_ramp);
    }
    if (layer.light) {
        out.add_float(layer.light->radius);
        for (const auto component : layer.light->color) out.add_float(component);
        out.add_float(layer.light->intensity);
        out.add_enum(layer.light->kind);
        for (const auto component : layer.light->direction) out.add_float(component);
        out.add_float(layer.light->range);
        out.add_float(layer.light->decay);
        out.add_float(layer.light->angle);
        out.add_float(layer.light->penumbra);
        out.add_bool(layer.light->cast_shadow);
        out.add_float(layer.light->shadow_radius);
        out.add_float(layer.light->shadow_bias);
    }
    if (layer.path_mask) {
        out.add_bool(true);
        out.add_string(layer.path_mask->type);
        out.add_string(layer.path_mask->mode);
        for (const auto value : layer.path_mask->position) out.add_float(value);
        for (const auto value : layer.path_mask->size) out.add_float(value);
        out.add_float(layer.path_mask->radius);
        out.add_float(layer.path_mask->feather);
        out.add_float(layer.path_mask->expansion);
        out.add_float(layer.path_mask->opacity);
        add_optional(out, layer.path_mask->field,
            [](auto& writer, const graphics::Field2D& field) { add_field2d(writer, field); });
        add_optional(out, layer.path_mask->feather_track,
            [](auto& writer, const AnimationTrackPlan& track) { add_track(writer, track); });
        add_optional(out, layer.path_mask->expansion_track,
            [](auto& writer, const AnimationTrackPlan& track) { add_track(writer, track); });
        add_optional(out, layer.path_mask->opacity_track,
            [](auto& writer, const AnimationTrackPlan& track) { add_track(writer, track); });
        out.add_bool(layer.path_mask->inverted);
        out.add_size(layer.path_mask->path.size());
        for (const auto& command : layer.path_mask->path) {
            out.add_enum(command.type);
            for (const auto value : command.point) out.add_float(value);
            for (const auto value : command.control1) out.add_float(value);
            for (const auto value : command.control2) out.add_float(value);
        }
        out.add_size(layer.path_mask->morph_target.size());
        for (const auto& command : layer.path_mask->morph_target) {
            out.add_enum(command.type);
            for (const auto value : command.point) out.add_float(value);
            for (const auto value : command.control1) out.add_float(value);
            for (const auto value : command.control2) out.add_float(value);
        }
        add_optional(out, layer.path_mask->morph_animation,
            [](auto& writer, const AnimationTrackPlan& animation) { add_track(writer, animation); });
    }
    out.add_size(layer.masks.size());
    for (const auto& mask : layer.masks) {
        out.add_string(mask.type);
        out.add_string(mask.mode);
        out.add_string(mask.text_source);
        for (const auto value : mask.position) out.add_float(value);
        for (const auto value : mask.size) out.add_float(value);
        out.add_float(mask.radius);
        out.add_float(mask.feather);
        out.add_float(mask.expansion);
        out.add_float(mask.opacity);
        add_optional(out, mask.field,
            [](auto& writer, const graphics::Field2D& field) { add_field2d(writer, field); });
        add_optional(out, mask.feather_track,
            [](auto& writer, const AnimationTrackPlan& track) { add_track(writer, track); });
        add_optional(out, mask.expansion_track,
            [](auto& writer, const AnimationTrackPlan& track) { add_track(writer, track); });
        add_optional(out, mask.opacity_track,
            [](auto& writer, const AnimationTrackPlan& track) { add_track(writer, track); });
        out.add_bool(mask.inverted);
        const auto add_mask_commands = [&](const auto& commands) {
            out.add_size(commands.size());
            for (const auto& command : commands) {
                out.add_enum(command.type);
                for (const auto value : command.point) out.add_float(value);
                for (const auto value : command.control1) out.add_float(value);
                for (const auto value : command.control2) out.add_float(value);
            }
        };
        add_mask_commands(mask.path);
        add_mask_commands(mask.morph_target);
        add_optional(out, mask.morph_animation,
            [](auto& writer, const AnimationTrackPlan& animation) { add_track(writer, animation); });
    }
    if (!layer.effects.empty()) {
        out.add_bool(true);
        out.add_size(layer.effects.size());
        for (const auto& effect : layer.effects) {
            out.add_enum(effect.kind);
            switch (effect.kind) {
            case LayerPlan::EffectKindPlan::Lut3D: {
                out.add_string(effect.asset);
                out.add_float(effect.amount);
            } break;
            case LayerPlan::EffectKindPlan::Echo: {
                out.add_size(effect.echoes);
                out.add_float(effect.decay);
                out.add_size(effect.time_offset_frames);
            } break;
            case LayerPlan::EffectKindPlan::Keying: {
                out.add_enum(effect.keying_mode);
                for (const auto value : effect.key_color) out.add_float(value);
                out.add_float(effect.threshold);
                out.add_float(effect.softness);
                out.add_float(effect.spill);
                out.add_float(effect.clip_black);
                out.add_float(effect.clip_white);
                out.add_float(effect.choke);
                for (const auto value : effect.garbage_rect) out.add_float(value);
                out.add_float(effect.garbage_feather);
            } break;
            case LayerPlan::EffectKindPlan::Threshold: {
                out.add_float(effect.threshold);
                out.add_float(effect.threshold_softness);
            } break;
            case LayerPlan::EffectKindPlan::GaussianBlur: {
                out.add_float(effect.radius);
            } break;
            case LayerPlan::EffectKindPlan::DropShadow: {
                out.add_float(effect.radius);
                for (const auto value : effect.offset) out.add_float(value);
                for (const auto value : effect.effect_color) out.add_float(value);
            } break;
            case LayerPlan::EffectKindPlan::Bloom: {
                out.add_float(effect.bloom_threshold);
                out.add_float(effect.radius);
                out.add_float(effect.bloom_intensity);
            } break;
            case LayerPlan::EffectKindPlan::Glow: {
                out.add_float(effect.radius);
                out.add_float(effect.glow_intensity);
                for (const auto value : effect.effect_color) out.add_float(value);
            } break;
            case LayerPlan::EffectKindPlan::Vignette: {
                out.add_float(effect.radius);
                out.add_float(effect.softness);
                out.add_float(effect.amount);
                for (const auto value : effect.effect_color) out.add_float(value);
            } break;
            case LayerPlan::EffectKindPlan::Catalog: {
                out.add_string(effect.canonical_effect_id);
                out.add_size(effect.canonical_params.size());
                for (const auto& value : effect.canonical_params) {
                    out.add_string(value.param);
                    out.add_float(value.value);
                }
            } break;
            case LayerPlan::EffectKindPlan::LightRays: {
                for (const auto value : effect.light_rays_origin) out.add_float(value);
                out.add_float(effect.light_rays_length);
                out.add_float(effect.light_rays_density);
                for (const auto value : effect.light_rays_color) out.add_float(value);
                out.add_float(effect.light_rays_decay);
            } break;
            case LayerPlan::EffectKindPlan::Noise: {
                out.add_float(effect.amount);
                out.add_uint64(effect.seed);
                out.add_bool(effect.animated);
                out.add_bool(effect.rgb_noise);
                out.add_float(effect.noise_size);
                out.add_float(effect.grain_shadow_response);
                out.add_float(effect.grain_midtone_response);
                out.add_float(effect.grain_highlight_response);
            } break;
            case LayerPlan::EffectKindPlan::FractalNoise: {
                out.add_uint64(effect.seed);
                out.add_uint64(effect.octaves);
                out.add_float(effect.frequency);
                out.add_float(effect.amplitude);
                out.add_float(effect.evolution);
                out.add_float(effect.contrast);
                out.add_float(effect.brightness);
                out.add_bool(effect.rgb_noise);
                add_optional(out, effect.noise_field,
                    [](auto& writer, const graphics::Field2D& field) { add_field2d(writer, field); });
            } break;
            case LayerPlan::EffectKindPlan::CornerPin: {
                for (const auto value : effect.quad) out.add_float(value);
            } break;
            case LayerPlan::EffectKindPlan::Twirl: {
                for (const auto value : effect.warp_center) out.add_float(value);
                out.add_float(effect.warp_angle);
                out.add_float(effect.warp_radius);
                out.add_float(effect.warp_falloff);
            } break;
            case LayerPlan::EffectKindPlan::Bulge: {
                for (const auto value : effect.warp_center) out.add_float(value);
                out.add_float(effect.warp_amount);
                out.add_float(effect.warp_radius);
            } break;
            case LayerPlan::EffectKindPlan::MeshWarp: {
                out.add_size(effect.mesh_cols);
                out.add_size(effect.mesh_rows);
                out.add_float(effect.mesh_smoothness);
                out.add_size(effect.mesh_offsets.size());
                for (const auto value : effect.mesh_offsets) out.add_float(value);
            } break;
            case LayerPlan::EffectKindPlan::Sharpen: {
                out.add_float(effect.amount); out.add_float(effect.radius); out.add_float(effect.threshold);
            } break;
            case LayerPlan::EffectKindPlan::RoughenEdges: {
                out.add_float(effect.amount); out.add_float(effect.roughen_scale);
                out.add_uint64(effect.roughen_complexity); out.add_float(effect.warp_evolution);
                out.add_float(effect.warp_speed); out.add_uint64(effect.roughen_seed);
            } break;
            case LayerPlan::EffectKindPlan::TurbulentDisplace: {
                out.add_float(effect.amount); out.add_float(effect.warp_size); out.add_float(effect.warp_evolution);
                out.add_float(effect.warp_speed); out.add_uint64(effect.warp_complexity);
            } break;
            case LayerPlan::EffectKindPlan::WaveWarp: {
                out.add_uint64(effect.wave_type); for (auto v : effect.warp_direction) out.add_float(v);
                out.add_float(effect.amount); out.add_float(effect.warp_wavelength); out.add_float(effect.warp_speed); out.add_float(effect.warp_phase);
            } break;
            case LayerPlan::EffectKindPlan::Ripple: {
                for(auto v:effect.warp_center) out.add_float(v); out.add_float(effect.warp_radius); out.add_float(effect.warp_speed);
                out.add_float(effect.warp_width); out.add_float(effect.warp_height); out.add_float(effect.warp_damping);
            } break;
            case LayerPlan::EffectKindPlan::DisplacementMap: {
                out.add_string(effect.map_layer); out.add_float(effect.map_intensity_x);
                out.add_float(effect.map_intensity_y); out.add_uint64(effect.map_channel);
                add_optional(out, effect.map_field,
                    [](auto& writer, const graphics::Field2D& field) { add_field2d(writer, field); });
            } break;
            case LayerPlan::EffectKindPlan::ChromaticAberration: {
                out.add_float(effect.chromatic_red_offset);
                out.add_float(effect.chromatic_blue_offset);
                out.add_size(static_cast<std::size_t>(effect.chromatic_offset_mode));
                out.add_float(effect.chromatic_velocity[0]);
                out.add_float(effect.chromatic_velocity[1]);
                for (const auto value : effect.chromatic_red_transform) out.add_float(value);
                for (const auto value : effect.chromatic_green_transform) out.add_float(value);
                for (const auto value : effect.chromatic_blue_transform) out.add_float(value);
            } break;
            case LayerPlan::EffectKindPlan::Halftone: {
                out.add_size(effect.halftone_size);
                out.add_float(effect.halftone_angle);
                out.add_float(effect.halftone_amount);
            } break;
            case LayerPlan::EffectKindPlan::DirectionalBlur: {
                out.add_float(effect.directional_angle);
                out.add_float(effect.directional_length);
                out.add_size(static_cast<std::size_t>(effect.directional_samples));
            } break;
            case LayerPlan::EffectKindPlan::RadialBlur: {
                out.add_float(effect.radial_center[0]);
                out.add_float(effect.radial_center[1]);
                out.add_float(effect.radial_amount);
                out.add_size(static_cast<std::size_t>(effect.radial_render_samples));
                out.add_size(static_cast<std::size_t>(effect.radial_preview_samples));
            } break;
            case LayerPlan::EffectKindPlan::Mosaic: {
                out.add_size(effect.block_size);
            } break;
            case LayerPlan::EffectKindPlan::LensDistortion: {
                out.add_float(effect.lens_curvature);
                out.add_float(effect.lens_center[0]);
                out.add_float(effect.lens_center[1]);
                out.add_float(effect.lens_scale);
            } break;
            case LayerPlan::EffectKindPlan::LuminanceResponse: {
                out.add_float(effect.response_threshold);
                out.add_float(effect.response_softness);
                out.add_float(effect.response_radius);
                for (std::size_t c = 0; c < 4; ++c) out.add_float(effect.response_tint[c]);
                out.add_float(effect.response_intensity);
                out.add_size(static_cast<std::size_t>(effect.response_blend));
            } break;
            case LayerPlan::EffectKindPlan::FoldWarp: {
                out.add_size(static_cast<std::size_t>(effect.fold_axis));
                out.add_float(effect.fold_position);
                out.add_float(effect.fold_amount);
                out.add_float(effect.fold_radius);
                out.add_float(effect.fold_falloff);
                out.add_float(effect.fold_perspective);
            } break;
            case LayerPlan::EffectKindPlan::CreaseShading: {
                out.add_size(static_cast<std::size_t>(effect.crease_axis));
                out.add_float(effect.crease_position);
                out.add_float(effect.crease_width);
                out.add_float(effect.crease_dark_side);
                out.add_float(effect.crease_light_side);
                out.add_float(effect.crease_softness);
            } break;
            }
            add_optional(out,effect.posterize_fps,[](auto& w,float v){w.add_float(v);});
            add_optional(out,effect.time_curve,[](auto& w,const AnimationTrackPlan& track){add_track(w,track);});
        }
    }
    if (layer.backdrop_filter) {
        out.add_bool(true);
        out.add_float(layer.backdrop_filter->blur);
        out.add_float(layer.backdrop_filter->brightness);
        out.add_float(layer.backdrop_filter->saturation);
        out.add_float(layer.backdrop_filter->refraction_strength);
        if (layer.backdrop_filter->refraction_field) {
            out.add_bool(true);
            out.add_uint64(graphics::field2d_fingerprint(
                *layer.backdrop_filter->refraction_field));
        }
        out.add_float(layer.backdrop_filter->rim_strength);
        out.add_float(layer.backdrop_filter->rim_width);
        out.add_float(layer.backdrop_filter->chromatic_dispersion);
        out.add_float(layer.backdrop_filter->grain);
        out.add_float(layer.backdrop_filter->opacity);
    }
    out.add_size(layer.effect_param_tracks.size());
    for (const auto& track : layer.effect_param_tracks) {
        out.add_string(track.effect_id);
        out.add_string(track.param);
        add_track(out, track.track);
    }

    for (const auto value : layer.size) out.add_float(value);
    out.add_size(layer.size_dimensions);
    for (const auto value : layer.color) out.add_float(value);
    for (const auto value : layer.position) out.add_float(value);
    out.add_size(layer.position_dimensions);
    for (const auto value : layer.scale) out.add_float(value);
    out.add_size(layer.scale_dimensions);
    for (const auto value : layer.rotation) out.add_float(value);
    out.add_size(layer.rotation_dimensions);
    out.add_float(layer.skew_angle_deg);
    out.add_float(layer.skew_axis_deg);
    out.add_bool(layer.enable_3d);
    if (layer.casts_shadows || layer.accepts_shadows) {
        out.add_bool(true);
        add_optional(out, layer.casts_shadows,
                     [](auto& writer, bool value) { writer.add_bool(value); });
        add_optional(out, layer.accepts_shadows,
                     [](auto& writer, bool value) { writer.add_bool(value); });
    }
    out.add_bool(layer.screen_space);
    // Preserve byte-for-byte fingerprints for plans authored before safe
    // insets existed; the presence marker is only emitted for this extension.
    if (layer.safe_insets) {
        out.add_bool(true);
        for (const float inset : *layer.safe_insets) out.add_float(inset);
    }
    add_optional(out, layer.depth_role,
                 [](auto& writer, const std::string& value) { writer.add_string(value); });
    out.add_float(layer.depth_offset);

    add_optional(out, layer.radius,
                 [](auto& writer, float value) { writer.add_float(value); });

    add_optional(out, layer.start_frame,
                 [](auto& writer, Frame value) { writer.add_frame(value); });
    add_optional(out, layer.duration_frames,
                 [](auto& writer, Frame value) { writer.add_frame(value); });
    add_optional(out, layer.transition_in,
                 [](auto& writer, const TransitionPlan& value) { add_transition(writer, value); });
    add_optional(out, layer.transition_out,
                 [](auto& writer, const TransitionPlan& value) { add_transition(writer, value); });
    add_optional(out, layer.fit,
                 [](auto& writer, FitMode value) { writer.add_enum(value); });
    add_optional(out, layer.crop,
                 [](auto& writer, const ImageCropPlan& value) {
        writer.add_bool(value.enabled);
        for (const float v : value.origin) writer.add_float(v);
        for (const float v : value.size) writer.add_float(v);
        writer.add_bool(value.flip_x);
    });

    add_optional(out, layer.animation,
                 [](auto& writer, const AnimationPlan& animation) {
        writer.add_size(animation.tracks.size());
        for (const auto& track : animation.tracks) add_track(writer, track);
    });
    add_optional(out, layer.fill_gradient_animation,
                 [](auto& writer, const GradientAnimationPlan& animation) {
        add_gradient_animation(writer, animation);
    });
    out.add_size(layer.text_animators.size());
    for (const auto& animator : layer.text_animators)
        add_text_animator(out, animator);

    add_optional(out, layer.blend_mode,
                 [](auto& writer, chronon3d::BlendMode value) {
                     writer.add_enum(value);
                 });
    add_optional(out, layer.opacity,
                 [](auto& writer, float value) { writer.add_float(value); });
    out.add_bool(layer.loop);
    out.add_bool(layer.auto_orient);
}

} // namespace chronon3d::render_plan::fingerprint_detail
