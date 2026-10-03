#include <chronon3d/backends/image/exr_mmap.hpp>
#include <OpenEXR/ImfInputFile.h>
#include <OpenEXR/ImfRgbaFile.h>
#include <OpenEXR/ImfIO.h>
#include <OpenEXR/ImfStandardAttributes.h>
#include <Imath/half.h>
#include <spdlog/spdlog.h>

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <stdexcept>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <array>
#include <optional>

namespace chronon3d::image {

namespace {

using Matrix3 = std::array<std::array<double, 3>, 3>;

std::optional<Matrix3> inverse(const Matrix3& matrix) {
    const double a = matrix[0][0], b = matrix[0][1], c = matrix[0][2];
    const double d = matrix[1][0], e = matrix[1][1], f = matrix[1][2];
    const double g = matrix[2][0], h = matrix[2][1], i = matrix[2][2];
    const double determinant = a * (e * i - f * h) - b * (d * i - f * g) +
                               c * (d * h - e * g);
    if (!std::isfinite(determinant) || std::abs(determinant) < 1.0e-12)
        return std::nullopt;
    const double scale = 1.0 / determinant;
    return Matrix3{{
        {{(e * i - f * h) * scale, (c * h - b * i) * scale,
          (b * f - c * e) * scale}},
        {{(f * g - d * i) * scale, (a * i - c * g) * scale,
          (c * d - a * f) * scale}},
        {{(d * h - e * g) * scale, (b * g - a * h) * scale,
          (a * e - b * d) * scale}},
    }};
}

Matrix3 multiply(const Matrix3& left, const Matrix3& right) {
    Matrix3 result{};
    for (std::size_t row = 0; row < 3; ++row)
        for (std::size_t column = 0; column < 3; ++column)
            for (std::size_t k = 0; k < 3; ++k)
                result[row][column] += left[row][k] * right[k][column];
    return result;
}

std::optional<Matrix3> linear_exr_to_bt709(const Imf::Chromaticities& chroma) {
    constexpr double kD65x = 0.3127;
    constexpr double kD65y = 0.3290;
    const double white_x = chroma.white.x;
    const double white_y = chroma.white.y;
    if (!std::isfinite(white_x) || !std::isfinite(white_y) || white_x <= 0.0 ||
        white_y <= 0.0 || white_x + white_y >= 1.0)
        return std::nullopt;

    const auto rgb_to_xyz = [](const Imf::Chromaticities& value)
        -> std::optional<Matrix3> {
        const std::array<Imath::V2f, 3> primaries{
            value.red, value.green, value.blue};
        Matrix3 unscaled{};
        for (std::size_t column = 0; column < primaries.size(); ++column) {
            const double x = primaries[column].x;
            const double y = primaries[column].y;
            if (!std::isfinite(x) || !std::isfinite(y) || y <= 0.0 ||
                x < 0.0 || x + y > 1.0)
                return std::nullopt;
            unscaled[0][column] = x / y;
            unscaled[1][column] = 1.0;
            unscaled[2][column] = (1.0 - x - y) / y;
        }
        const auto inverse_unscaled = inverse(unscaled);
        if (!inverse_unscaled) return std::nullopt;
        const std::array<double, 3> white{
            static_cast<double>(value.white.x) / value.white.y,
            1.0,
            (1.0 - value.white.x - value.white.y) / value.white.y};
        std::array<double, 3> scale{};
        for (std::size_t row = 0; row < 3; ++row)
            for (std::size_t k = 0; k < 3; ++k)
                scale[row] += (*inverse_unscaled)[row][k] * white[k];
        Matrix3 result = unscaled;
        for (std::size_t row = 0; row < 3; ++row)
            for (std::size_t column = 0; column < 3; ++column)
                result[row][column] *= scale[column];
        return result;
    };

    const auto source = rgb_to_xyz(chroma);
    const Imf::Chromaticities target_chromaticities{};
    const auto target = rgb_to_xyz(target_chromaticities);
    if (!source || !target) return std::nullopt;
    const auto xyz_to_target = inverse(*target);
    if (!xyz_to_target) return std::nullopt;

    // Bradford cone-response adaptation maps the source white to D65 before
    // the RGB values enter Chronon's BT.709 working space. Both white points
    // are expressed in XYZ with Y normalized to 1.
    constexpr Matrix3 kBradford{{
        {{0.8951, 0.2664, -0.1614}},
        {{-0.7502, 1.7135, 0.0367}},
        {{0.0389, -0.0685, 1.0296}},
    }};
    const auto inverse_bradford = inverse(kBradford);
    if (!inverse_bradford) return std::nullopt;
    const auto transform_vector = [](const Matrix3& matrix,
                                     const std::array<double, 3>& vector) {
        std::array<double, 3> result{};
        for (std::size_t row = 0; row < 3; ++row)
            for (std::size_t column = 0; column < 3; ++column)
                result[row] += matrix[row][column] * vector[column];
        return result;
    };
    const std::array<double, 3> source_white{
        white_x / white_y, 1.0, (1.0 - white_x - white_y) / white_y};
    const std::array<double, 3> target_white{
        kD65x / kD65y, 1.0, (1.0 - kD65x - kD65y) / kD65y};
    const auto source_cone = transform_vector(kBradford, source_white);
    const auto target_cone = transform_vector(kBradford, target_white);
    Matrix3 cone_scale{};
    for (std::size_t channel = 0; channel < 3; ++channel) {
        if (!std::isfinite(source_cone[channel]) ||
            !std::isfinite(target_cone[channel]) ||
            std::abs(source_cone[channel]) < 1.0e-12)
            return std::nullopt;
        cone_scale[channel][channel] = target_cone[channel] / source_cone[channel];
    }
    const Matrix3 adaptation = multiply(
        multiply(*inverse_bradford, cone_scale), kBradford);
    return multiply(multiply(*xyz_to_target, adaptation), *source);
}

} // namespace

class MmapIStream : public Imf::IStream {
public:
    MmapIStream(const std::string& fileName) : Imf::IStream(fileName.c_str()), _data(nullptr), _size(0), _pos(0) {
        int fd = open(fileName.c_str(), O_RDONLY);
        if (fd == -1) {
            throw std::runtime_error("Cannot open file: " + fileName);
        }

        struct stat st;
        if (fstat(fd, &st) == -1) {
            close(fd);
            throw std::runtime_error("Cannot stat file: " + fileName);
        }

        _size = st.st_size;
        _data = static_cast<const char*>(mmap(nullptr, _size, PROT_READ, MAP_PRIVATE, fd, 0));
        close(fd);

        if (_data == MAP_FAILED) {
            throw std::runtime_error("Cannot mmap file: " + fileName);
        }
    }

    ~MmapIStream() override {
        if (_data != MAP_FAILED && _data != nullptr) {
            munmap(const_cast<char*>(_data), _size);
        }
    }

    bool read(char c[/*n*/], int n) override {
        if (_pos + n > _size) {
            throw std::runtime_error("Unexpected end of file");
        }
        std::memcpy(c, _data + _pos, n);
        _pos += n;
        return _pos < _size;
    }

    uint64_t tellg() override {
        return _pos;
    }

    void seekg(uint64_t pos) override {
        _pos = std::min(pos, static_cast<uint64_t>(_size));
    }

    void clear() override {}

private:
    const char* _data;
    size_t _size;
    size_t _pos;
};

std::unique_ptr<ImageBuffer> load_exr_mmap(const std::string& path) {
    try {
        MmapIStream stream(path);
        Imf::RgbaInputFile file(stream);
        Imath::Box2i dw = file.dataWindow();

        int width = dw.max.x - dw.min.x + 1;
        int height = dw.max.y - dw.min.y + 1;

        if (width <= 0 || height <= 0) return nullptr;

        std::vector<Imf::Rgba> pixels(width * height);
        file.setFrameBuffer(pixels.data() - dw.min.x - dw.min.y * width, 1, width);
        file.readPixels(dw.min.y, dw.max.y);

        auto buffer = std::make_unique<ImageBuffer>();
        buffer->width = width;
        buffer->height = height;
        buffer->channels = 4;
        buffer->pixels = std::make_unique<u8[]>(width * height * 4);

        u8* out = buffer->pixels.get();
        for (int i = 0; i < width * height; ++i) {
            // Return straight-alpha RGBA8 (matching stb_image convention).
            // ImageCache premultiplies alpha via simd::premultiply_alpha_rgba8,
            // so we must NOT straighten/strip alpha here — the cache handles
            // the premultiplication uniformly for both PNG (stb) and EXR paths.
            // OpenEXR may store either premultiplied or straight alpha; passing
            // pixels through as-is and letting the cache premultiply avoids
            // the double-transformation bug (exr_mmap straighten → cache premul).
            const float r = pixels[i].r;
            const float g = pixels[i].g;
            const float b = pixels[i].b;
            const float a = std::clamp(static_cast<float>(pixels[i].a), 0.0f, 1.0f);

            // NaN/Inf guard: clamp to valid range to prevent corrupt output
            const auto clamp_valid = [](float v) -> u8 {
                if (std::isnan(v) || std::isinf(v)) return 0;
                return static_cast<u8>(std::clamp(v * 255.0f, 0.0f, 255.0f));
            };
            out[i * 4 + 0] = clamp_valid(r);
            out[i * 4 + 1] = clamp_valid(g);
            out[i * 4 + 2] = clamp_valid(b);
            out[i * 4 + 3] = clamp_valid(a);
        }

        return buffer;
    } catch (...) {
        spdlog::error("exr_mmap: failed to load EXR file (unknown exception)");
        return nullptr;
    }
}

std::shared_ptr<Framebuffer> load_exr_as_framebuffer_float(const std::string& path) {
    try {
        MmapIStream stream(path);
        Imf::RgbaInputFile file(stream);
        const auto* chromaticities_attribute =
            file.header().findTypedAttribute<Imf::ChromaticitiesAttribute>("chromaticities");
        const Imf::Chromaticities source_chromaticities = chromaticities_attribute
            ? chromaticities_attribute->value() : Imf::Chromaticities{};
        const auto primary_transform = linear_exr_to_bt709(source_chromaticities);
        if (!primary_transform)
            throw std::runtime_error(
                "EXR chromaticities are invalid or singular; BT.709 scope conversion is unavailable");
        const Imath::Box2i dw = file.dataWindow();
        const int width = dw.max.x - dw.min.x + 1;
        const int height = dw.max.y - dw.min.y + 1;
        if (width <= 0 || height <= 0) return nullptr;

        std::vector<Imf::Rgba> pixels(static_cast<std::size_t>(width) * height);
        file.setFrameBuffer(pixels.data() - dw.min.x - dw.min.y * width, 1, width);
        file.readPixels(dw.min.y, dw.max.y);

        auto framebuffer = std::make_shared<Framebuffer>(width, height);
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const auto& sample = pixels[static_cast<std::size_t>(y) * width + x];
                const float alpha = std::clamp(static_cast<float>(sample.a), 0.0f, 1.0f);
                const std::array<double, 3> source_rgb{
                    static_cast<float>(sample.r), static_cast<float>(sample.g),
                    static_cast<float>(sample.b)};
                std::array<float, 3> bt709_rgb{};
                for (std::size_t row = 0; row < 3; ++row)
                    for (std::size_t column = 0; column < 3; ++column)
                        bt709_rgb[row] += static_cast<float>(
                            (*primary_transform)[row][column] * source_rgb[column]);
                // OpenEXR RGBA channels conventionally store associated
                // (premultiplied) color. Preserve those linear samples and
                // their scene-linear headroom without multiplying alpha again.
                // Chromaticities are converted to the engine's linear BT.709
                // working primaries before the framebuffer is published.
                framebuffer->set_pixel(x, y, Color{
                    bt709_rgb[0], bt709_rgb[1], bt709_rgb[2],
                    alpha});
            }
        }
        return framebuffer;
    } catch (const std::exception& error) {
        spdlog::error("exr_mmap: failed to load float framebuffer '{}': {}", path, error.what());
        return nullptr;
    } catch (...) {
        spdlog::error("exr_mmap: failed to load float framebuffer '{}'", path);
        return nullptr;
    }
}

std::optional<float> read_exr_white_luminance_nits(const std::string& path) {
    MmapIStream stream(path);
    Imf::RgbaInputFile file(stream);
    const auto* attribute = file.header().findTypedAttribute<Imf::FloatAttribute>(
        "whiteLuminance");
    if (!attribute) return std::nullopt;
    const float value = attribute->value();
    if (!std::isfinite(value) || value <= 0.0f)
        throw std::runtime_error("EXR whiteLuminance must be finite and positive");
    return value;
}

} // namespace chronon3d::image
