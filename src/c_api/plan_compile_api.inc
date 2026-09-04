chronon_status chronon_plan_compile_json_n(chronon_engine* engine, const char* source,
                                           uint64_t source_size,
                                           chronon_plan** out_plan) {
    if (!engine || !source || source_size == 0 || !out_plan)
        return set_error(engine, CHRONON_ERROR_INVALID_ARGUMENT, "invalid plan arguments");
    EngineUseGuard use(engine->in_use);
    if (!use.acquired()) return CHRONON_ERROR_BUSY;
    *out_plan = nullptr;
    try {
        const auto root = json::parse(source, source + source_size);
        auto plan = std::make_unique<chronon_plan>();
        plan->prepared = compile_plan(root, engine->resolver);
        plan->settings.width = plan->prepared.canvas.width;
        plan->settings.height = plan->prepared.canvas.height;
        *out_plan = plan.release();
        clear_error(engine);
        return CHRONON_OK;
    } catch (const PlanCompileError& error) {
        return set_plan_error(engine, error.what(), error.path, error.code, error.component);
    } catch (const std::exception& error) {
        return set_error(engine, CHRONON_ERROR_PARSE_FAILED, error.what());
    }
}

chronon_status chronon_plan_compile_json(chronon_engine* engine, const char* source,
                                         chronon_plan** out_plan) {
    if (!source) return set_error(engine, CHRONON_ERROR_INVALID_ARGUMENT, "invalid plan arguments");
    return chronon_plan_compile_json_n(engine, source, std::strlen(source), out_plan);
}

void chronon_plan_destroy(chronon_plan* plan) { delete plan; }
