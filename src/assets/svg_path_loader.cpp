#include <chronon3d/assets/svg_path_loader.hpp>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <cctype>
#include <cstdlib>
#include <cmath>

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

    if (d_pos + 2 >= svg.size()) {
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
        skip_separators();
        if (pos_ >= input_.size()) return false;
        const char c = input_[pos_];
        return std::isalpha(static_cast<unsigned char>(c)) != 0;
    }

    char read_command() {
        skip_separators();
        if (pos_ >= input_.size()) throw std::runtime_error("Expected SVG path command");
        return input_[pos_++];
    }

    f32 read_number() {
        skip_separators();
        if (pos_ >= input_.size()) {
            throw std::runtime_error("Expected SVG path number, hit EOF");
        }

        const char* begin = input_.data() + pos_;
        char* parsed_end = nullptr;
        
        // input_ is null-terminated, so std::strtof is safe
        const float value = std::strtof(begin, &parsed_end);

        if (parsed_end == begin) {
            throw std::runtime_error("Expected SVG path number");
        }

        pos_ = static_cast<size_t>(parsed_end - input_.data());
        return value;
    }

private:
    void skip_separators() {
        while (pos_ < input_.size()) {
            const char c = input_[pos_];
            if (std::isspace(static_cast<unsigned char>(c)) || c == ',') {
                ++pos_;
            } else {
                break;
            }
        }
    }

    std::string_view input_;
    size_t pos_{0};
};

SvgPathLoadResult parse_svg_path_data(std::string_view d, SvgPathLoadOptions) {
    // Copy to std::string to guarantee null-termination for safe std::strtof tokenization
    std::string d_str(d);
    SvgPathTokenizer t(d_str);
    PathShape path;

    Vec2 current{0.0f, 0.0f};
    Vec2 subpath_start{0.0f, 0.0f};
    char command = 0;

    try {
        while (!t.eof()) {
            if (t.next_is_command()) {
                command = t.read_command();
            }

            const bool relative = std::islower(static_cast<unsigned char>(command)) != 0;
            const char op = static_cast<char>(std::toupper(static_cast<unsigned char>(command)));

            auto make_point = [&](f32 x, f32 y) {
                Vec2 p{x, y};
                if (relative) {
                    p.x += current.x;
                    p.y += current.y;
                }
                return p;
            };

            switch (op) {
                case 'M': {
                    f32 x = t.read_number();
                    f32 y = t.read_number();
                    Vec2 p = make_point(x, y);
                    path.commands.push_back(PathCommand::move_to(p));
                    current = p;
                    subpath_start = p;

                    // SVG rule: extra coordinate pairs after M/m are treated as L/l.
                    command = relative ? 'l' : 'L';
                    break;
                }

                case 'L': {
                    f32 x = t.read_number();
                    f32 y = t.read_number();
                    Vec2 p = make_point(x, y);
                    path.commands.push_back(PathCommand::line_to(p));
                    current = p;
                    break;
                }

                case 'H': {
                    f32 x = t.read_number();
                    Vec2 p = relative ? Vec2{current.x + x, current.y}
                                      : Vec2{x, current.y};
                    path.commands.push_back(PathCommand::line_to(p));
                    current = p;
                    break;
                }

                case 'V': {
                    f32 y = t.read_number();
                    Vec2 p = relative ? Vec2{current.x, current.y + y}
                                      : Vec2{current.x, y};
                    path.commands.push_back(PathCommand::line_to(p));
                    current = p;
                    break;
                }

                case 'C': {
                    f32 cx1 = t.read_number();
                    f32 cy1 = t.read_number();
                    Vec2 c1 = make_point(cx1, cy1);

                    f32 cx2 = t.read_number();
                    f32 cy2 = t.read_number();
                    Vec2 c2 = make_point(cx2, cy2);

                    f32 px = t.read_number();
                    f32 py = t.read_number();
                    Vec2 p = make_point(px, py);

                    path.commands.push_back(PathCommand::cubic_to(c1, c2, p));
                    current = p;
                    break;
                }

                case 'Q': {
                    f32 cx = t.read_number();
                    f32 cy = t.read_number();
                    Vec2 c = make_point(cx, cy);

                    f32 px = t.read_number();
                    f32 py = t.read_number();
                    Vec2 p = make_point(px, py);

                    path.commands.push_back(PathCommand::quadratic_to(c, p));
                    current = p;
                    break;
                }

                case 'Z': {
                    path.commands.push_back(PathCommand::close());
                    path.closed = true;
                    current = subpath_start;
                    command = 0;
                    break;
                }

                default:
                    return {.path = {}, .ok = false, .error = "Unsupported SVG path command"};
            }
        }

        return {.path = std::move(path), .ok = true, .error = ""};

    } catch (const std::exception& e) {
        return {.path = {}, .ok = false, .error = e.what()};
    }
}

SvgPathLoadResult load_svg_path_file(const std::string& filename, SvgPathLoadOptions options) {
    std::ifstream in(filename);
    if (!in) {
        return {.path = {}, .ok = false, .error = "Cannot open SVG file"};
    }

    std::stringstream ss;
    ss << in.rdbuf();
    std::string svg = ss.str();

    auto d = extract_first_path_d(svg);
    if (!d) {
        return {.path = {}, .ok = false, .error = "No <path d=\"...\"> found"};
    }

    return parse_svg_path_data(*d, options);
}

} // namespace chronon3d::assets
