// expression_value.cpp — Diagnostic to_string() for ExpressionValue variants.

#include <chronon3d/expressions/v2/expression_value.hpp>

#include <sstream>
#include <string>

namespace chronon3d::expressions::v2 {

std::string to_string(const ExpressionValue& v) {
    std::ostringstream oss;
    switch (v.index()) {
        case 0: oss << "Number(" << std::get<double>(v) << ")"; break;
        case 1: oss << "Bool("   << (std::get<bool>(v) ? "true" : "false") << ")"; break;
        case 2: oss << "String(" << std::get<std::string>(v) << ")"; break;
        case 3: oss << "Vec2(" << std::get<Vec2>(v).x << "," << std::get<Vec2>(v).y << ")"; break;
        case 4: {
            const Vec3& t = std::get<Vec3>(v);
            oss << "Vec3(" << t.x << "," << t.y << "," << t.z << ")";
            break;
        }
        case 5: {
            const Vec4& t = std::get<Vec4>(v);
            oss << "Vec4(" << t.x << "," << t.y << "," << t.z << "," << t.w << ")";
            break;
        }
        case 6: {
            const Color& c = std::get<Color>(v);
            oss << "Color(" << c.r << "," << c.g << "," << c.b << "," << c.a << ")";
            break;
        }
        case 7: oss << "LayerReference(" << std::get<LayerReference>(v).layer_id << ")"; break;
        case 8: {
            const auto& p = std::get<PropertyReference>(v);
            oss << "PropertyReference(" << p.layer_id << "/" << p.property_path << ")";
            break;
        }
        default: oss << "<unknown ExpressionValue index=" << v.index() << ">"; break;
    }
    return oss.str();
}

} // namespace chronon3d::expressions::v2
