#include <chronon3d/renderer/software/fake_extruded_text_renderer.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/math/camera_2_5d_projection.hpp>
#include "../primitive_renderer.hpp"
#include <stb_truetype.h>
#include <mapbox/earcut.hpp>
#include <fmt/format.h>
#include <chronon3d/math/constants.hpp>
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

// Key light direction (upper-left-front): matches fake_box3d_renderer.
static const Vec3 k_light_dir = glm::normalize(Vec3(-0.4f, 1.2f, -0.6f));
static constexpr float k_ambient  = 0.20f;
static constexpr float k_diffuse  = 0.80f;
static constexpr int   k_bevel_n  = 6;

static float ndotl(const Vec3& normal) {
    return k_ambient + k_diffuse * std::max(0.0f, glm::dot(normal, k_light_dir));
}

// ── Glyph geometry cache ──────────────────────────────────────────────────────

const GlyphGeometry& FakeExtrudedTextRenderer::get_glyph(
    const std::string& font_path, float font_size,
    int codepoint, const uint8_t* font_data)
{
    std::string key = font_path + "|" + std::to_string(font_size) + "|" + std::to_string(codepoint);
    auto it = m_glyph_cache.find(key);
    if (it != m_glyph_cache.end()) return it->second;

    GlyphGeometry geom;

    stbtt_fontinfo font;
    stbtt_InitFont(&font, font_data, 0);
    const float sc_f = stbtt_ScaleForPixelHeight(&font, font_size);

    int adv, lsb;
    stbtt_GetCodepointHMetrics(&font, codepoint, &adv, &lsb);
    geom.advance = adv * sc_f;

    stbtt_vertex* verts = nullptr;
    int nv = stbtt_GetCodepointShape(&font, codepoint, &verts);

    if (nv > 0 && verts) {
        std::vector<std::vector<Vec2>> contours;
        for (int vi = 0; vi < nv; ++vi) {
            const auto& v = verts[vi];
            if (v.type == STBTT_vmove) {
                contours.emplace_back();
                contours.back().push_back({(float)v.x * sc_f, (float)v.y * sc_f});
            } else if (v.type == STBTT_vline) {
                if (!contours.empty())
                    contours.back().push_back({(float)v.x * sc_f, (float)v.y * sc_f});
            } else if (v.type == STBTT_vcurve) {
                if (contours.empty()) continue;
                Vec2 p0 = contours.back().back();
                Vec2 cp{(float)v.cx * sc_f, (float)v.cy * sc_f};
                Vec2 p1{(float)v.x  * sc_f, (float)v.y  * sc_f};
                for (int step = 1; step <= 12; ++step) {
                    float t = step / 12.0f, mt = 1.0f - t;
                    contours.back().push_back({
                        mt*mt*p0.x + 2*mt*t*cp.x + t*t*p1.x,
                        mt*mt*p0.y + 2*mt*t*cp.y + t*t*p1.y
                    });
                }
            } else if (v.type == STBTT_vcubic) {
                if (contours.empty()) continue;
                Vec2 p0 = contours.back().back();
                Vec2 c1{(float)v.cx  * sc_f, (float)v.cy  * sc_f};
                Vec2 c2{(float)v.cx1 * sc_f, (float)v.cy1 * sc_f};
                Vec2 p1{(float)v.x   * sc_f, (float)v.y   * sc_f};
                for (int step = 1; step <= 16; ++step) {
                    float t = step / 16.0f, mt = 1.0f - t;
                    contours.back().push_back({
                        mt*mt*mt*p0.x + 3*mt*mt*t*c1.x + 3*mt*t*t*c2.x + t*t*t*p1.x,
                        mt*mt*mt*p0.y + 3*mt*mt*t*c1.y + 3*mt*t*t*c2.y + t*t*t*p1.y
                    });
                }
            }
        }
        stbtt_FreeShape(&font, verts);

        auto get_area = [](const std::vector<Vec2>& c) {
            float a = 0;
            for (size_t i = 0; i < c.size(); ++i) {
                size_t j = (i + 1) % c.size();
                a += c[i].x * c[j].y - c[j].x * c[i].y;
            }
            return a * 0.5f;
        };

        std::vector<GlyphIsland> islands;
        for (auto& c : contours) {
            if (c.size() < 3) continue;
            float a = get_area(c);
            if (a > 0) {
                islands.push_back({});
                islands.back().polygon.push_back(std::move(c));
            } else {
                bool placed = false;
                for (auto& island : islands) {
                    island.polygon.push_back(std::move(c));
                    placed = true;
                    break;
                }
                if (!placed) {
                    islands.push_back({});
                    islands.back().polygon.push_back(std::move(c));
                }
            }
        }

        for (auto& island : islands) {
            island.indices = mapbox::earcut<uint32_t>(island.polygon);
            for (const auto& c : island.polygon)
                island.flat_verts.insert(island.flat_verts.end(), c.begin(), c.end());
        }
        geom.islands = std::move(islands);
    }

    return m_glyph_cache.emplace(std::move(key), std::move(geom)).first->second;
}

// ── Main draw ─────────────────────────────────────────────────────────────────

void FakeExtrudedTextRenderer::draw(
    Framebuffer& fb,
    const RenderNode& node,
    const RenderState& state,
    const Camera& camera,
    i32 width,
    i32 height,
    TextRenderer& text_renderer)
{
    const auto& s = node.shape.fake_extruded_text;
    const f32 op = state.opacity;

    // ── SCREEN-SPACE FALLBACK (no camera) ─────────────────────────────────
    const auto& rt = node.fake_extruded_text_runtime;
    if (!rt.cam_ready) {
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
            text_renderer.draw_text(ts,tr,front_mask);
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
          text_renderer.draw_text(ts,tr,fb,&state); }
        if (s.highlight_opacity>0) {
            TextShape ts; ts.text=s.text; ts.style.font_path=s.font_path;
            ts.style.size=s.font_size; ts.style.color=Color{1,1,1,s.highlight_opacity*op};
            ts.style.align=s.align; Transform tr; tr.position=Vec3(front_sp.x,front_sp.y-1,0);
            text_renderer.draw_text(ts,tr,fb,&state);
        }
        return;
    }

    // ── MESH-BASED 3D EXTRUSION ────────────────────────────────────────────
    const CachedFont* font_entry = text_renderer.get_font(s.font_path);
    if (!font_entry) return;

    stbtt_fontinfo font;
    if (!stbtt_InitFont(&font, font_entry->data.data(), 0)) return;

    const float sc_f = stbtt_ScaleForPixelHeight(&font, s.font_size);
    float x_cur = 0.0f;
    {
        float total_w = 0;
        for (char ch : s.text) {
            int adv, lsb;
            stbtt_GetCodepointHMetrics(&font, (int)(unsigned char)ch, &adv, &lsb);
            total_w += (float)adv * sc_f;
        }
        x_cur = (s.align == TextAlign::Center) ? -total_w * 0.5f :
                (s.align == TextAlign::Right)   ? -total_w : 0.0f;
    }

    struct SideQ { Vec2 v[4]; Color c0, c1; float depth; };
    struct Tri3D { Vec2 v[3]; Color colors[3]; float depth; };
    std::vector<SideQ> quads;
    std::vector<Tri3D> triangles;

    const float depth_z = (float)s.depth * s.extrude_z_step;

    // Perspective-correct projection helper
    auto proj3 = [&](const Vec3& w, bool& ok) -> Vec2 {
        Vec4 p = camera.view_matrix() * Vec4(w, 1.0f);
        if (p.z > -1.0f) { ok = false; return {}; }
        Vec4 clip = camera.projection_matrix((float)width / height) * p;
        ok = true;
        float inv_w = 1.0f / clip.w;
        return Vec2{(clip.x * inv_w + 1.0f) * width * 0.5f, (1.0f - clip.y * inv_w) * height * 0.5f};
    };

    auto cam_depth = [&](const Vec3& w) -> float {
        return -(camera.view_matrix() * Vec4(w, 1.0f)).z;
    };

    for (char ch : s.text) {
        const int cp = (int)(unsigned char)ch;
        const GlyphGeometry& geom = get_glyph(s.font_path, s.font_size, cp, font_entry->data.data());

        auto transform_pt = [&](Vec2 p, float z) -> Vec3 {
            Vec4 local{x_cur + p.x, -p.y, z, 1.0f};
            Vec4 world = state.matrix * local;
            return Vec3(world.x, world.y, world.z);
        };

        for (const auto& island : geom.islands) {
            // Compute glyph Y range for front face gradient
            float island_ymin = +1e9f, island_ymax = -1e9f;
            for (const auto& v : island.flat_verts) {
                island_ymin = std::min(island_ymin, v.y);
                island_ymax = std::max(island_ymax, v.y);
            }
            const float island_yrange = island_ymax - island_ymin + 1e-6f;

            // Front + back faces
            for (size_t i = 0; i < island.indices.size(); i += 3) {
                // Front face (z=0)
                Vec3 w_front[3]; Vec2 s_front[3]; bool ok_f[3]; float d_f = 0;
                for (int j = 0; j < 3; ++j) {
                    w_front[j] = transform_pt(island.flat_verts[island.indices[i + j]], 0);
                    s_front[j] = proj3(w_front[j], ok_f[j]);
                    d_f += cam_depth(w_front[j]);
                }
                if (ok_f[0] && ok_f[1] && ok_f[2]) {
                    Vec3 fn = glm::normalize(Vec3(state.matrix * Vec4(0,0,-1,0)));
                    float fl = ndotl(fn);
                    Color c_colors[3];
                    for (int j = 0; j < 3; ++j) {
                        float ty = std::clamp(
                            (island.flat_verts[island.indices[i+j]].y - island_ymin) / island_yrange,
                            0.0f, 1.0f);
                        float grad = fl * (1.05f - 0.10f * ty);
                        Color fc = s.front_color.with_alpha(s.front_color.a * op);
                        c_colors[j] = Color{
                            std::min(1.0f, fc.r * grad), std::min(1.0f, fc.g * grad),
                            std::min(1.0f, fc.b * grad), fc.a
                        };
                    }
                    triangles.push_back({{s_front[0], s_front[1], s_front[2]},
                                         {c_colors[0], c_colors[1], c_colors[2]}, d_f / 3.0f});
                }

                // Back face (z=depth_z)
                Vec3 w_back[3]; Vec2 s_back[3]; bool ok_b[3]; float d_b = 0;
                for (int j = 0; j < 3; ++j) {
                    w_back[j] = transform_pt(island.flat_verts[island.indices[i + (2-j)]], depth_z);
                    s_back[j] = proj3(w_back[j], ok_b[j]);
                    d_b += cam_depth(w_back[j]);
                }
                if (ok_b[0] && ok_b[1] && ok_b[2]) {
                    Vec3 bn = glm::normalize(Vec3(state.matrix * Vec4(0,0,1,0)));
                    float bl = ndotl(bn);
                    Color bc = s.side_color.with_alpha(s.side_color.a * 0.5f * op);
                    Color bc_lit{bc.r * bl, bc.g * bl, bc.b * bl, bc.a};
                    triangles.push_back({{s_back[0], s_back[1], s_back[2]},
                                         {bc_lit, bc_lit, bc_lit}, d_b / 3.0f});
                }
            }

            // Side (extruded) faces
            for (const auto& contour : island.polygon) {
                for (size_t i = 0; i < contour.size(); ++i) {
                    Vec2 p0 = contour[i];
                    Vec2 p1 = contour[(i + 1) % contour.size()];

                    Vec2 edge = p1 - p0;
                    Vec2 norm2d = {-edge.y, edge.x};
                    float nlen = std::sqrt(norm2d.x*norm2d.x + norm2d.y*norm2d.y);
                    if (nlen < 1e-6f) continue;
                    norm2d = norm2d / nlen;

                    Vec2 mid = (p0 + p1) * 0.5f;
                    float d = cam_depth(transform_pt(mid, depth_z * 0.5f));

                    Vec4 n3d_local{norm2d.x, -norm2d.y, 0.0f, 0.0f};
                    Vec3 n3d_world = glm::normalize(Vec3(state.matrix * n3d_local));
                    float side_light = ndotl(n3d_world);

                    Color c_base = s.side_color.with_alpha(s.side_color.a * op);
                    Color c_lit{c_base.r * side_light, c_base.g * side_light, c_base.b * side_light, c_base.a};

                    auto add_quad = [&](float z0, float z1, Color ca, Color cb) {
                        Vec3 w00 = transform_pt(p0, z0), w10 = transform_pt(p1, z0);
                        Vec3 w01 = transform_pt(p0, z1), w11 = transform_pt(p1, z1);
                        bool ok[4]; Vec2 sv[4];
                        sv[0] = proj3(w00, ok[0]); sv[1] = proj3(w10, ok[1]);
                        sv[2] = proj3(w11, ok[2]); sv[3] = proj3(w01, ok[3]);
                        if (ok[0] && ok[1] && ok[2] && ok[3])
                            quads.push_back({{sv[0], sv[1], sv[2], sv[3]}, ca, cb, d});
                    };

                    if (s.bevel_size > 0.01f) {
                        // Curved bevel: k_bevel_n rings with circular arc profile
                        const float pi_half = math::half_pi;
                        for (int bi = 0; bi < k_bevel_n; ++bi) {
                            float t0 = (float)bi / k_bevel_n;
                            float t1 = (float)(bi + 1) / k_bevel_n;
                            float z0 = s.bevel_size * (1.0f - std::cos(t0 * pi_half));
                            float z1 = s.bevel_size * (1.0f - std::cos(t1 * pi_half));
                            float slope0 = std::cos(t0 * pi_half);
                            float slope1 = std::cos(t1 * pi_half);
                            Vec3 bn0 = glm::normalize(Vec3(state.matrix * Vec4(norm2d.x*slope0, -norm2d.y*slope0, -(1.0f-slope0), 0.0f)));
                            Vec3 bn1 = glm::normalize(Vec3(state.matrix * Vec4(norm2d.x*slope1, -norm2d.y*slope1, -(1.0f-slope1), 0.0f)));
                            float bl0 = ndotl(bn0);
                            float bl1 = ndotl(bn1);
                            Color bc0{c_base.r*bl0, c_base.g*bl0, c_base.b*bl0, c_base.a};
                            Color bc1{c_base.r*bl1, c_base.g*bl1, c_base.b*bl1, c_base.a};
                            add_quad(z0, z1, bc0, bc1);
                        }
                        add_quad(s.bevel_size, depth_z, c_lit, c_lit);
                    } else {
                        add_quad(0.0f, depth_z, c_lit, c_lit);
                    }
                }
            }
        }

        x_cur += geom.advance;
    }

    // ── Depth sort and render ─────────────────────────────────────────────
    struct RenderPrimitive { enum Type { Quad, Tri } type; int index; float depth; };
    std::vector<RenderPrimitive> sorted;
    sorted.reserve(quads.size() + triangles.size());
    for (size_t i = 0; i < quads.size(); ++i)
        sorted.push_back({RenderPrimitive::Quad, (int)i, quads[i].depth});
    for (size_t i = 0; i < triangles.size(); ++i)
        sorted.push_back({RenderPrimitive::Tri, (int)i, triangles[i].depth});
    std::stable_sort(sorted.begin(), sorted.end(),
        [](const RenderPrimitive& a, const RenderPrimitive& b){ return a.depth > b.depth; });

    for (const auto& p : sorted) {
        if (p.type == RenderPrimitive::Quad) {
            const auto& q = quads[p.index];
            Color gc[4] = {q.c0, q.c1, q.c1, q.c0};
            renderer::fill_gradient_quad(fb, q.v, gc);
        } else {
            const auto& t = triangles[p.index];
            renderer::fill_gradient_triangle(fb, t.v, t.colors);
        }
    }
}

} // namespace chronon3d
