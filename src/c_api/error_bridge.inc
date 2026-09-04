const char* chronon_version_string(void) {
    return CHRONON3D_PROJECT_VERSION_STRING;
}

const char* chronon_status_name(chronon_status status) {
    switch (status) {
        case CHRONON_OK: return "OK";
        case CHRONON_ERROR_UNKNOWN: return "UNKNOWN";
        case CHRONON_ERROR_INVALID_ARGUMENT: return "INVALID_ARGUMENT";
        case CHRONON_ERROR_PARSE_FAILED: return "PARSE_FAILED";
        case CHRONON_ERROR_RENDER_FAILED: return "RENDER_FAILED";
        case CHRONON_ERROR_IO_FAILED: return "IO_FAILED";
        case CHRONON_ERROR_UNSUPPORTED: return "UNSUPPORTED";
        case CHRONON_ERROR_CANCELLED: return "CANCELLED";
        case CHRONON_ERROR_ABI_MISMATCH: return "ABI_MISMATCH";
        case CHRONON_ERROR_BUFFER_TOO_SMALL: return "BUFFER_TOO_SMALL";
        case CHRONON_ERROR_BUSY: return "BUSY";
        case CHRONON_ERROR_ASSET_CHANGED: return "ASSET_CHANGED";
        case CHRONON_ERROR_BUDGET_EXCEEDED: return "BUDGET_EXCEEDED";
        case CHRONON_ERROR_PREFLIGHT_FAILED: return "PREFLIGHT_FAILED";
        case CHRONON_ERROR_INVALID_PLAN: return "INVALID_PLAN";
        case CHRONON_ERROR_UNSUPPORTED_SCHEMA: return "UNSUPPORTED_SCHEMA";
        case CHRONON_ERROR_ASSET_NOT_FOUND: return "ASSET_NOT_FOUND";
        case CHRONON_ERROR_DECODE_FAILED: return "DECODE_FAILED";
        case CHRONON_ERROR_ENCODE_FAILED: return "ENCODE_FAILED";
        case CHRONON_ERROR_OUT_OF_MEMORY: return "OUT_OF_MEMORY";
    }
    return "UNKNOWN";
}

uint32_t chronon_abi_version(void) { return 2; }

const char* chronon_engine_last_error(chronon_engine* engine) {
    return engine ? engine->last_error.c_str() : "invalid engine";
}

chronon_status chronon_engine_last_error_info(chronon_engine* engine,
                                              chronon_error_info* out) {
    constexpr std::size_t base_size =
        offsetof(chronon_error_info, message) + sizeof(chronon_error_info::message);
    if (!engine || !out || out->struct_size < base_size)
        return CHRONON_ERROR_INVALID_ARGUMENT;
    out->status = engine->last_status;
    out->message = engine->last_error.empty() ? nullptr : engine->last_error.c_str();
    if (out->struct_size >= sizeof(chronon_error_info)) {
        out->code = engine->last_code.empty() ? nullptr : engine->last_code.c_str();
        out->component = engine->last_component.empty() ? nullptr : engine->last_component.c_str();
        out->node_id = engine->last_node_id.empty() ? nullptr : engine->last_node_id.c_str();
        out->asset = engine->last_asset.empty() ? nullptr : engine->last_asset.c_str();
    }
    return CHRONON_OK;
}
