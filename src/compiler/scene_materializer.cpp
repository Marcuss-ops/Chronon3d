#include <chronon3d/scene/model/shape/path_operator.hpp>
#include <cmath>
#include <string>
#include <chronon3d/compiler/scene_materializer.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/geometry/mesh.hpp>
#include <chronon3d/graphics/shape_style/fill_style.hpp>
#include <chronon3d/graphics/shape_style/stroke_style.hpp>
#include <chronon3d/graphics/shape_style/fill_style.hpp>
#include <chronon3d/graphics/shape_style/stroke_style.hpp>

#include <cmath>

namespace chronon3d::compiler {

chronon3d::Scene SceneMaterializer::materialize_scene(
    const CompiledProgram& program, std::pmr::memory_resource* resource) const {
    const auto& meta = program.metadata();
    SceneBuilder scene(resource);
    if (meta.lighting_rig) {
        const auto& source = *meta.lighting_rig;
        rendering::LightingRig rig;
        rig.key_direction = rendering::safe_normalize(Vec3{source.key_direction[0], source.key_direction[1], source.key_direction[2]});
        rig.key_color = Color{source.key_color[0], source.key_color[1], source.key_color[2], source.key_color[3]};
        rig.key_intensity = source.key_intensity;
        rig.ambient_color = Color{source.ambient_color[0], source.ambient_color[1], source.ambient_color[2], source.ambient_color[3]};
        rig.ambient_intensity = source.ambient_intensity;
        rig.rim.enabled = source.rim_enabled;
        rig.rim.color = Color{source.rim_color[0], source.rim_color[1], source.rim_color[2], source.rim_color[3]};
        rig.rim.intensity = source.rim_intensity;
        rig.rim.power = source.rim_power;
        scene.apply_lighting_rig(rig);
    }
    for (const auto& layer : program.layers()) {
        const std::string name = layer.name.empty()
            ? ("layer_" + std::to_string(layer.id)) : layer.name;
        Transform transform;
        transform.position = {layer.position[0], layer.position[1], layer.position[2]};
        transform.scale = {layer.scale[0], layer.scale[1], layer.scale[2]};
        transform.rotation = glm::quat(glm::radians(Vec3{
            layer.rotation[0], layer.rotation[1], layer.rotation[2]}));
        transform.opacity = layer.opacity;

        scene.layer(name, [&](LayerBuilder& builder) {
            builder.from(Frame{layer.start_frame})
                .duration(Frame{layer.duration_frames})
                .transform(transform)
                .opacity(layer.opacity);
            switch (layer.kind) {
                case NodeKind::Color: {
                    RectParams params;
                    params.size = layer.size_dimensions == 2
                        ? Vec2{layer.size[0], layer.size[1]}
                        : Vec2{static_cast<float>(meta.width), static_cast<float>(meta.height)};
                    params.color = {layer.color[0], layer.color[1], layer.color[2], layer.color[3]};
                    if (layer.position_dimensions < 2) {
                        builder.pin_to(Anchor::Center);
                        params.pos = {-params.size.x * 0.5f, -params.size.y * 0.5f, 0.0f};
                    }
                    builder.rect("color", std::move(params));
                    break;
                }
                case NodeKind::Image: {
                    ImageParams params;
                    params.asset_path = layer.asset;
                    params.size = layer.size_dimensions == 2
                        ? Vec2{layer.size[0], layer.size[1]}
                        : Vec2{static_cast<float>(meta.width), static_cast<float>(meta.height)};
                    params.fit = static_cast<chronon3d::FitMode>(layer.fit);
                    params.radius = layer.radius;
                    builder.image("image", std::move(params));
                    break;
                }
                case NodeKind::Video: {
                    video::VideoSource source{.path = layer.source.empty() ? layer.asset : layer.source};
                    if (layer.size_dimensions == 2) source.size = {layer.size[0], layer.size[1]};
                    if (layer.loop) source.loop_mode = video::VideoLoopMode::Loop;
                    builder.video(std::move(source));
                    break;
                }
                case NodeKind::Text:
                    builder.kind(LayerKind::Text);
                    break;
                case NodeKind::Mesh: {
                    if (!layer.inline_mesh)
                        throw std::runtime_error("mesh layer '" + layer.name + "' has no inline geometry");
                    InlineMeshParams params;
                    params.name = layer.name;
                    params.vertices.reserve(layer.inline_mesh->vertices.size());
                    for (const auto& vertex : layer.inline_mesh->vertices)
                        params.vertices.push_back(Vertex{
                            Vec3{vertex.position[0], vertex.position[1], vertex.position[2]},
                            Vec3{vertex.normal[0], vertex.normal[1], vertex.normal[2]},
                            Vec2{vertex.uv[0], vertex.uv[1]}});
                    params.indices = layer.inline_mesh->indices;
                    builder.accepts_lights(false).accepts_shadows(false);
                    builder.inline_mesh(std::move(params));
                    auto& node = builder.last_node_handle().node();
                    node.color = Color{layer.color[0], layer.color[1], layer.color[2], layer.color[3]};
                    if (layer.inline_mesh->material) {
                        const auto& source = *layer.inline_mesh->material;
                        RenderNode::InlineMeshMaterial material;
                        material.diffuse = source.diffuse;
                        material.specular = source.specular;
                        material.shininess = source.shininess;
                        material.rim_intensity = source.rim_intensity;
                        if (source.base_color)
                            material.base_color = Color{(*source.base_color)[0], (*source.base_color)[1],
                                                        (*source.base_color)[2], (*source.base_color)[3]};
                        node.inline_mesh_material = material;
                        builder.accepts_lights(true);
                    }
                    break;
                }
                case NodeKind::Shape: {
                    const auto shape = layer.shape.value_or(render_plan::ShapeContentPlan{});
                    const Vec2 size = layer.size_dimensions == 2
                        ? Vec2{layer.size[0], layer.size[1]}
                        : Vec2{static_cast<float>(meta.width), static_cast<float>(meta.height)};
                    const auto layer_color = Color{
                        layer.color[0], layer.color[1], layer.color[2], layer.color[3]};
                    auto fill_style = [&]() -> std::optional<graphics::FillStyle> {
                        if (shape.gradient) {
                            graphics::FillStyle fill;
                            fill.enabled = true;
                            fill.gradient = *shape.gradient;
                            return fill;
                        }
                        if (shape.fill_color) {
                            return graphics::FillStyle::solid(Color{
                                (*shape.fill_color)[0], (*shape.fill_color)[1],
                                (*shape.fill_color)[2], (*shape.fill_color)[3]});
                        }
                        return std::nullopt;
                    }();
                    graphics::StrokeStyle stroke;
                    if (shape.stroke) {
                        stroke.enabled = true;
                        stroke.width = shape.stroke->width.value_or(1.0f);
                        if (shape.stroke->solid_color) {
                            const auto& rgba = *shape.stroke->solid_color;
                            stroke.color = Color{rgba[0], rgba[1], rgba[2], rgba[3]};
                        }
                        if (shape.stroke->gradient) stroke.gradient = shape.stroke->gradient;
                        stroke.trim_start = shape.stroke->trim_start.value_or(0.0f);
                        stroke.trim_end = shape.stroke->trim_end.value_or(1.0f);
                    }
                    auto build_path_from_shape_plan = [&](const std::vector<render_plan::ShapePathCommandPlan>& cmds, graphics::FillStyle fill_for_path) -> PathParams {
                        PathParams pp;
                        for (const auto& command : cmds) {
                            const Vec2 point{command.point[0], command.point[1]};
                            const Vec2 control1{command.control1[0], command.control1[1]};
                            const Vec2 control2{command.control2[0], command.control2[1]};
                            switch (command.type) {
                                case render_plan::ShapePathCommandTypePlan::MoveTo: pp.commands.push_back(PathCommand::move_to(point)); break;
                                case render_plan::ShapePathCommandTypePlan::LineTo: pp.commands.push_back(PathCommand::line_to(point)); break;
                                case render_plan::ShapePathCommandTypePlan::QuadraticTo: pp.commands.push_back(PathCommand::quadratic_to(control1, point)); break;
                                case render_plan::ShapePathCommandTypePlan::CubicTo: pp.commands.push_back(PathCommand::cubic_to(control1, control2, point)); break;
                                case render_plan::ShapePathCommandTypePlan::Close: pp.commands.push_back(PathCommand::close()); pp.closed = true; break;
                            }
                        }
                        pp.color = layer_color;
                        pp.fill = fill_for_path.to_fill();
                        pp.stroke = stroke.to_path_stroke();
                        pp.pos = {0.0f, 0.0f, 0.0f};
                        return pp;
                    };
                    auto fill_for_procedural = [&]() -> graphics::FillStyle {
                        if (fill_style) return *fill_style;
                        if (shape.fill_color) return graphics::FillStyle::solid(layer_color);
                        return graphics::FillStyle::solid(layer_color);
                    };
                    auto emit_procedural = [&](PathParams pp) {
                        if (shape.operators.empty()) { builder.path("shape", std::move(pp)); return; }
                        PathShape source_path; source_path.commands = pp.commands; source_path.stroke = pp.stroke; source_path.fill = pp.fill; source_path.closed = pp.closed;
                        std::vector<PathOperatorSpec> specs;
                        for (const auto& op : shape.operators) {
                            if (op.kind == render_plan::PathOperatorKindPlan::RoundCorners) specs.push_back({PathOperatorKind::RoundCorners, RoundCornersParams{op.radius}, 0});
                            else if (op.kind == render_plan::PathOperatorKindPlan::ZigZag) specs.push_back({PathOperatorKind::ZigZag, ZigZagParams{op.size, op.points_per_unit, op.smooth}, 0});
                            else if (op.kind == render_plan::PathOperatorKindPlan::OffsetPath) specs.push_back({PathOperatorKind::OffsetPath, OffsetPathParams{op.amount, op.miter_limit}, 0});
                            else if (op.kind == render_plan::PathOperatorKindPlan::Trim) specs.push_back({PathOperatorKind::Trim, TrimPathParams{op.trim_start, op.trim_end, op.trim_offset}, 0});
                            else specs.push_back({PathOperatorKind::Repeater, RepeaterParams{op.repeater.copies, {op.repeater.position[0],op.repeater.position[1]}, {op.repeater.scale[0],op.repeater.scale[1]}, op.repeater.rotation_degrees, {op.repeater.anchor[0],op.repeater.anchor[1]}, op.repeater.opacity_decay}, 0});
                        }
                        auto expanded = apply_path_operators(source_path, specs);
                        for (size_t i=0;i<expanded.size();++i) { PathParams copy=pp; copy.commands=std::move(expanded[i].commands); copy.stroke=std::move(expanded[i].stroke); copy.fill=std::move(expanded[i].fill); copy.color.a*=expanded[i].opacity; builder.path("shape_repeater_"+std::to_string(i), std::move(copy)); }
                    };
                    // ── Procedural web primitives: Star / Polygon / Arrow / Grid / DotGrid / Arc / WorldMap / Chart ──
                    auto make_star_polygon = [&](bool is_star) {
                        const int n = shape.points.value_or(is_star ? 5 : 6);
                        const float outer = std::min(size.x, size.y) * 0.5f;
                        const float inner = is_star ? outer * std::clamp(shape.inner_radius_ratio, 0.05f, 0.95f) : outer;
                        const float rot = shape.shape_rotation_deg * 3.14159265358979f / 180.0f;
                        const Vec2 center{size.x * 0.5f, size.y * 0.5f};
                        std::vector<render_plan::ShapePathCommandPlan> cmds;
                        const int verts = is_star ? n * 2 : n;
                        for (int i=0;i<verts;++i) {
                            float angle = rot + (2.0f * 3.14159265358979f * static_cast<float>(i) / static_cast<float>(verts)) - 3.14159265358979f * 0.5f;
                            float r = (is_star && (i % 2 == 1)) ? inner : outer;
                            Vec2 pt{center.x + std::cos(angle)*r, center.y + std::sin(angle)*r};
                            render_plan::ShapePathCommandPlan cmd;
                            cmd.point = {pt.x, pt.y};
                            cmd.type = (i==0) ? render_plan::ShapePathCommandTypePlan::MoveTo : render_plan::ShapePathCommandTypePlan::LineTo;
                            cmds.push_back(cmd);
                        }
                        cmds.push_back({render_plan::ShapePathCommandTypePlan::Close});
                        graphics::FillStyle fs = fill_for_procedural();
                        auto pp = build_path_from_shape_plan(cmds, fs);
                        emit_procedural(std::move(pp));
                    };
                    auto make_arrow = [&]() {
                        const float w = size.x, h = size.y;
                        const float shaft = std::clamp(shape.arrow_shaft_width, 1.0f, std::min(w,h));
                        const float head_len = shape.arrow_head_length.value_or(w * 0.3f);
                        const float head_w = shape.arrow_head_width.value_or(w * 0.5f);
                        const std::string& dir = shape.arrow_direction;
                        // Build horizontal right-arrow in local [0,w]x[0,h], then rotate per direction
                        auto rotate_pt = [&](Vec2 p) -> Vec2 {
                            if (dir=="up") return {h - p.y, p.x};
                            if (dir=="down") return {p.y, w - p.x};
                            if (dir=="left") return {w - p.x, h - p.y};
                            if (dir=="both") return p; // both handled below
                            return p; // right
                        };
                        std::vector<Vec2> pts;
                        if (dir=="both") {
                            float hl = std::clamp(head_len, 0.0f, w*0.5f);
                            float hw = std::clamp(head_w, 0.0f, h);
                            float shaft_y0 = (h - shaft)*0.5f, shaft_y1 = (h + shaft)*0.5f;
                            float mid_y = h*0.5f;
                            pts = {{hl, mid_y - hw*0.5f},{0, mid_y},{hl, mid_y+hw*0.5f},{hl, shaft_y1},{w-hl, shaft_y1},{w-hl, mid_y+hw*0.5f},{w, mid_y},{w-hl, mid_y-hw*0.5f},{w-hl, shaft_y0},{hl, shaft_y0}};
                        } else {
                            float hl = std::clamp(head_len, 0.0f, w);
                            float hw = std::clamp(head_w, 0.0f, h);
                            float shaft_y0 = (h - shaft)*0.5f, shaft_y1 = (h + shaft)*0.5f;
                            float mid_y = h*0.5f;
                            pts = {{0, shaft_y0},{w-hl, shaft_y0},{w-hl, mid_y - hw*0.5f},{w, mid_y},{w-hl, mid_y+hw*0.5f},{w-hl, shaft_y1},{0, shaft_y1}};
                            for (auto& p: pts) p = rotate_pt(p);
                            // Fix size swap for vertical arrows: keep in [0,w]x[0,h] by remapping via w/h already used
                        }
                        std::vector<render_plan::ShapePathCommandPlan> cmds;
                        for (size_t i=0;i<pts.size();++i) { render_plan::ShapePathCommandPlan c; c.point={pts[i].x, pts[i].y}; c.type=(i==0)?render_plan::ShapePathCommandTypePlan::MoveTo:render_plan::ShapePathCommandTypePlan::LineTo; cmds.push_back(c); }
                        cmds.push_back({render_plan::ShapePathCommandTypePlan::Close});
                        graphics::FillStyle fs = fill_for_procedural();
                        auto pp = build_path_from_shape_plan(cmds, fs);
                        emit_procedural(std::move(pp));
                    };
                    auto make_grid = [&](bool dots) {
                        const float spacing = std::clamp(shape.grid_spacing, 4.0f, 512.0f);
                        graphics::FillStyle fs = fill_for_procedural();
                        if (dots) {
                            const float r = std::clamp(shape.dot_radius, 0.5f, 64.0f);
                            for (float y = spacing*0.5f; y < size.y; y += spacing) {
                                for (float x = spacing*0.5f; x < size.x; x += spacing) {
                                    CircleParams cp; cp.radius = r; cp.color = layer_color; cp.fill = fs; cp.pos = {x - r, y - r, 0.0f};
                                    builder.circle("dot_"+std::to_string(int(x))+"_"+std::to_string(int(y)), std::move(cp));
                                }
                            }
                        } else {
                            const float lw = std::clamp(shape.grid_line_width, 0.25f, 32.0f);
                            // vertical lines
                            for (float x=0; x <= size.x; x += spacing) { RectParams rp; rp.size={lw, size.y}; rp.color=layer_color; rp.fill=fs; rp.pos={x,0,0}; builder.rect("grid_v_"+std::to_string(int(x)), std::move(rp)); }
                            // horizontal lines
                            for (float y=0; y <= size.y; y += spacing) { RectParams rp; rp.size={size.x, lw}; rp.color=layer_color; rp.fill=fs; rp.pos={0,y,0}; builder.rect("grid_h_"+std::to_string(int(y)), std::move(rp)); }
                        }
                    };
                    auto make_arc = [&]() {
                        const float cx = size.x*0.5f, cy = size.y*0.5f;
                        const float radius = std::min(size.x, size.y)*0.5f;
                        const float start = shape.arc_start_deg * 3.14159265358979f / 180.0f;
                        const float sweep = shape.arc_sweep_deg * 3.14159265358979f / 180.0f;
                        const int segments = std::clamp(int(std::abs(sweep) / (3.14159f/12.0f)), 8, 96);
                        std::vector<render_plan::ShapePathCommandPlan> cmds;
                        for (int i=0;i<=segments;++i) { float t = static_cast<float>(i)/segments; float ang = start + sweep*t; Vec2 pt{cx + std::cos(ang)*radius, cy + std::sin(ang)*radius}; render_plan::ShapePathCommandPlan c; c.point={pt.x, pt.y}; c.type=(i==0)?render_plan::ShapePathCommandTypePlan::MoveTo:render_plan::ShapePathCommandTypePlan::LineTo; cmds.push_back(c); }
                        graphics::FillStyle fs; // arc is stroked
                        auto pp = build_path_from_shape_plan(cmds, fs);
                        pp.stroke.enabled = true;
                        pp.stroke.width = shape.arc_thickness > 0.0f ? shape.arc_thickness : (stroke.enabled ? stroke.width : 8.0f);
                        pp.stroke.color = layer_color;
                        if (shape.stroke && shape.stroke->solid_color) pp.stroke.color = Color{(*shape.stroke->solid_color)[0],(*shape.stroke->solid_color)[1],(*shape.stroke->solid_color)[2],(*shape.stroke->solid_color)[3]};
                        emit_procedural(std::move(pp));
                    };
                    auto make_worldmap = [&]() {
                        // Minimal deterministic world-map: filled rect + route polyline + highlight dots
                        RectParams bg; bg.size=size; bg.color=layer_color; bg.fill=fill_style; bg.stroke=stroke; builder.rect("worldmap_bg", std::move(bg));
                        if (shape.world_map && !shape.world_map->route.empty()) {
                            const auto& route = shape.world_map->route;
                            // Mercator projection of lon/lat into [0,size]
                            auto project = [&](float lon, float lat) -> Vec2 {
                                float x = (lon + 180.0f)/360.0f * size.x;
                                float lat_rad = lat * 3.14159265358979f/180.0f;
                                float merc_y = std::log(std::tan(3.14159265358979f/4.0f + lat_rad/2.0f));
                                float y = (1.0f - (merc_y / 3.14159265358979f + 1.0f)*0.5f) * size.y; // simplified
                                // center/zoom ignored for now (deterministic placeholder)
                                return {x,y};
                            };
                            std::vector<render_plan::ShapePathCommandPlan> cmds;
                            for (size_t i=0;i<route.size();++i) { Vec2 pt = project(route[i][0], route[i][1]); render_plan::ShapePathCommandPlan c; c.point={pt.x, pt.y}; c.type=(i==0)?render_plan::ShapePathCommandTypePlan::MoveTo:render_plan::ShapePathCommandTypePlan::LineTo; cmds.push_back(c); }
                            graphics::FillStyle no_fill;
                            auto pp = build_path_from_shape_plan(cmds, no_fill);
                            pp.stroke.enabled = true; pp.stroke.width = 2.0f; pp.stroke.color = Color{1,0.23f,0.19f,1};
                            emit_procedural(std::move(pp));
                        }
                    };
                    auto make_chart = [&]() {
                        if (!shape.chart || shape.chart->data.empty()) return;
                        const auto& data = shape.chart->data;
                        const std::string& ctype = shape.chart->chart_type;
                        graphics::FillStyle fs = fill_for_procedural();
                        if (ctype=="bar") {
                            float gap = std::clamp(shape.chart->bar_gap, 0.0f, 0.5f);
                            float n = static_cast<float>(data.size());
                            float bar_w = size.x / n * (1.0f - gap);
                            float step = size.x / n;
                            float max_v = 0.0f; for(float v: data) max_v = std::max(max_v, std::abs(v)); if(max_v==0) max_v=1;
                            for (size_t i=0;i<data.size();++i) { float h = std::abs(data[i])/max_v * size.y; RectParams rp; rp.size={bar_w, h}; rp.color=layer_color; rp.fill=fs; if(i<shape.chart->colors.size()){ auto& c=shape.chart->colors[i]; rp.color={c[0],c[1],c[2],c[3]}; rp.fill=graphics::FillStyle::solid(rp.color);} rp.pos={step*float(i)+(step-bar_w)*0.5f, size.y - h, 0}; builder.rect("chart_bar_"+std::to_string(i), std::move(rp)); }
                        } else if (ctype=="line") {
                            float max_v=0; for(float v:data) max_v=std::max(max_v,std::abs(v)); if(max_v==0) max_v=1;
                            std::vector<render_plan::ShapePathCommandPlan> cmds;
                            for(size_t i=0;i<data.size();++i){ float x = (data.size()==1? size.x*0.5f : float(i)/float(data.size()-1)*size.x); float y = size.y - std::abs(data[i])/max_v*size.y; render_plan::ShapePathCommandPlan c; c.point={x,y}; c.type=(i==0)?render_plan::ShapePathCommandTypePlan::MoveTo:render_plan::ShapePathCommandTypePlan::LineTo; cmds.push_back(c); }
                            graphics::FillStyle no_fill;
                            auto pp = build_path_from_shape_plan(cmds, no_fill);
                            pp.stroke.enabled=true; pp.stroke.width=shape.chart->line_width; pp.stroke.color=layer_color;
                            emit_procedural(std::move(pp));
                        } else if (ctype=="pie") {
                            float total=0; for(float v:data) total+=std::abs(v); if(total==0) total=1;
                            float cx=size.x*0.5f, cy=size.y*0.5f, r=std::min(size.x,size.y)*0.5f;
                            float start=0;
                            for(size_t i=0;i<data.size();++i){ float sweep = std::abs(data[i])/total*360.0f; if(sweep<1) { start+=sweep; continue; } float mid = (start+sweep*0.5f)*3.14159265f/180.0f; Vec2 p0{cx,cy}; Vec2 p1{cx+std::cos(start*3.14159265f/180.0f)*r, cy+std::sin(start*3.14159265f/180.0f)*r}; Vec2 p2{cx+std::cos((start+sweep)*3.14159265f/180.0f)*r, cy+std::sin((start+sweep)*3.14159265f/180.0f)*r}; (void)mid; std::vector<render_plan::ShapePathCommandPlan> cmds; render_plan::ShapePathCommandPlan m; m.type=render_plan::ShapePathCommandTypePlan::MoveTo; m.point={p0.x,p0.y}; cmds.push_back(m); render_plan::ShapePathCommandPlan l1; l1.type=render_plan::ShapePathCommandTypePlan::LineTo; l1.point={p1.x,p1.y}; cmds.push_back(l1); render_plan::ShapePathCommandPlan l2; l2.type=render_plan::ShapePathCommandTypePlan::LineTo; l2.point={p2.x,p2.y}; cmds.push_back(l2); cmds.push_back({render_plan::ShapePathCommandTypePlan::Close}); graphics::FillStyle sfs=fs; if(i<shape.chart->colors.size()){ auto& c=shape.chart->colors[i]; Color col{c[0],c[1],c[2],c[3]}; sfs=graphics::FillStyle::solid(col); } auto pp=build_path_from_shape_plan(cmds, sfs); builder.path("chart_pie_"+std::to_string(i), std::move(pp)); start+=sweep; }
                        }
                    };
                    // Dispatch new shape types before falling through to legacy rect handling
                    if (shape.type == render_plan::ShapeContentTypePlan::Star) { make_star_polygon(true); break; }
                    if (shape.type == render_plan::ShapeContentTypePlan::Polygon) { make_star_polygon(false); break; }
                    if (shape.type == render_plan::ShapeContentTypePlan::Arrow) { make_arrow(); break; }
                    if (shape.type == render_plan::ShapeContentTypePlan::Grid) { make_grid(false); break; }
                    if (shape.type == render_plan::ShapeContentTypePlan::DotGrid) { make_grid(true); break; }
                    if (shape.type == render_plan::ShapeContentTypePlan::Arc) { make_arc(); break; }
                    if (shape.type == render_plan::ShapeContentTypePlan::WorldMap) { make_worldmap(); break; }
                    if (shape.type == render_plan::ShapeContentTypePlan::Chart) { make_chart(); break; }
                    // additive device_frame/counter: emit chrome rect + keep primary shape
                    if (shape.device_frame) {
                        RoundedRectParams chrome_rr; chrome_rr.size=size; chrome_rr.radius=shape.device_frame->radius; chrome_rr.color=layer_color; chrome_rr.fill=fill_style; chrome_rr.stroke=stroke;
                        builder.rounded_rect("device_frame", std::move(chrome_rr));
                        // don't break — still emit primary shape payload below if it was also Star etc (already returned). For plain rect + device_frame, fall through to rect.
                    }
                    switch (shape.type) {
                        case render_plan::ShapeContentTypePlan::Rect: {
                            RectParams params;
                            params.size = size;
                            params.color = layer_color;
                            params.fill = fill_style;
                            params.stroke = stroke;
                            builder.rect("shape", std::move(params));
                            break;
                        }
                        case render_plan::ShapeContentTypePlan::RoundedRect: {
                            RoundedRectParams params;
                            params.size = size;
                            params.radius = shape.radius;
                            params.color = layer_color;
                            params.fill = fill_style;
                            params.stroke = stroke;
                            builder.rounded_rect("shape", std::move(params));
                            break;
                        }
                        case render_plan::ShapeContentTypePlan::Ellipse: {
                            CircleParams params;
                            params.radius = std::min(size.x, size.y) * 0.5f;
                            params.color = layer_color;
                            params.fill = fill_style;
                            params.stroke = stroke;
                            builder.circle("shape", std::move(params));
                            break;
                        }
                        case render_plan::ShapeContentTypePlan::Line:
                        case render_plan::ShapeContentTypePlan::Path: {
                            PathParams params;
                            for (const auto& command : shape.path) {
                                const Vec2 point{command.point[0], command.point[1]};
                                const Vec2 control1{command.control1[0], command.control1[1]};
                                const Vec2 control2{command.control2[0], command.control2[1]};
                                switch (command.type) {
                                    case render_plan::ShapePathCommandTypePlan::MoveTo:
                                        params.commands.push_back(PathCommand::move_to(point)); break;
                                    case render_plan::ShapePathCommandTypePlan::LineTo:
                                        params.commands.push_back(PathCommand::line_to(point)); break;
                                    case render_plan::ShapePathCommandTypePlan::QuadraticTo:
                                        params.commands.push_back(PathCommand::quadratic_to(control1, point)); break;
                                    case render_plan::ShapePathCommandTypePlan::CubicTo:
                                        params.commands.push_back(PathCommand::cubic_to(control1, control2, point)); break;
                                    case render_plan::ShapePathCommandTypePlan::Close:
                                        params.commands.push_back(PathCommand::close());
                                        params.closed = true;
                                        break;
                                }
                            }
                            if (shape.gradient) {
                                params.fill.enabled = true;
                                params.fill.type = fill_type_for_gradient(shape.gradient->type);
                                params.fill.gradient = *shape.gradient;
                            } else if (shape.fill_color) {
                                params.fill = Fill::solid_color(Color{
                                    (*shape.fill_color)[0], (*shape.fill_color)[1],
                                    (*shape.fill_color)[2], (*shape.fill_color)[3]});
                            }
                            params.color = layer_color;
                            params.stroke = stroke.to_path_stroke();
                            if (shape.type == render_plan::ShapeContentTypePlan::Line &&
                                params.stroke.width <= 1.0f) params.stroke.width = 1.0f;
                            params.pos = {0.0f, 0.0f, 0.0f};
                            if (shape.operators.empty()) {
                                builder.path("shape", std::move(params));
                            } else {
                                PathShape source_path;
                                source_path.commands = params.commands;
                                source_path.stroke = params.stroke;
                                source_path.fill = params.fill;
                                source_path.closed = params.closed;
                                std::vector<PathOperatorSpec> specs;
                                for (const auto& op : shape.operators) {
                                    if (op.kind == render_plan::PathOperatorKindPlan::RoundCorners) {
                                        specs.push_back({PathOperatorKind::RoundCorners,
                                            RoundCornersParams{op.radius}, 0});
                                    } else if (op.kind == render_plan::PathOperatorKindPlan::ZigZag) {
                                        specs.push_back({PathOperatorKind::ZigZag,
                                            ZigZagParams{op.size, op.points_per_unit, op.smooth}, 0});
                                    } else if (op.kind == render_plan::PathOperatorKindPlan::OffsetPath) {
                                        specs.push_back({PathOperatorKind::OffsetPath,
                                            OffsetPathParams{op.amount, op.miter_limit}, 0});
                                    } else if (op.kind == render_plan::PathOperatorKindPlan::PuckerBloat) {
                                        specs.push_back({PathOperatorKind::PuckerBloat,
                                            PuckerBloatParams{op.amount, {op.anchor[0], op.anchor[1]}}, 0});
                                    } else if (op.kind == render_plan::PathOperatorKindPlan::Twist) {
                                        specs.push_back({PathOperatorKind::Twist,
                                            TwistParams{op.degrees, {op.center[0], op.center[1]}}, 0});
                                    } else if (op.kind == render_plan::PathOperatorKindPlan::WigglePath) {
                                        specs.push_back({PathOperatorKind::WigglePath,
                                            WigglePathParams{op.amount, op.frequency}, op.seed});
                                    } else if (op.kind == render_plan::PathOperatorKindPlan::Merge) {
                                        MergeParams merge;
                                        for (const auto& planned_path : op.paths) {
                                            std::vector<PathCommand> subpath;
                                            for (const auto& command : planned_path) {
                                                const Vec2 point{command.point[0], command.point[1]};
                                                const Vec2 c1{command.control1[0], command.control1[1]};
                                                const Vec2 c2{command.control2[0], command.control2[1]};
                                                switch (command.type) {
                                                    case render_plan::ShapePathCommandTypePlan::MoveTo: subpath.push_back(PathCommand::move_to(point)); break;
                                                    case render_plan::ShapePathCommandTypePlan::LineTo: subpath.push_back(PathCommand::line_to(point)); break;
                                                    case render_plan::ShapePathCommandTypePlan::QuadraticTo: subpath.push_back(PathCommand::quadratic_to(c1, point)); break;
                                                    case render_plan::ShapePathCommandTypePlan::CubicTo: subpath.push_back(PathCommand::cubic_to(c1, c2, point)); break;
                                                    case render_plan::ShapePathCommandTypePlan::Close: subpath.push_back(PathCommand::close()); break;
                                                }
                                            }
                                            merge.paths.push_back(std::move(subpath));
                                        }
                                        specs.push_back({PathOperatorKind::Merge, std::move(merge), 0});
                                    } else if (op.kind == render_plan::PathOperatorKindPlan::Trim) {
                                        specs.push_back({PathOperatorKind::Trim,
                                            TrimPathParams{op.trim_start, op.trim_end, op.trim_offset}, 0});
                                    } else {
                                        const auto& repeater = op.repeater;
                                        RepeaterParams rp;
                                        rp.copies = repeater.copies;
                                        rp.position = {repeater.position[0], repeater.position[1]};
                                        rp.scale = {repeater.scale[0], repeater.scale[1]};
                                        rp.rotation_degrees = repeater.rotation_degrees;
                                        rp.anchor = {repeater.anchor[0], repeater.anchor[1]};
                                        rp.opacity_decay = repeater.opacity_decay;
                                        specs.push_back({PathOperatorKind::Repeater, rp, 0});
                                    }
                                }
                                auto expanded = apply_path_operators(source_path, specs);
                                for (std::size_t i = 0; i < expanded.size(); ++i) {
                                    PathParams copy = params;
                                    copy.commands = std::move(expanded[i].commands);
                                    copy.stroke = std::move(expanded[i].stroke);
                                    copy.fill = std::move(expanded[i].fill);
                                    copy.color.a *= expanded[i].opacity;
                                    builder.path("shape_repeater_" + std::to_string(i), std::move(copy));
                                }
                            }
                            break;
                        }
                    }
                    break;
                }
                case NodeKind::Unknown:
                    break;
            }
        });
    }
    return scene.build();
}

}  // namespace chronon3d::compiler
