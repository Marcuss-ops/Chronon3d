// ---------------------------------------------------------------------------
// src/ipc/ipc_codec.cpp — FlatBuffers encode/decode implementation
// ---------------------------------------------------------------------------

#include "ipc_codec.hpp"

namespace chronon3d::ipc {

WireFrame IpcCodec::encode_request(std::uint64_t message_id,
                                   const IpcRequest& request) {
    flatbuffers::FlatBufferBuilder builder(1024);

    std::visit([&](const auto& req) {
        using T = std::decay_t<decltype(req)>;
        if constexpr (std::is_same_v<T, IpcCreateComposition>) {
            auto comp_id = builder.CreateString(req.composition_id);
            auto desc    = builder.CreateString(req.descriptor_json);
            auto body_offset = CreateCreateCompositionRequest(
                builder, comp_id, desc).Union();
            auto envelope = CreateIpcEnvelope(
                builder, message_id, IpcRequestBody_CreateCompositionRequest, body_offset);
            builder.Finish(envelope);
        } else if constexpr (std::is_same_v<T, IpcRenderFrame>) {
            auto comp_id = builder.CreateString(req.composition_id);
            auto output  = req.output_path.empty()
                ? flatbuffers::Offset<flatbuffers::String>{}
                : builder.CreateString(req.output_path);
            std::vector<flatbuffers::Offset<ParamOverride>> params;
            for (const auto& [k, v] : req.parameters) {
                auto key = builder.CreateString(k);
                auto val = builder.CreateString(v);
                params.push_back(CreateParamOverride(builder, key, val));
            }
            auto params_vec = builder.CreateVector(params);
            auto body_offset = CreateRenderFrameRequest(
                builder, comp_id, req.frame_index, params_vec, output).Union();
            auto envelope = CreateIpcEnvelope(
                builder, message_id, IpcRequestBody_RenderFrameRequest, body_offset);
            builder.Finish(envelope);
        } else if constexpr (std::is_same_v<T, IpcStatusRequest>) {
            auto body_offset = CreateStatusRequest(builder).Union();
            auto envelope = CreateIpcEnvelope(
                builder, message_id, IpcRequestBody_StatusRequest, body_offset);
            builder.Finish(envelope);
        } else if constexpr (std::is_same_v<T, IpcShutdown>) {
            auto body_offset = CreateShutdownRequest(builder).Union();
            auto envelope = CreateIpcEnvelope(
                builder, message_id, IpcRequestBody_ShutdownRequest, body_offset);
            builder.Finish(envelope);
        }
    }, request);

    const auto* data = builder.GetBufferPointer();
    const auto size  = builder.GetSize();
    return WireFrame(data, data + size);
}

WireFrame IpcCodec::encode_reply(std::uint64_t message_id,
                                 const IpcResponse& response) {
    flatbuffers::FlatBufferBuilder builder(1024);

    std::visit([&](const auto& rep) {
        using T = std::decay_t<decltype(rep)>;
        if constexpr (std::is_same_v<T, IpcCreateCompositionResult>) {
            auto msg = builder.CreateString(rep.message);
            auto body_offset = CreateCreateCompositionReply(
                builder, rep.status, msg).Union();
            auto envelope = CreateIpcReplyEnvelope(
                builder, message_id, IpcReplyBody_CreateCompositionReply, body_offset);
            builder.Finish(envelope);
        } else if constexpr (std::is_same_v<T, IpcRenderFrameResult>) {
            auto msg = builder.CreateString(rep.message);
            auto out = rep.output_path.empty()
                ? flatbuffers::Offset<flatbuffers::String>{}
                : builder.CreateString(rep.output_path);
            auto body_offset = CreateRenderFrameReply(
                builder, rep.status, msg, out, rep.render_ms).Union();
            auto envelope = CreateIpcReplyEnvelope(
                builder, message_id, IpcReplyBody_RenderFrameReply, body_offset);
            builder.Finish(envelope);
        } else if constexpr (std::is_same_v<T, IpcStatusResult>) {
            auto msg = builder.CreateString(rep.message);
            auto body_offset = CreateStatusReply(builder, rep.status, msg).Union();
            auto envelope = CreateIpcReplyEnvelope(
                builder, message_id, IpcReplyBody_StatusReply, body_offset);
            builder.Finish(envelope);
        } else if constexpr (std::is_same_v<T, IpcShutdownResult>) {
            auto msg = builder.CreateString(rep.message);
            auto body_offset = CreateShutdownReply(builder, rep.status, msg).Union();
            auto envelope = CreateIpcReplyEnvelope(
                builder, message_id, IpcReplyBody_ShutdownReply, body_offset);
            builder.Finish(envelope);
        }
    }, response);

    const auto* data = builder.GetBufferPointer();
    const auto size  = builder.GetSize();
    return WireFrame(data, data + size);
}

std::optional<std::pair<std::uint64_t, IpcRequest>>
IpcCodec::decode_request(const WireFrame& frame) {
    if (frame.empty()) return std::nullopt;

    flatbuffers::Verifier verifier(frame.data(), frame.size());
    if (!verifier.VerifyBuffer<IpcEnvelope>(nullptr)) return std::nullopt;

    const auto* env = flatbuffers::GetRoot<IpcEnvelope>(frame.data());
    if (!env) return std::nullopt;

    const auto message_id = env->message_id();
    switch (env->body_type()) {
        case IpcRequestBody_CreateCompositionRequest: {
            const auto* req = env->body_as_CreateCompositionRequest();
            if (!req || !req->composition_id() || !req->descriptor_json()) return std::nullopt;
            IpcCreateComposition out;
            out.composition_id  = req->composition_id()->str();
            out.descriptor_json = req->descriptor_json()->str();
            return std::make_pair(message_id, IpcRequest{std::move(out)});
        }
        case IpcRequestBody_RenderFrameRequest: {
            const auto* req = env->body_as_RenderFrameRequest();
            if (!req || !req->composition_id()) return std::nullopt;
            IpcRenderFrame out;
            out.composition_id = req->composition_id()->str();
            out.frame_index    = req->frame_index();
            if (req->parameters()) {
                for (const auto* p : *req->parameters()) {
                    if (p && p->key() && p->value()) {
                        out.parameters.emplace_back(p->key()->str(), p->value()->str());
                    }
                }
            }
            if (req->output_path()) out.output_path = req->output_path()->str();
            return std::make_pair(message_id, IpcRequest{std::move(out)});
        }
        case IpcRequestBody_StatusRequest:
            return std::make_pair(message_id, IpcRequest{IpcStatusRequest{}});
        case IpcRequestBody_ShutdownRequest:
            return std::make_pair(message_id, IpcRequest{IpcShutdown{}});
        default:
            return std::nullopt;
    }
}

std::optional<std::pair<std::uint64_t, IpcResponse>>
IpcCodec::decode_reply(const WireFrame& frame) {
    if (frame.empty()) return std::nullopt;

    flatbuffers::Verifier verifier(frame.data(), frame.size());
    if (!verifier.VerifyBuffer<IpcReplyEnvelope>(nullptr)) return std::nullopt;

    const auto* env = flatbuffers::GetRoot<IpcReplyEnvelope>(frame.data());
    if (!env) return std::nullopt;

    const auto message_id = env->message_id();
    switch (env->body_type()) {
        case IpcReplyBody_CreateCompositionReply: {
            const auto* r = env->body_as_CreateCompositionReply();
            if (!r) return std::nullopt;
            IpcCreateCompositionResult out;
            out.status = r->status();
            if (r->message()) out.message = r->message()->str();
            return std::make_pair(message_id, IpcResponse{std::move(out)});
        }
        case IpcReplyBody_RenderFrameReply: {
            const auto* r = env->body_as_RenderFrameReply();
            if (!r) return std::nullopt;
            IpcRenderFrameResult out;
            out.status    = r->status();
            out.render_ms = r->render_ms();
            if (r->message()) out.message = r->message()->str();
            if (r->output_path()) out.output_path = r->output_path()->str();
            return std::make_pair(message_id, IpcResponse{std::move(out)});
        }
        case IpcReplyBody_StatusReply: {
            const auto* r = env->body_as_StatusReply();
            if (!r) return std::nullopt;
            IpcStatusResult out;
            out.status = r->status();
            if (r->message()) out.message = r->message()->str();
            return std::make_pair(message_id, IpcResponse{std::move(out)});
        }
        case IpcReplyBody_ShutdownReply: {
            const auto* r = env->body_as_ShutdownReply();
            if (!r) return std::nullopt;
            IpcShutdownResult out;
            out.status = r->status();
            if (r->message()) out.message = r->message()->str();
            return std::make_pair(message_id, IpcResponse{std::move(out)});
        }
        default:
            return std::nullopt;
    }
}

} // namespace chronon3d::ipc
