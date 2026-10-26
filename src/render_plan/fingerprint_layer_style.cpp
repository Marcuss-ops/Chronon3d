#include "fingerprint_detail.hpp"

namespace chronon3d::render_plan::fingerprint_detail {

void add_layer_style_and_shape(CanonicalPlanWriter& out, const LayerPlan& layer) {
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
    if (!layer.svg_glyph_paint_ranges.empty()) {
      out.add_size(layer.svg_glyph_paint_ranges.size());
      for (const auto& range : layer.svg_glyph_paint_ranges) {
        out.add_size(range.byte_start);
        out.add_size(range.byte_end);
        out.add_float(range.color.r); out.add_float(range.color.g);
        out.add_float(range.color.b); out.add_float(range.color.a);
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
            add_optional(nested, stroke.opacity, [](auto& w, float v) { w.add_float(v); });
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
            if (stroke.miter_limit != 10.0f) {
                nested.add_bool(true);
                nested.add_float(stroke.miter_limit);
            }
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
                add_optional(writer, stroke.opacity, [](auto& nested, float value) {
                    nested.add_float(value);
                });
                add_optional(writer, stroke.solid_color, [](auto& nested, const auto& color) {
                    for (const auto component : color) nested.add_float(component);
                });
                add_optional(writer, stroke.brush, [](auto& nested,
                        const graphics::BrushProfile& brush) {
                    nested.add_uint64(brush.fingerprint());
                });
                if (stroke.cap != "butt" || stroke.join != "miter" ||
                    !stroke.dash_array.empty() || stroke.dash_offset != 0.0f ||
                    stroke.miter_limit != 10.0f) {
                    writer.add_bool(true);
                    writer.add_string(stroke.cap);
                    writer.add_string(stroke.join);
                    writer.add_size(stroke.dash_array.size());
                    for (const float dash : stroke.dash_array) writer.add_float(dash);
                    writer.add_float(stroke.dash_offset);
                    writer.add_float(stroke.miter_limit);
                }
            });
            if (subpath.fill_rule != ShapeFillRulePlan::NonZero) {
                out.add_bool(true);
                out.add_enum(subpath.fill_rule);
            }
            out.add_size(subpath.path.size());
            for (const auto& command : subpath.path) {
                out.add_enum(command.type);
                for (const auto value : command.point) out.add_float(value);
                for (const auto value : command.control1) out.add_float(value);
                for (const auto value : command.control2) out.add_float(value);
            }
            if (!subpath.svg_groups.empty() || !subpath.svg_local_masks.empty() ||
                !subpath.svg_transform_animations.empty()) {
                out.add_bool(true);
                out.add_size(subpath.svg_groups.size());
                for (const auto& group : subpath.svg_groups) {
                    out.add_uint64(group.id);
                    out.add_float(group.opacity);
                    add_optional(out, group.opacity_animation,
                        [](auto& writer, const AnimationTrackPlan& track) {
                            add_track(writer, track);
                        });
                }
                out.add_size(subpath.svg_local_masks.size());
                for (const auto& mask : subpath.svg_local_masks) {
                    out.add_size(mask.size());
                    for (const auto& command : mask) {
                        out.add_enum(command.type);
                        for (const auto value : command.point) out.add_float(value);
                        for (const auto value : command.control1) out.add_float(value);
                        for (const auto value : command.control2) out.add_float(value);
                    }
                }
                out.add_size(subpath.svg_transform_animations.size());
                for (const auto& transform : subpath.svg_transform_animations) {
                    out.add_string(transform.operation);
                    add_track(out, transform.animation);
                    if (transform.has_pivot) {
                        out.add_bool(true);
                        for (const auto value : transform.pivot) out.add_float(value);
                    }
                }
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
            shape.type == ShapeContentTypePlan::DotGrid || shape.type == ShapeContentTypePlan::Arc ||
            shape.type == ShapeContentTypePlan::RingSegment) {
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
            if (!op.target_id.empty()) {
                out.add_bool(true);
                out.add_string(op.target_id);
            }
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
            if (op.kind == PathOperatorKindPlan::Merge ||
                op.kind == PathOperatorKindPlan::BooleanUnion ||
                op.kind == PathOperatorKindPlan::BooleanDifference ||
                op.kind == PathOperatorKindPlan::BooleanIntersection ||
                op.kind == PathOperatorKindPlan::BooleanXor) {
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
            if (op.repeater_animation) {
                out.add_bool(true);
                add_track(out, *op.repeater_animation);
            }
        }
        // Element-addressed SVG animation is additive: preserve legacy fingerprints
        // for plans that do not author any bindings.
        if (!shape.svg_element_animations.empty()) {
            out.add_bool(true);
            out.add_size(shape.svg_element_animations.size());
            for (const auto& binding : shape.svg_element_animations) {
                out.add_string(binding.target_id);
                out.add_string(binding.property);
                add_track(out, binding.animation);
            }
        }
    }
}

} // namespace chronon3d::render_plan::fingerprint_detail
