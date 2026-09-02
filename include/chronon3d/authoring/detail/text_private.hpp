// Included inside chronon3d::authoring::Text private section.

friend class Layer;
friend class testing::TextRunBuilderInspector;

[[nodiscard]] PendingTextRun& mutable_pending() noexcept {
    return *pending_;
}

PendingTextRun* pending_;
const CanvasInfo* canvas_;
