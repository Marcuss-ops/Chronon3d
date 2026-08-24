#include "ipc_codec.hpp"

#include <cstddef>
#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    chronon3d::ipc::WireFrame frame(data, data + size);
    (void)chronon3d::ipc::IpcCodec::decode_request(frame);
    (void)chronon3d::ipc::IpcCodec::decode_reply(frame);
    return 0;
}
