// ---------------------------------------------------------------------------
// tests/fuzz/ipc_codec_fuzz.cpp — libFuzzer target for the IPC codec
//
// Coverage-guided fuzz target for the daemon's FlatBuffers deserialization
// boundary.  The fuzzer generates arbitrary byte buffers and feeds them through
// IpcCodec::decode_request + IpcCodec::decode_reply — the first untrusted-data
// boundary after the Unix-socket framing layer.
//
// Build: cmake -DCHRONON3D_BUILD_FUZZERS=ON (Clang only)
// Run:   ./ipc_codec_fuzz tests/fuzz/corpus/ipc/
//
// Targets the full surface: 0 bytes, 1 byte, random garbage, corrupt
// FlatBuffers, valid FlatBuffers, truncated FlatBuffers, large payloads,
// bad union types, bad offsets.
// ---------------------------------------------------------------------------

#include "src/ipc/ipc_codec.hpp"

#include <cstddef>
#include <cstdint>

using namespace chronon3d::ipc;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    // Construct a WireFrame from the fuzzer-provided bytes.
    // WireFrame = std::vector<uint8_t> — a contiguous byte buffer.
    const WireFrame frame(data, data + size);

    // Exercise both decode paths.  The codec must never crash, overflow,
    // or invoke undefined behavior regardless of input.
    (void)IpcCodec::decode_request(frame);
    (void)IpcCodec::decode_reply(frame);

    return 0;  // libFuzzer convention: 0 = keep this input for the corpus
}