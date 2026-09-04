#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#include <chronon3d/backends/image/image_writer.hpp>
#include <chronon3d/core/profiling/profiling.hpp>

#ifdef CHRONON3D_ENABLE_EXR
#include <OpenEXR/ImfOutputFile.h>
#include <OpenEXR/ImfTiledOutputFile.h>
#include <OpenEXR/ImfChannelList.h>
#include <OpenEXR/ImfFrameBuffer.h>
#include <OpenEXR/ImfHeader.h>
#include <Imath/half.h>
#endif

#include <vector>
#include <filesystem>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <stdexcept>
#include <string>
#include <spdlog/spdlog.h>

namespace chronon3d {

#include "image_writer_support_detail.hpp"
#include "image_writer_png_detail.hpp"
#include "image_writer_exr_detail.hpp"
#include "image_writer_dispatch_detail.hpp"

} // namespace chronon3d
