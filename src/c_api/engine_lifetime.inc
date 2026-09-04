chronon_engine* chronon_engine_create(const chronon_engine_config* config) {
    try {
        std::string error;
        if (validate_engine_config(config, error) != CHRONON_OK) return nullptr;
        chronon3d::sdk::RenderSettings settings;
        auto* engine = new chronon_engine(settings);
        if (config && config->assets_root) {
            engine->engine.set_assets_root(config->assets_root);
            engine->resolver.mount(config->assets_root);
        }
        return engine;
    } catch (...) {
        return nullptr;
    }
}

chronon_status chronon_engine_create_v2(const chronon_engine_config* config,
                                        chronon_engine** out_engine,
                                        chronon_error_info* out_error) {
    if (out_error && out_error->struct_size >= sizeof(chronon_error_info)) {
        out_error->status = CHRONON_OK;
        out_error->message = nullptr;
    }
    if (!out_engine) {
        write_error_info(out_error, CHRONON_ERROR_INVALID_ARGUMENT, "out_engine is null");
        return CHRONON_ERROR_INVALID_ARGUMENT;
    }
    *out_engine = nullptr;
    try {
        std::string error;
        const auto status = validate_engine_config(config, error);
        if (status != CHRONON_OK) {
            write_error_info(out_error, status, std::move(error));
            return status;
        }
        chronon3d::sdk::RenderSettings settings;
        auto* engine = new chronon_engine(settings);
        if (config && config->assets_root) {
            engine->engine.set_assets_root(config->assets_root);
            engine->resolver.mount(config->assets_root);
        }
        *out_engine = engine;
        return CHRONON_OK;
    } catch (const std::exception& error) {
        write_error_info(out_error, CHRONON_ERROR_UNKNOWN, error.what());
        return CHRONON_ERROR_UNKNOWN;
    } catch (...) {
        write_error_info(out_error, CHRONON_ERROR_UNKNOWN, "unknown engine creation failure");
        return CHRONON_ERROR_UNKNOWN;
    }
}

void chronon_engine_destroy(chronon_engine* engine) {
    if (!engine) return;
    std::free(engine->buffer);
    delete engine;
}
