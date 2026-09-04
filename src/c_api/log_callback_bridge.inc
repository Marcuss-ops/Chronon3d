chronon_status chronon_engine_set_log_callback(
    chronon_engine* engine, chronon_log_callback callback, void* user) {
    if (!engine) return CHRONON_ERROR_INVALID_ARGUMENT;
    if (!callback) {
        engine->engine.set_log_callback({}, nullptr);
        return CHRONON_OK;
    }
    engine->engine.set_log_callback(
        [callback](chronon3d::sdk::LogLevel level,
                   std::string_view component,
                   std::string_view message, void* callback_user) {
            callback(static_cast<int>(level),
                     std::string(component).c_str(),
                     std::string(message).c_str(), callback_user);
        }, user);
    return CHRONON_OK;
}
