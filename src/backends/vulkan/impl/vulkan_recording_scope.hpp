#pragma once

// Recording-window TLS guard, included inside VulkanBackend::Impl.
struct RecordingResolveScope {
    const bool previous;
    RecordingResolveScope() noexcept;
    ~RecordingResolveScope();
    RecordingResolveScope(const RecordingResolveScope&) = delete;
    RecordingResolveScope& operator=(const RecordingResolveScope&) = delete;
};
