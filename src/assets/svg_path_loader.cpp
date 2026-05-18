#include <chronon3d/assets/svg_path_loader.hpp>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <cctype>
#include <cstdlib>

namespace chronon3d::assets {

static std::optional<std::string> extract_first_path_d(std::string_view svg) {
    const auto path_pos = svg.find("<path");
    if (path_pos == std::string_view::npos) {
        return std::nullopt;
    }

    const auto d_pos = svg.find("d=", path_pos);
    if (d_pos == std::string_view::npos) {
        return std::nullopt;
    }

    const char quote = svg[d_pos + 2];
    if (quote != '"' && quote != '\'') {
        return std::nullopt;
    }

    const auto value_start = d_pos + 3;
    const auto value_end = svg.find(quote, value_start);

    if (value_end == std::string_view::npos) {
        return std::nullopt;
    }

    return std::string(svg.substr(value_start, value_end - value_start));
}

class SvgPathTokenizer {
public:
    explicit SvgPathTokenizer(std::string_view input) : input_(input) {}

    bool eof() {
        skip_separators();
        return pos_ >= input_.size();
    }

    bool next_is_command() {
        if (eof()) return false;
        char c = input_[pos_];
        return std::isalpha(c);
    }

    char read_command() {
        if (eof()) return 0;
        return input_[pos_++];
    }

    float read_number() {
        if (eof()) return 0.0f;
        char* end;
        float val = std::strtof(input_.data() + pos_, &end);
        pos_ = end - input_.data();
        return val;
    }

private:
    void skip_separators() {
        while (pos_ < input_.size()) {
            char c = input_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == ',') {
                pos_++;
            } else {
                break;
            }
        }
    }

    std::string_view input_;
    size_t pos_{0};
};

SvgPath parse_svg_path_data(std::string_view d) {
    SvgPathTokenizer t(d);
    SvgPath path;

    Vec2 current{0, 0};
    Vec2 subpath_start{0, 0};
    char command = 0;

    while (!t.eof()) {
        if (t.next_is_command()) {
            command = t.read_command();
        } else if (command == 0) {
            break;
        }

        switch (command) {
            case 'M': {
                Vec2 p{t.read_number(), t.read_number()};
                current = p;
                subpath_start = p;
                path.commands.push_back({SvgPathCommandType::MoveTo, p});
                command = 'L'; // subsequent coordinates are LineTo
                break;
            }
            case 'L': {
                Vec2 p{t.read_number(), t.read_number()};
                current = p;
                path.commands.push_back({SvgPathCommandType::LineTo, p});
                break;
            }
            case 'H': {
                Vec2 p{t.read_number(), current.y};
                current = p;
                path.commands.push_back({SvgPathCommandType::LineTo, p});
                break;
            }
            case 'V': {
                Vec2 p{current.x, t.read_number()};
                current = p;
                path.commands.push_back({SvgPathCommandType::LineTo, p});
                break;
            }
            case 'C': {
                Vec2 c1{t.read_number(), t.read_number()};
                Vec2 c2{t.read_number(), t.read_number()};
                Vec2 p {t.read_number(), t.read_number()};
                current = p;
                path.commands.push_back({SvgPathCommandType::CubicTo, c1, c2, p});
                break;
            }
            case 'Q': {
                Vec2 c{t.read_number(), t.read_number()};
                Vec2 p{t.read_number(), t.read_number()};
                current = p;
                path.commands.push_back({SvgPathCommandType::QuadTo, c, p});
                break;
            }
            case 'Z':
            case 'z': {
                current = subpath_start;
                path.commands.push_back({SvgPathCommandType::Close});
                command = 0;
                break;
            }
            default:
                throw std::runtime_error(std::string("Unsupported SVG path command: ") + command);
        }
    }

    return path;
}

SvgLoadResult load_svg_path_file(const std::string& filename) {
    std::ifstream f(filename);
    if (!f.is_open()) {
        throw std::runtime_error("Failed to open SVG file: " + filename);
    }
    std::stringstream buffer;
    buffer << f.rdbuf();
    std::string svg_content = buffer.str();

    auto d = extract_first_path_d(svg_content);
    if (!d) {
        throw std::runtime_error("No valid <path d=\"...\"> found in " + filename);
    }

    SvgLoadResult res;
    res.path = parse_svg_path_data(*d);
    res.viewbox_size = {100.0f, 100.0f}; // Placeholder
    return res;
}

} // namespace chronon3d::assets
