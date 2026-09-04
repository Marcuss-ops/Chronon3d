void chronon_buffer_free(chronon_engine* engine, chronon_frame_buffer* buffer) {
    if (!engine || !buffer) return;
    std::free(engine->buffer);
    engine->buffer = nullptr;
    engine->buffer_size = 0;
    std::memset(buffer, 0, sizeof(*buffer));
}
