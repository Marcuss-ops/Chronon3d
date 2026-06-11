#include <chronon3d/backends/image/exr_mmap.hpp>
#include <OpenEXR/ImfInputFile.h>
#include <OpenEXR/ImfRgbaFile.h>
#include <OpenEXR/ImfIO.h>
#include <Imath/half.h>
#include <spdlog/spdlog.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#endif

#include <stdexcept>
#include <cstring>
#include <cmath>
#include <algorithm>

namespace chronon3d::image {

class MmapIStream : public Imf::IStream {
public:
    MmapIStream(const std::string& fileName) : Imf::IStream(fileName.c_str()), _data(nullptr), _size(0), _pos(0) {
#ifdef _WIN32
        _hFile = CreateFileA(fileName.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (_hFile == INVALID_HANDLE_VALUE) {
            throw std::runtime_error("Cannot open file: " + fileName);
        }

        LARGE_INTEGER size;
        if (!GetFileSizeEx(_hFile, &size)) {
            CloseHandle(_hFile);
            throw std::runtime_error("Cannot get file size: " + fileName);
        }
        _size = static_cast<size_t>(size.QuadPart);

        _hMapping = CreateFileMappingA(_hFile, NULL, PAGE_READONLY, 0, 0, NULL);
        if (_hMapping == NULL) {
            CloseHandle(_hFile);
            throw std::runtime_error("Cannot create file mapping: " + fileName);
        }

        _data = static_cast<const char*>(MapViewOfFile(_hMapping, FILE_MAP_READ, 0, 0, 0));
        if (_data == NULL) {
            CloseHandle(_hMapping);
            CloseHandle(_hFile);
            throw std::runtime_error("Cannot map view of file: " + fileName);
        }
#else
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
#endif
    }

    ~MmapIStream() override {
#ifdef _WIN32
        if (_data) UnmapViewOfFile(_data);
        if (_hMapping) CloseHandle(_hMapping);
        if (_hFile != INVALID_HANDLE_VALUE) CloseHandle(_hFile);
#else
        if (_data != MAP_FAILED && _data != nullptr) {
            munmap(const_cast<char*>(_data), _size);
        }
#endif
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
#ifdef _WIN32
    HANDLE _hFile = INVALID_HANDLE_VALUE;
    HANDLE _hMapping = NULL;
#endif
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

} // namespace chronon3d::image
