#include <chronon3d/runtime/frame_execution_slot_ring.hpp>

#include <chrono>

namespace chronon3d::runtime {

FrameExecutionSlotRing::SlotLease::SlotLease(SlotLease&& other) noexcept
    : ring_(std::exchange(other.ring_, nullptr)),
      slot_(std::exchange(other.slot_, nullptr)) {}

FrameExecutionSlotRing::SlotLease& FrameExecutionSlotRing::SlotLease::operator=(
    SlotLease&& other) noexcept {
    if (this != &other) {
        release();
        ring_ = std::exchange(other.ring_, nullptr);
        slot_ = std::exchange(other.slot_, nullptr);
    }
    return *this;
}

FrameExecutionSlotRing::SlotLease::~SlotLease() {
    release();
}

void FrameExecutionSlotRing::SlotLease::mark_ready() noexcept {
    if (ring_ && slot_) ring_->mark_ready(slot_);
}

void FrameExecutionSlotRing::SlotLease::retire(
    std::shared_ptr<GpuCompletion> completion) noexcept {
    if (!ring_ || !slot_) return;
    ring_->retire_slot(slot_, std::move(completion));
    ring_ = nullptr;
    slot_ = nullptr;
}

void FrameExecutionSlotRing::SlotLease::release() noexcept {
    if (!ring_ || !slot_) return;
    ring_->release_slot(slot_);
    ring_ = nullptr;
    slot_ = nullptr;
}

FrameExecutionSlotRing::FrameExecutionSlotRing(std::size_t capacity)
    : gpu_completions_(capacity),
      slots_(capacity, gpu_completions_),
      evaluated_(capacity),
      rendered_(capacity) {}

std::size_t FrameExecutionSlotRing::capacity() const noexcept {
    return slots_.capacity();
}

std::size_t FrameExecutionSlotRing::depth() const noexcept {
    return slots_.capacity();
}

FrameExecutionSlot* FrameExecutionSlotRing::acquire_free_slot(
    const CancellationToken* token) {
    std::unique_lock lock(mutex_);
    const auto wait_start = std::chrono::steady_clock::now();
    for (;;) {
        reap_ready_completions_locked();
        if (closed_ || (token && token->is_cancelled())) return nullptr;
        if (auto* acquired = slots_.try_acquire(FrameSlotState::GpuWriting)) {
            const auto waited_us = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - wait_start).count());
            if (waited_us != 0) {
                wait_count_.fetch_add(1, std::memory_order_relaxed);
                wait_us_.fetch_add(waited_us, std::memory_order_relaxed);
            }
            return acquired;
        }
        cv_free_.wait_for(lock, std::chrono::milliseconds(1));
    }
}

FrameExecutionSlotRing::SlotLease FrameExecutionSlotRing::acquire_lease(
    const CancellationToken* token) {
    return SlotLease(this, acquire_free_slot(token));
}

FrameExecutionSlot* FrameExecutionSlotRing::acquire_for_evaluation() noexcept {
    std::lock_guard lock(mutex_);
    reap_ready_completions_locked();
    if (closed_) return nullptr;
    return slots_.try_acquire(FrameSlotState::Evaluating);
}

bool FrameExecutionSlotRing::publish_evaluated(FrameExecutionSlot& slot) noexcept {
    std::lock_guard lock(mutex_);
    if (!slots_.transition(
            slot, FrameSlotState::Evaluating, FrameSlotState::Evaluated)) {
        return false;
    }
    if (!evaluated_.try_push(slot.slot_id)) {
        slots_.set_state(slot, FrameSlotState::Evaluating);
        return false;
    }
    cv_free_.notify_all();
    return true;
}

FrameExecutionSlot* FrameExecutionSlotRing::acquire_for_render() noexcept {
    std::lock_guard lock(mutex_);
    FrameSlotId slot_id = 0;
    return evaluated_.try_pop(slot_id) ? &slots_.slot(slot_id) : nullptr;
}

bool FrameExecutionSlotRing::publish_rendered(FrameExecutionSlot& slot) noexcept {
    std::lock_guard lock(mutex_);
    if (!slots_.transition(
            slot, FrameSlotState::Evaluated, FrameSlotState::Rendered)) {
        return false;
    }
    if (!rendered_.try_push(slot.slot_id)) {
        slots_.set_state(slot, FrameSlotState::Evaluated);
        return false;
    }
    cv_free_.notify_all();
    return true;
}

FrameExecutionSlot* FrameExecutionSlotRing::acquire_for_encoding() noexcept {
    std::lock_guard lock(mutex_);
    FrameSlotId slot_id = 0;
    return rendered_.try_pop(slot_id) ? &slots_.slot(slot_id) : nullptr;
}

bool FrameExecutionSlotRing::begin_encoding(FrameExecutionSlot& slot) noexcept {
    return slots_.transition(
        slot, FrameSlotState::Rendered, FrameSlotState::Encoding);
}

bool FrameExecutionSlotRing::release_encoded(FrameExecutionSlot& slot) noexcept {
    if (slot.state.load(std::memory_order_acquire) != FrameSlotState::Encoding) {
        return false;
    }
    release_slot(&slot);
    return true;
}

bool FrameExecutionSlotRing::abort(FrameExecutionSlot& slot) noexcept {
    if (slot.state.load(std::memory_order_acquire) == FrameSlotState::Free) {
        return false;
    }
    release_slot(&slot);
    return true;
}

void FrameExecutionSlotRing::mark_ready(FrameExecutionSlot* slot) noexcept {
    if (slot) slots_.set_state(*slot, FrameSlotState::ReadyForEncode);
}

void FrameExecutionSlotRing::release_slot(FrameExecutionSlot* slot) noexcept {
    if (!slot) return;
    {
        std::lock_guard lock(mutex_);
        gpu_completions_.recycle(slot->slot_id);
        slot->native_surface_ptr = 0;
        slot->gpu_ready_sync = 0;
        slots_.release(*slot);
    }
    cv_free_.notify_one();
}

void FrameExecutionSlotRing::close() noexcept {
    {
        std::lock_guard lock(mutex_);
        closed_ = true;
    }
    cv_free_.notify_all();
}

std::size_t FrameExecutionSlotRing::busy_count() const noexcept {
    return slots_.busy_count();
}

std::uint64_t FrameExecutionSlotRing::wait_count() const noexcept {
    return wait_count_.load(std::memory_order_relaxed);
}

std::uint64_t FrameExecutionSlotRing::wait_us() const noexcept {
    return wait_us_.load(std::memory_order_relaxed);
}

std::size_t FrameExecutionSlotRing::in_flight() const noexcept {
    return busy_count();
}

std::size_t FrameExecutionSlotRing::rendered_depth() const noexcept {
    std::lock_guard lock(mutex_);
    return rendered_.size();
}

void FrameExecutionSlotRing::reset() noexcept {
    {
        std::lock_guard lock(mutex_);
        evaluated_.clear();
        rendered_.clear();
        gpu_completions_.reset();
        slots_.reset();
    }
    cv_free_.notify_all();
}

void FrameExecutionSlotRing::retire_slot(
    FrameExecutionSlot* slot,
    std::shared_ptr<GpuCompletion> completion) noexcept {
    if (!slot) return;
    {
        std::lock_guard lock(mutex_);
        gpu_completions_.retire(slot->slot_id, std::move(completion));
        slots_.set_state(*slot, FrameSlotState::Encoding);
    }
    cv_free_.notify_all();
}

FrameExecutionSlot& FrameExecutionSlotRing::slot(std::size_t index) {
    return slots_.slot(index);
}

const FrameExecutionSlot& FrameExecutionSlotRing::slot(std::size_t index) const {
    return slots_.slot(index);
}

void FrameExecutionSlotRing::reap_ready_completions_locked() noexcept {
    for (FrameSlotId slot_id = 0; slot_id < slots_.capacity(); ++slot_id) {
        if (!gpu_completions_.has_completion(slot_id) ||
            !gpu_completions_.completion_ready(slot_id)) {
            continue;
        }
        auto& completed = slots_.slot(slot_id);
        gpu_completions_.recycle(slot_id);
        completed.native_surface_ptr = 0;
        completed.gpu_ready_sync = 0;
        slots_.release(completed);
    }
}

} // namespace chronon3d::runtime
