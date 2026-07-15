// Included inside chronon3d::authoring::Text public section.

Text& style(std::string_view id, const StyleRegistry& registry) {
    const auto resolved = registry.resolve(id);
    if (!resolved.has_value()) {
        last_style_outcome_ = ResolutionOutcome::Missing;
        return *this;
    }
    apply_text_style(*resolved);
    last_style_outcome_ = ResolutionOutcome::Found;
    return *this;
}

Text& style(std::string_view id) {
    if (style_registry_ == nullptr) {
        last_style_outcome_ = ResolutionOutcome::RegistryUnavailable;
        return *this;
    }
    const auto resolved = style_registry_->resolve(id);
    if (!resolved.has_value()) {
        last_style_outcome_ = ResolutionOutcome::Missing;
        return *this;
    }
    apply_text_style(*resolved);
    last_style_outcome_ = ResolutionOutcome::Found;
    return *this;
}

Text& motion(std::string_view id, const MotionRegistry& registry) {
    const auto resolved = registry.resolve(id);
    if (!resolved.has_value()) {
        last_motion_outcome_ = ResolutionOutcome::Missing;
        return *this;
    }
    pending_->params.animators.push_back(*resolved);
    last_motion_outcome_ = ResolutionOutcome::Found;
    return *this;
}

Text& motion(std::string_view id) {
    if (motion_registry_ == nullptr) {
        last_motion_outcome_ = ResolutionOutcome::RegistryUnavailable;
        return *this;
    }
    const auto resolved = motion_registry_->resolve(id);
    if (!resolved.has_value()) {
        last_motion_outcome_ = ResolutionOutcome::Missing;
        return *this;
    }
    pending_->params.animators.push_back(*resolved);
    last_motion_outcome_ = ResolutionOutcome::Found;
    return *this;
}

template <class Fn>
Text& configure_core(Fn&& mutator) {
    mutator(pending_->params);
    return *this;
}

[[nodiscard]] ResolutionOutcome last_style_outcome() const noexcept {
    return last_style_outcome_;
}

[[nodiscard]] ResolutionOutcome last_motion_outcome() const noexcept {
    return last_motion_outcome_;
}

[[nodiscard]] const PendingTextRun& pending() const noexcept {
    return *pending_;
}

[[nodiscard]] const StyleRegistry* ambient_style_registry() const noexcept {
    return style_registry_;
}

[[nodiscard]] const MotionRegistry* ambient_motion_registry() const noexcept {
    return motion_registry_;
}
