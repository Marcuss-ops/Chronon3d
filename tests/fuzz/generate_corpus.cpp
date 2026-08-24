// ---------------------------------------------------------------------------
// tests/fuzz/generate_corpus.cpp — Seed corpus generator for IPC fuzz targets
//
// Produces a small set of valid FlatBuffers-encoded IPC frames so the fuzzer
// starts from realistic inputs rather than purely random bytes.  This
// dramatically accelerates coverage discovery (libFuzzer mutates from known
// valid inputs).
//
// Output files (written to <output_dir>/):
//   status.bin               — StatusRequest
//   create_composition.bin   — CreateCompositionRequest (minimal JSON)
//   render_frame.bin         — RenderFrameRequest (no params)
//   shutdown.bin             — ShutdownRequest
// ---------------------------------------------------------------------------

#include "src/ipc/ipc_codec.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>

using namespace chronon3d::ipc;
namespace fs = std::filesystem;

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: generate_corpus <output_directory>\n";
        return 1;
    }

    fs::path out_dir = argv[1];
    fs::create_directories(out_dir);

    // ── Status request (simplest: no fields) ────────────────────────
    {
        auto frame = IpcCodec::encode_request(1, IpcRequest{IpcStatusRequest{}});
        std::ofstream(out_dir / "status.bin", std::ios::binary)
            .write(reinterpret_cast<const char*>(frame.data()),
                   static_cast<std::streamsize>(frame.size()));
    }

    // ── CreateComposition request (minimal valid JSON payload) ──────
    {
        IpcCreateComposition req;
        req.composition_id  = "seed-comp-1";
        req.descriptor_json = R"({"layers":[]})";
        auto frame = IpcCodec::encode_request(2, IpcRequest{std::move(req)});
        std::ofstream(out_dir / "create_composition.bin", std::ios::binary)
            .write(reinterpret_cast<const char*>(frame.data()),
                   static_cast<std::streamsize>(frame.size()));
    }

    // ── RenderFrame request (frame 0, no parameter overrides) ───────
    {
        IpcRenderFrame req;
        req.composition_id = "seed-comp-1";
        req.frame_index    = 0;
        auto frame = IpcCodec::encode_request(3, IpcRequest{std::move(req)});
        std::ofstream(out_dir / "render_frame.bin", std::ios::binary)
            .write(reinterpret_cast<const char*>(frame.data()),
                   static_cast<std::streamsize>(frame.size()));
    }

    // ── Shutdown request ────────────────────────────────────────────
    {
        auto frame = IpcCodec::encode_request(4, IpcRequest{IpcShutdown{}});
        std::ofstream(out_dir / "shutdown.bin", std::ios::binary)
            .write(reinterpret_cast<const char*>(frame.data()),
                   static_cast<std::streamsize>(frame.size()));
    }

    std::cout << "Seed corpus written to " << out_dir << " (" << 4 << " files)\n";
    return 0;
}