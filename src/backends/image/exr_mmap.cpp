#include <chronon3d/backends/image/exr_mmap.hpp>
#include <OpenEXR/ImfInputFile.h>
#include <OpenEXR/ImfRgbaFile.h>
#include <OpenEXR/ImfIO.h>
#include <Imath/half.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdexcept>
#include <cstring>
#include <algorithm>

namespace chronon3d::image {

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
            // Un-premultiply alpha because stb_image returns straight alpha 
            // and we premultiply it later. OpenEXR stores pre-multiplied alpha.
            // Actually, chronon3d uses linear colors. StbImageBackend returns RGBA8 
            // which are converted to float later. Wait, load_image returns u8[].
            // We must return u8[] here too, mapping 0-1 to 0-255.
            float r = pixels[i].r;
            float g = pixels[i].g;
            float b = pixels[i].b;
            float a = pixels[i].a;

            // Straighten alpha if needed (OpenEXR is premultiplied)
            if (a > 0.0f && a < 1.0f) {
                r /= a;
                g /= a;
                b /= a;
            }

            out[i * 4 + 0] = static_cast<u8>(std::clamp(r * 255.0f, 0.0f, 255.0f));
            out[i * 4 + 1] = static_cast<u8>(std::clamp(g * 255.0f, 0.0f, 255.0f));
            out[i * 4 + 2] = static_cast<u8>(std::clamp(b * 255.0f, 0.0f, 255.0f));
            out[i * 4 + 3] = static_cast<u8>(std::clamp(a * 255.0f, 0.0f, 255.0f));
        }

        return buffer;
    } catch (...) {
        return nullptr;
    }
}

} // namespace chronon3d::image
