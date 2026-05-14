#include <chronon3d/renderer/software/software_renderer.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/scene/scene.hpp>
#include <chronon3d/math/camera_2_5d_projection.hpp>
#include <chronon3d/core/profiling.hpp>
#include "primitive_renderer.hpp"
#include "render_graph_builder.hpp"
#include "render_effects_processor.hpp"
#include <stb_truetype.h>   // implementation compiled in text_renderer.cpp
#include <mapbox/earcut.hpp>
#include <fmt/format.h>
#include <algorithm>
#include <cmath>
#include <vector>

namespace mapbox { namespace util {
    template <> struct nth<0, chronon3d::Vec2> {
        inline static float get(const chronon3d::Vec2& v) { return v.x; }
    };
    template <> struct nth<1, chronon3d::Vec2> {
        inline static float get(const chronon3d::Vec2& v) { return v.y; }
    };
}}

namespace chronon3d {

using namespace renderer;

// ---------------------------------------------------------------------------
// SoftwareRenderer - Main Interface
// ---------------------------------------------------------------------------

static std::unique_ptr<Framebuffer> downsample_fb(const Framebuffer& src, int dst_w, int dst_h) {
    auto dst = std::make_unique<Framebuffer>(dst_w, dst_h);
    const float sx = static_cast<float>(src.width()) / static_cast<float>(dst_w);
    const float sy = static_cast<float>(src.height()) / static_cast<float>(dst_h);

    for (int y = 0; y < dst_h; ++y) {
        for (int x = 0; x < dst_w; ++x) {
            float r = 0, g = 0, b = 0, a = 0;
            int count = 0;
            int x0 = static_cast<int>(x * sx);
            int y0 = static_cast<int>(y * sy);
            int x1 = std::min(static_cast<int>((x + 1) * sx), src.width());
            int y1 = std::min(static_cast<int>((y + 1) * sy), src.height());

            for (int sy_i = y0; sy_i < y1; ++sy_i) {
                for (int sx_i = x0; sx_i < x1; ++sx_i) {
                    Color c = src.get_pixel(sx_i, sy_i);
                    r += c.r; g += c.g; b += c.b; a += c.a;
                    count++;
                }
            }
            if (count > 0) {
                float inv = 1.0f / count;
                dst->set_pixel(x, y, Color{r * inv, g * inv, b * inv, a * inv});
            }
        }
    }
    return dst;
}

std::unique_ptr<Framebuffer> SoftwareRenderer::render_frame(const Composition& comp, Frame frame) {
    const float ssaa = std::max(1.0f, m_settings.ssaa_factor);
    const int   w    = comp.width();
    const int   h    = comp.height();
    const int   rw   = static_cast<int>(w * ssaa);
    const int   rh   = static_cast<int>(h * ssaa);

    std::unique_ptr<Framebuffer> render_fb;

    if (!m_settings.motion_blur.enabled || m_settings.motion_blur.samples <= 1) {
        Scene scene = comp.evaluate(frame);
        render_fb = render_scene_internal(scene, comp.camera, rw, rh, frame, 0.0f);
    } else {
        const int   N       = std::max(2, m_settings.motion_blur.samples);
        const float shutter = m_settings.motion_blur.shutter_angle / 360.0f;

        std::vector<float> accum(static_cast<size_t>(rw * rh * 4), 0.0f);
        const float weight = 1.0f / static_cast<float>(N);

        for (int s = 0; s < N; ++s) {
            const float t = (static_cast<float>(s) / static_cast<float>(N)) * shutter;
            Scene sub_scene = comp.evaluate(frame, t);
            auto sub_fb = render_scene_internal(sub_scene, comp.camera, rw, rh, frame, t);

            for (int y = 0; y < rh; ++y) {
                for (int x = 0; x < rw; ++x) {
                    Color c = sub_fb->get_pixel(x, y);
                    const size_t idx = static_cast<size_t>((y * rw + x) * 4);
                    accum[idx + 0] += c.r * weight;
                    accum[idx + 1] += c.g * weight;
                    accum[idx + 2] += c.b * weight;
                    accum[idx + 3] += c.a * weight;
                }
            }
        }

        render_fb = std::make_unique<Framebuffer>(rw, rh);
        for (int y = 0; y < rh; ++y) {
            for (int x = 0; x < rw; ++x) {
                const size_t idx = static_cast<size_t>((y * rw + x) * 4);
                render_fb->set_pixel(x, y, Color{accum[idx], accum[idx+1], accum[idx+2], accum[idx+3]});
            }
        }
    }

    if (ssaa > 1.0f) {
        return downsample_fb(*render_fb, w, h);
    }
    return render_fb;
}


std::unique_ptr<Framebuffer> SoftwareRenderer::render_scene(
    const Scene& scene, const Camera& camera, i32 width, i32 height, Frame frame)
{
    return render_scene_internal(scene, camera, width, height, frame, 0.0f);
}

std::string SoftwareRenderer::debug_render_graph(const Scene& scene, const Camera& camera,
                                                 i32 width, i32 height, Frame frame,
                                                 f32 frame_time) const
{
    auto graph = build_render_graph(scene, camera, width, height, frame, frame_time);
    return graph.debug_dot();
}

std::unique_ptr<Framebuffer> SoftwareRenderer::render_scene_internal(
    const Scene& scene, const Camera& camera, i32 width, i32 height, Frame frame, f32 frame_time)
{
    ZoneScoped;

    if (m_settings.use_modular_graph) {
        graph::RenderGraphContext ctx;
        ctx.frame = frame;
        ctx.time_seconds = frame_time;
        ctx.width = width;
        ctx.height = height;
        ctx.camera = camera;
        ctx.renderer = this;
        ctx.node_cache = &m_node_cache;
        ctx.cache_enabled = true;
        ctx.diagnostics_enabled = m_settings.diagnostic;
        ctx.registry = m_registry;
        ctx.video_decoder = &m_video_extractor;
        ctx.ssaa_factor = m_settings.ssaa_factor;

        auto graph = graph::GraphBuilder::build(scene, ctx);
        graph::GraphExecutor executor;
        auto result_shared = executor.execute(graph, ctx);

        if (!result_shared) return nullptr;
        return std::make_unique<Framebuffer>(*result_shared);
    }

    auto graph_wrapper = build_render_graph(scene, camera, width, height, frame, frame_time);

    rendergraph::RenderGraphExecutionContext ctx{
        .renderer = *this,
        .camera = camera,
        .frame = frame,
        .width = width,
        .height = height,
        .diagnostic = m_settings.diagnostic,
    };

    return graph_wrapper.execute(ctx);
}

rendergraph::RenderGraph SoftwareRenderer::build_render_graph(
    const Scene& scene, const Camera& camera, i32 width, i32 height, Frame frame, f32 frame_time) const
{
    return build_software_render_graph(*this, scene, camera, width, height, frame, frame_time);
}

// ---------------------------------------------------------------------------
// Node Drawing Dispatch
// ---------------------------------------------------------------------------

void SoftwareRenderer::draw_node(Framebuffer& fb, const RenderNode& node, const RenderState& state, 
                                 const Camera& camera, i32 width, i32 height) {
    const Color linear_color = node.color.to_linear();
    const Mat4& model = state.matrix;
    const f32 opacity = state.opacity;

    // Node-level effects
    if (node.shadow.enabled) renderer::draw_shadow(fb, node, state);
    if (node.glow.enabled)   renderer::draw_glow(fb, node, state);

    if (node.shape.type == ShapeType::Mesh) {
        if (node.mesh) {
            const f32 aspect = static_cast<f32>(width) / static_cast<f32>(height);
            renderer::render_mesh_wireframe(fb, *node.mesh, model, camera.view_matrix(), 
                                           camera.projection_matrix(aspect), linear_color);
        }
        return;
    }

    if (node.shape.type == ShapeType::Line) {
        Vec4 p0 = model * Vec4(0, 0, 0, 1);
        Vec4 p1 = model * Vec4(node.shape.line.to, 1);
        Color col = linear_color;
        col.a *= opacity;
        draw_line(fb, Vec3(p0.x, p0.y, p0.z), Vec3(p1.x, p1.y, p1.z), col);
        return;
    }

    if (node.shape.type == ShapeType::Text) {
        Transform text_tr;
        text_tr.position = Vec3(model[3]);
        text_tr.opacity  = opacity;
        text_tr.scale.x  = glm::length(Vec3(model[0]));
        text_tr.scale.y  = glm::length(Vec3(model[1]));
        m_text_renderer.draw_text(node.shape.text, text_tr, fb, &state);
        return;
    }

    if (node.shape.type == ShapeType::Image) {
        m_image_renderer.draw_image(node.shape.image, state, fb);
        return;
    }

    if (node.shape.type == ShapeType::FakeExtrudedText) {
        const auto& s = node.shape.fake_extruded_text;
        const f32 op = state.opacity;

        // ── SCREEN-SPACE FALLBACK (no camera) ─────────────────────────────────
        // Used by FakeExtrudedTextProof comparison without 3D camera.
        if (!s.cam_ready) {
            const Vec2 front_sp{s.world_pos.x, s.world_pos.y};
            Vec2 dir{s.extrude_dir.x, s.extrude_dir.y};
            const f32 dir_len = std::sqrt(dir.x*dir.x + dir.y*dir.y);

            Framebuffer front_mask(fb.width(), fb.height());
            front_mask.clear(Color::transparent());
            {
                TextShape ts; ts.text=s.text; ts.style.font_path=s.font_path;
                ts.style.size=s.font_size; ts.style.color=Color{1,1,1,1};
                ts.style.align=s.align;
                Transform tr; tr.position=Vec3(front_sp.x,front_sp.y,0);
                m_text_renderer.draw_text(ts,tr,front_mask);
            }
            if (dir_len > 0.1f && s.depth > 0) {
                const f32 inv_d = 1.0f/static_cast<f32>(s.depth);
                for (int y=0; y<fb.height(); ++y) {
                    for (int x=0; x<fb.width(); ++x) {
                        if (front_mask.get_pixel(x,y).a > 0.05f) continue;
                        bool found=false; f32 dist=0;
                        for (int st=1; st<=s.depth; ++st) {
                            int sx=x-static_cast<int>(dir.x*st+0.5f);
                            int sy=y-static_cast<int>(dir.y*st+0.5f);
                            if (sx<0||sx>=fb.width()||sy<0||sy>=fb.height()) break;
                            if (front_mask.get_pixel(sx,sy).a > 0.05f) { dist=st*inv_d; found=true; break; }
                        }
                        if (found) {
                            Color sc=s.side_color;
                            sc.r=std::max(0.0f,sc.r-0.30f*dist); sc.g=std::max(0.0f,sc.g-0.30f*dist);
                            sc.b=std::max(0.0f,sc.b-0.30f*dist); sc.a=s.side_color.a*(1.0f-s.side_fade*dist)*op;
                            fb.set_pixel(x,y,compositor::blend(sc,fb.get_pixel(x,y),BlendMode::Normal));
                        }
                    }
                }
            }
            { TextShape ts; ts.text=s.text; ts.style.font_path=s.font_path;
              ts.style.size=s.font_size; ts.style.color=Color{s.front_color.r,s.front_color.g,s.front_color.b,s.front_color.a*op};
              ts.style.align=s.align; Transform tr; tr.position=Vec3(front_sp.x,front_sp.y,0); tr.opacity=op;
              m_text_renderer.draw_text(ts,tr,fb,&state); }
            if (s.highlight_opacity>0) {
                TextShape ts; ts.text=s.text; ts.style.font_path=s.font_path;
                ts.style.size=s.font_size; ts.style.color=Color{1,1,1,s.highlight_opacity*op};
                ts.style.align=s.align; Transform tr; tr.position=Vec3(front_sp.x,front_sp.y-1,0);
                m_text_renderer.draw_text(ts,tr,fb,&state);
            }
            return;
        }

        // ── MESH-BASED 3D EXTRUSION ────────────────────────────────────────────
        const CachedFont* font_entry = m_text_renderer.get_font(s.font_path);
        if (!font_entry) return;
        stbtt_fontinfo font;
        if (!stbtt_InitFont(&font, font_entry->data.data(), 0)) return;

        const float sc_f = stbtt_ScaleForPixelHeight(&font, s.font_size);
        int ascent, descent, line_gap;
        stbtt_GetFontVMetrics(&font, &ascent, &descent, &line_gap);

        float total_w = 0;
        for (char ch : s.text) {
            int adv, lsb;
            stbtt_GetCodepointHMetrics(&font, (int)(unsigned char)ch, &adv, &lsb);
            total_w += (float)adv * sc_f;
        }
        float x_cur_start = (s.align == TextAlign::Center) ? -total_w * 0.5f :
                            (s.align == TextAlign::Right)   ? -total_w : 0.0f;
        float x_cur = x_cur_start;

        struct SideQ { Vec2 v[4]; Color c0, c1; float depth; };
        struct Tri3D { Vec2 v[3]; Color color; float depth; };
        std::vector<SideQ> quads;
        std::vector<Tri3D> triangles;

        const float depth_z = (float)s.depth * s.extrude_z_step;

        for (char ch : s.text) {
            stbtt_vertex* verts = nullptr;
            int nv = stbtt_GetCodepointShape(&font, (int)(unsigned char)ch, &verts);
            if (nv > 0 && verts) {
                std::vector<std::vector<Vec2>> contours;
                for (int vi = 0; vi < nv; ++vi) {
                    const auto& v = verts[vi];
                    if (v.type == STBTT_vmove) {
                        contours.emplace_back();
                        contours.back().push_back({(float)v.x * sc_f, (float)v.y * sc_f});
                    } else if (v.type == STBTT_vline) {
                        if (!contours.empty()) contours.back().push_back({(float)v.x * sc_f, (float)v.y * sc_f});
                    } else if (v.type == STBTT_vcurve) {
                        if (contours.empty()) continue;
                        Vec2 p0 = contours.back().back();
                        Vec2 cp{(float)v.cx * sc_f, (float)v.cy * sc_f};
                        Vec2 p1{(float)v.x  * sc_f, (float)v.y  * sc_f};
                        for (int step = 1; step <= 12; ++step) {
                            float t = (float)step / 12.0f, mt = 1.0f - t;
                            contours.back().push_back({mt*mt*p0.x + 2*mt*t*cp.x + t*t*p1.x, mt*mt*p0.y + 2*mt*t*cp.y + t*t*p1.y});
                        }
                    } else if (v.type == STBTT_vcubic) {
                        if (contours.empty()) continue;
                        Vec2 p0 = contours.back().back();
                        Vec2 c1{(float)v.cx  * sc_f, (float)v.cy  * sc_f};
                        Vec2 c2{(float)v.cx1 * sc_f, (float)v.cy1 * sc_f};
                        Vec2 p1{(float)v.x   * sc_f, (float)v.y   * sc_f};
                        for (int step = 1; step <= 16; ++step) {
                            float t = (float)step / 16.0f, mt = 1.0f - t;
                            contours.back().push_back({mt*mt*mt*p0.x + 3*mt*mt*t*c1.x + 3*mt*t*t*c2.x + t*t*t*p1.x,
                                                       mt*mt*mt*p0.y + 3*mt*mt*t*c1.y + 3*mt*t*t*c2.y + t*t*t*p1.y});
                        }
                    }
                }

                auto get_area = [](const std::vector<Vec2>& c) {
                    float a = 0;
                    for (size_t i = 0; i < c.size(); ++i) {
                        size_t j = (i + 1) % c.size();
                        a += c[i].x * c[j].y - c[j].x * c[i].y;
                    }
                    return a * 0.5f;
                };

                // Group contours into islands (outer ring + its holes)
                struct Island {
                    std::vector<std::vector<Vec2>> polygon;
                    float area;
                };
                std::vector<Island> islands;
                for (auto& c : contours) {
                    if (c.size() < 3) continue;
                    float a = get_area(c);
                    // stbtt: positive area is CW (outer in Y-down), negative CCW (hole)
                    // But we want to be robust. Let's use the fact that holes are inside outer rings.
                    if (a > 0) { // Outer ring
                        islands.push_back({ {std::move(c)}, a });
                    } else { // Hole
                        bool placed = false;
                        for (auto& island : islands) {
                            // Simple heuristic: if the first point is inside the outer ring, it's a hole.
                            // For speed, we just append to the last island if it's a hole (common in stbtt).
                            island.polygon.push_back(std::move(c));
                            placed = true;
                            break;
                        }
                        if (!placed) {
                            // If no island yet, treat hole as outer ring (shouldn't happen with well-formed fonts)
                            islands.push_back({ {std::move(c)}, a });
                        }
                    }
                }

                auto transform_pt = [&](Vec2 p, float z) {
                    // Local text space: x is character advance, y is glyph coords (stbtt uses Y-up internally? No, Y-down in our sc_f scaling)
                    // Actually stbtt Y is UP, so we negate it for our screen space.
                    Vec4 local{x_cur + p.x, -p.y, z, 1.0f};
                    Vec4 world = state.matrix * local;
                    return Vec3(world.x, world.y, world.z);
                };

                auto proj3 = [&](const Vec3& w, bool& ok) {
                    Vec4 p = camera.view_matrix() * Vec4(w, 1.0f);
                    if (p.z > -1.0f) { ok = false; return Vec2{0, 0}; }
                    Vec4 clip = camera.projection_matrix((float)width / height) * p;
                    ok = true;
                    float inv_w = 1.0f / clip.w;
                    return Vec2{(clip.x * inv_w + 1.0f) * width * 0.5f, (1.0f - clip.y * inv_w) * height * 0.5f};
                };

                for (auto& island : islands) {
                    std::vector<uint32_t> indices = mapbox::earcut<uint32_t>(island.polygon);
                    std::vector<Vec2> flat_verts;
                    for (const auto& c : island.polygon) flat_verts.insert(flat_verts.end(), c.begin(), c.end());

                    for (size_t i = 0; i < indices.size(); i += 3) {
                        Vec3 w_front[3], w_back[3];
                        Vec2 s_front[3], s_back[3];
                        bool ok_f[3], ok_b[3];
                        float d_f = 0, d_b = 0;

                        for (int j = 0; j < 3; ++j) {
                            w_front[j] = transform_pt(flat_verts[indices[i + j]], 0);
                            w_back[j]  = transform_pt(flat_verts[indices[i + (2-j)]], depth_z);
                            s_front[j] = proj3(w_front[j], ok_f[j]);
                            s_back[j]  = proj3(w_back[j], ok_b[j]);
                            d_f += -(camera.view_matrix() * Vec4(w_front[j], 1.0f)).z;
                            d_b += -(camera.view_matrix() * Vec4(w_back[j], 1.0f)).z;
                        }

                        if (ok_f[0] && ok_f[1] && ok_f[2]) {
                            triangles.push_back({{s_front[0], s_front[1], s_front[2]}, s.front_color.with_alpha(s.front_color.a * op), d_f / 3.0f});
                        }
                        if (ok_b[0] && ok_b[1] && ok_b[2]) {
                            triangles.push_back({{s_back[0], s_back[1], s_back[2]}, s.side_color.with_alpha(s.side_color.a * 0.5f * op), d_b / 3.0f});
                        }
                    }

                    // Side quads
                    for (const auto& contour : island.polygon) {
                        for (size_t i = 0; i < contour.size(); ++i) {
                            Vec2 p0 = contour[i];
                            Vec2 p1 = contour[(i + 1) % contour.size()];
                            
                            Vec2 mid = (p0 + p1) * 0.5f;
                            float d = -(camera.view_matrix() * Vec4(transform_pt(mid, depth_z * 0.5f), 1.0f)).z;

                            auto add_quad = [&](float z0, float z1, Color c0, Color c1) {
                                Vec3 w00 = transform_pt(p0, z0), w10 = transform_pt(p1, z0);
                                Vec3 w01 = transform_pt(p0, z1), w11 = transform_pt(p1, z1);
                                bool ok[4]; Vec2 sv[4];
                                sv[0] = proj3(w00, ok[0]); sv[1] = proj3(w10, ok[1]);
                                sv[2] = proj3(w11, ok[2]); sv[3] = proj3(w01, ok[3]);
                                if (ok[0] && ok[1] && ok[2] && ok[3]) {
                                    quads.push_back({{sv[0], sv[1], sv[2], sv[3]}, c0, c1, d});
                                }
                            };

                            Vec2 edge = p1 - p0;
                            Vec2 norm = {-edge.y, edge.x};
                            if (norm.x*norm.x + norm.y*norm.y > 1e-6f) norm = norm / std::sqrt(norm.x*norm.x + norm.y*norm.y);
                            float shade = std::clamp(0.5f + 0.5f * norm.x, 0.0f, 1.0f);
                            Color c_side = s.side_color.with_alpha(s.side_color.a * op);
                            Color c0 = Color(c_side.r * shade, c_side.g * shade, c_side.b * shade, c_side.a);
                            Color c1 = c0;

                            if (s.bevel_size > 0.01f) {
                                Color rim_c = lerp(s.front_color, Color::white(), 0.3f).with_alpha(s.side_color.a * op);
                                add_quad(0.0f, s.bevel_size, rim_c, rim_c);
                                add_quad(s.bevel_size, depth_z, c0, c1);
                            } else {
                                add_quad(0.0f, depth_z, c0, c1);
                            }
                        }
                    }
                }
                stbtt_FreeShape(&font, verts);
            }
            int adv, lsb;
            stbtt_GetCodepointHMetrics(&font, (int)(unsigned char)ch, &adv, &lsb);
            x_cur += (float)adv * sc_f;
        }

        struct RenderPrimitive { enum { Quad, Tri } type; int index; float depth; };
        std::vector<RenderPrimitive> sorted;
        sorted.reserve(quads.size() + triangles.size());
        for (size_t i = 0; i < quads.size(); ++i) sorted.push_back({RenderPrimitive::Quad, (int)i, quads[i].depth});
        for (size_t i = 0; i < triangles.size(); ++i) sorted.push_back({RenderPrimitive::Tri, (int)i, triangles[i].depth});
        std::stable_sort(sorted.begin(), sorted.end(), [](const RenderPrimitive& a, const RenderPrimitive& b){ return a.depth > b.depth; });

        for (const auto& p : sorted) {
            if (p.type == RenderPrimitive::Quad) {
                const auto& q = quads[p.index];
                Color gc[4] = {q.c0, q.c1, q.c1, q.c0};
                renderer::fill_gradient_quad(fb, q.v, gc);
            } else {
                const auto& t = triangles[p.index];
                renderer::fill_triangle(fb, t.v, t.color);
            }
        }
        return;
    }

    if (node.shape.type == ShapeType::FakeBox3D) {
        auto s = node.shape.fake_box3d;
        if (!s.cam_ready) {
            // Fallback: use legacy camera (only hit when called outside the 2.5D render path)
            s.cam_ready = true;
            s.cam_view  = camera.view_matrix();
            const f32 fw = static_cast<f32>(width);
            s.cam_focal = camera.focal_length(fw);
            s.vp_cx     = fw * 0.5f;
            s.vp_cy     = static_cast<f32>(height) * 0.5f;
        }
        renderer::draw_fake_box3d(fb, node, state, s);
        return;
    }

    if (node.shape.type == ShapeType::GridPlane) {
        auto s = node.shape.grid_plane;
        if (!s.cam_ready) {
            s.cam_ready = true;
            s.cam_view  = camera.view_matrix();
            const f32 fw = static_cast<f32>(width);
            s.cam_focal = camera.focal_length(fw);
            s.vp_cx     = fw * 0.5f;
            s.vp_cy     = static_cast<f32>(height) * 0.5f;
        }
        renderer::draw_grid_plane(fb, node, state, s);
        return;
    }

    // Standard 2D Shape
    Color fill_color = linear_color;
    fill_color.a *= opacity;
    renderer::draw_transformed_shape(fb, node.shape, model, fill_color, 0.0f, &state);

    if (m_settings.diagnostic) {
        draw_diagnostic_info(fb, node, state);
    }
}

void SoftwareRenderer::draw_diagnostic_info(Framebuffer& fb, const RenderNode& node, const RenderState& state) {
    TextStyle debug_style;
    debug_style.font_path = "assets/fonts/Inter-Regular.ttf";
    debug_style.size = 12.0f;
    debug_style.color = Color{1, 0, 0, 0.8f};

    TextShape debug_text;
    debug_text.text = fmt::format("{}: ({:.1f}, {:.1f})", std::string(node.name), state.matrix[3][0], state.matrix[3][1]);
    debug_text.style = debug_style;

    Transform text_tr;
    text_tr.position = Vec3(state.matrix[3]);
    m_text_renderer.draw_text(debug_text, text_tr, fb);
}

void SoftwareRenderer::draw_line(Framebuffer& fb, const Vec3& p1, const Vec3& p2, const Color& color) {
    renderer::bline(fb, Vec2(p1.x, p1.y), Vec2(p2.x, p2.y), color);
}

// ---------------------------------------------------------------------------
// Static Helpers (Effects & Compositing)
// ---------------------------------------------------------------------------

void SoftwareRenderer::apply_blur(Framebuffer& fb, f32 radius) {
    renderer::apply_blur(fb, radius);
}

void SoftwareRenderer::apply_effect_stack(Framebuffer& fb, const EffectStack& stack) {
    renderer::apply_effect_stack(fb, stack);
}

void SoftwareRenderer::composite_layer(Framebuffer& dst, const Framebuffer& src, BlendMode mode) {
    const i32 w = dst.width(), h = dst.height();
    for (i32 y = 0; y < h; ++y) {
        for (i32 x = 0; x < w; ++x) {
            Color s = src.get_pixel(x, y);
            if (s.a <= 0.0f) continue;
            s = s.unpremultiplied();
            dst.set_pixel(x, y, compositor::blend(s, dst.get_pixel(x, y), mode));
        }
    }
}

// Private dummy for internal usage if needed
void SoftwareRenderer::apply_color_effects(Framebuffer& fb, const LayerEffect& effect) {
    renderer::apply_color_effects(fb, effect);
}

void SoftwareRenderer::draw_shadow(Framebuffer& fb, const RenderNode& node, const RenderState& state) {
    renderer::draw_shadow(fb, node, state);
}

void SoftwareRenderer::draw_glow(Framebuffer& fb, const RenderNode& node, const RenderState& state) {
    renderer::draw_glow(fb, node, state);
}

void SoftwareRenderer::render_layer_nodes(Framebuffer& fb, const Layer& layer,
                                          const RenderState& layer_state,
                                          const Camera& camera, i32 width, i32 height) {
    for (const auto& node : layer.nodes) {
        if (!node.visible) continue;
        RenderState node_state = combine(layer_state, node.world_transform);
        draw_node(fb, node, node_state, camera, width, height);
    }
}

void SoftwareRenderer::render_mesh_wireframe(Framebuffer& fb, const Mesh& mesh, const Mat4& model,
                                             const Mat4& view, const Mat4& proj, const Color& color) {
    renderer::render_mesh_wireframe(fb, mesh, model, view, proj, color);
}

} // namespace chronon3d
