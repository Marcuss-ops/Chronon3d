#include "render_plan_decoder_detail.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace chronon3d::render_plan::detail {

graphics::Field2D decode_field2d_plan(const nlohmann::json& raw) {
    graphics::Field2D field;
    const auto generator = raw.at("generator").get<std::string>();
    static constexpr std::array generators{
        std::pair{"perlin", graphics::Field2DGenerator::Perlin},
        std::pair{"simplex", graphics::Field2DGenerator::Simplex},
        std::pair{"fractal", graphics::Field2DGenerator::Fractal},
        std::pair{"cellular", graphics::Field2DGenerator::Cellular},
        std::pair{"grid", graphics::Field2DGenerator::Grid},
        std::pair{"checker", graphics::Field2DGenerator::Checker},
        std::pair{"dots", graphics::Field2DGenerator::Dots},
        std::pair{"stripes", graphics::Field2DGenerator::Stripes},
        std::pair{"rings", graphics::Field2DGenerator::Rings},
        std::pair{"linear", graphics::Field2DGenerator::Linear},
        std::pair{"radial", graphics::Field2DGenerator::Radial},
        std::pair{"conic", graphics::Field2DGenerator::Conic},
        std::pair{"burst", graphics::Field2DGenerator::Burst}};
    const auto found = std::find_if(generators.begin(), generators.end(),
        [&](const auto& item) { return generator == item.first; });
    if (found == generators.end()) throw std::runtime_error("field.generator is unsupported");
    field.generator = found->second;
    field.seed = raw.value("seed", 0U);
    field.frequency = raw.value("frequency", field.frequency);
    field.octaves = raw.value("octaves", field.octaves);
    field.persistence = raw.value("persistence", field.persistence);
    field.lacunarity = raw.value("lacunarity", field.lacunarity);
    if (!std::isfinite(field.frequency) || field.frequency <= 0.0f || field.frequency > 4096.0f ||
        field.octaves < 1 || field.octaves > 16 || !std::isfinite(field.persistence) ||
        field.persistence < 0.0f || field.persistence > 1.0f ||
        !std::isfinite(field.lacunarity) || field.lacunarity <= 0.0f || field.lacunarity > 8.0f)
        throw std::runtime_error("field generator parameters are outside supported bounds");
    if (field.generator == graphics::Field2DGenerator::Burst &&
        (field.octaves < 3 || field.octaves > 16))
        throw std::runtime_error("burst field lobes must be in [3, 16]");
    if (raw.contains("operators")) {
        if (raw.at("operators").size() > 8)
            throw std::runtime_error("field operator chain exceeds the 8-operation limit");
        bool scalar_operator_seen = false;
        bool warp_seen = false;
        for (const auto& entry : raw.at("operators")) {
            graphics::Field2DOperator op;
            const auto kind = entry.at("kind").get<std::string>();
            const auto reject = [&](std::initializer_list<const char*> allowed) {
                for (const char* key : {"from", "to", "edge", "softness", "amount"})
                    if (entry.contains(key) && std::none_of(allowed.begin(), allowed.end(),
                        [&](const char* candidate) { return std::string_view(candidate) == key; }))
                        throw std::runtime_error("field operator contains a parameter unused by its kind");
            };
            if (kind == "remap") {
                reject({"from", "to"});
                scalar_operator_seen = true;
                op.kind = graphics::Field2DOperatorKind::Remap;
                op.from = entry.value("from", 0.0f); op.to = entry.value("to", 1.0f);
                if (!std::isfinite(op.from) || !std::isfinite(op.to) ||
                    op.from < 0.0f || op.from > 1.0f || op.to < 0.0f || op.to > 1.0f ||
                    std::abs(op.to - op.from) < 1.0e-7f)
                    throw std::runtime_error("field remap requires distinct bounds in [0, 1]");
            } else if (kind == "threshold") {
                reject({"edge"});
                scalar_operator_seen = true;
                op.kind = graphics::Field2DOperatorKind::Threshold;
                op.edge = entry.value("edge", 0.5f);
                if (!std::isfinite(op.edge) || op.edge < 0.0f || op.edge > 1.0f)
                    throw std::runtime_error("field threshold edge must be in [0, 1]");
            } else if (kind == "smoothstep") {
                reject({"edge", "softness"});
                scalar_operator_seen = true;
                op.kind = graphics::Field2DOperatorKind::Smoothstep;
                op.edge = entry.value("edge", 0.5f); op.softness = entry.value("softness", 0.1f);
                if (!std::isfinite(op.edge) || op.edge < 0.0f || op.edge > 1.0f ||
                    !std::isfinite(op.softness) || op.softness <= 0.0f || op.softness > 1.0f)
                    throw std::runtime_error("field smoothstep requires edge [0,1] and softness (0,1]");
            } else if (kind == "invert") {
                reject({});
                scalar_operator_seen = true;
                op.kind = graphics::Field2DOperatorKind::Invert;
            } else if (kind == "warp") {
                reject({"amount"});
                if (scalar_operator_seen || warp_seen)
                    throw std::runtime_error("field warp must occur once before scalar operators");
                warp_seen = true;
                op.kind = graphics::Field2DOperatorKind::Warp;
                op.amount = entry.value("amount", 0.05f);
                if (!std::isfinite(op.amount) || op.amount < 0.0f || op.amount > 1.0f)
                    throw std::runtime_error("field warp amount must be in [0, 1]");
            } else {
                throw std::runtime_error("field operator kind is unsupported");
            }
            field.operators.push_back(op);
        }
    }
    return field;
}


} // namespace chronon3d::render_plan::detail
