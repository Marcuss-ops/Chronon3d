#include "primitive_renderer.hpp"
#include <chronon3d/math/camera_2_5d_projection.hpp>
#include <stb_truetype.h>
#include <mapbox/earcut.hpp>
#include <chronon3d/scene/render_node.hpp>
#include <chronon3d/scene/camera.hpp>
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
namespace renderer {

void draw_fake_extruded_text(Framebuffer& fb, const RenderNode& node, const RenderState& state, const Camera& camera, i32 width, i32 height) {
    if (node.shape.type != ShapeType::FakeExtrudedText) return;
    const auto& s = node.shape.fake_extruded_text;
    const f32 op = state.opacity;

    // Load font
    // Note: In a production engine, this should be cached or passed from a central manager.
    // For now we keep the existing logic of opening it here or assuming it's available.
    // Actually SoftwareRenderer::draw_node assumes it can find it.
    
    // We need access to the font data. For now, let's assume we re-read it or it's provided.
    // To keep the refactor simple and safe, I'll copy the logic but I need to make sure
    // the font path is valid.
    
    std::vector<unsigned char> font_data;
    FILE* f = fopen(s.font_path.c_str(), "rb");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);
    font_data.resize(size);
    fread(font_data.data(), 1, size, f);
    fclose(f);

    stbtt_fontinfo font;
    if (!stbtt_InitFont(&font, font_data.data(), 0)) return;

    float sc_f = stbtt_ScaleForPixelHeight(&font, s.font_size);
    float x_cur = 0;

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

            struct Island { std::vector<std::vector<Vec2>> polygon; float area; };
            std::vector<Island> islands;
            for (auto& c : contours) {
                if (c.size() < 3) continue;
                float a = get_area(c);
                if (a > 0) { islands.push_back({ {std::move(c)}, a }); }
                else {
                    bool placed = false;
                    for (auto& island : islands) { island.polygon.push_back(std::move(c)); placed = true; break; }
                    if (!placed) { islands.push_back({ {std::move(c)}, a }); }
                }
            }

            auto transform_pt = [&](Vec2 p, float z) {
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
}

std::unique_ptr<Framebuffer> downsample_fb(const Framebuffer& src, i32 dst_w, i32 dst_h) {
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
                dst->set_pixel(x, y, Color(r * inv, g * inv, b * inv, a * inv));
            }
        }
    }
    return dst;
}

} // namespace renderer
} // namespace chronon3d
