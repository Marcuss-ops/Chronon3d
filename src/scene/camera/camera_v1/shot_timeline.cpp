// ==============================================================================
// chronon3d/src/scene/camera/camera_v1/shot_timeline.cpp
//
// ShotTimeline implementation with 5 built-in transitions.
//
// Fixes:
//   - add_shot validates (no silent mutation)
//   - validate() catches gaps/overlap/negative-duration
//   - Quaternion slerp for rotation (not euler mix)
//   - Endpoint parity: transition(0)==from, transition(1)==to
//   - Local frame time per shot
//   - ShotTimelineSession for persistent per-shot constraint state
//   - t reaches 1.0 at last overlap frame
//   - transition_out semantics (current shot's exit transition)
// ==============================================================================
#include <chronon3d/scene/camera/camera_v1/shot_timeline.hpp>

// P3-H + TICKET-CAMERA-FULL-LINUX sub-ticket C — the cache-controlled
// evaluate() path uses `lease.session()` which returns `CameraSession&`.
// The session type lives in the internal/ namespace (P3-H boundary
// contract); we include the full type here at the implementation TU
// because this is the only place where session fields are accessed.
// Public headers (shot_timeline.hpp + camera_session_cache.hpp) keep
// forward-declared CameraSession so the public manifest stays
// compile-clean for downstream consumers.
#include <chronon3d/internal/scene/camera/v1/camera_session.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>           // std::snprintf for derive_camera_id hex
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <stdexcept>
#include <sstream>

namespace chronon3d::camera_v1 {

// =========================================================================
// Helper: quaternion slerp for Camera2_5D rotation (euler → quat → slerp → euler).
// =========================================================================
namespace {

inline glm::quat euler_to_quat(const Vec3& euler_deg) {
    return glm::quat(glm::radians(euler_deg));
}

inline Vec3 quat_to_euler(const glm::quat& q) {
    return glm::degrees(glm::eulerAngles(q));
}

inline Vec3 slerp_rotation(const Vec3& from_euler, const Vec3& to_euler, float t) {
    glm::quat q0 = euler_to_quat(from_euler);
    glm::quat q1 = euler_to_quat(to_euler);
    // Ensure shortest arc.
    if (glm::dot(q0, q1) < 0.0f) q1 = -q1;
    glm::quat qs = glm::slerp(q0, q1, t);
    return quat_to_euler(qs);
}

} // namespace

// =========================================================================
// ShotTimeline
// =========================================================================

bool ShotTimeline::add_shot(CameraShot shot) {
    // Validate shot duration.
    if (shot.end_frame <= shot.start_frame) return false;
    if (shot.transition_frames < 0) return false;

    // Validate adjacent to last shot: no undeclared overlap, no gap.
    if (!shots_.empty()) {
        const auto& prev = shots_.back();
        if (shot.start_frame < prev.end_frame) {
            // Overlap — must have transition declared on the previous shot.
            if (prev.transition_frames <= 0) return false;
            int overlap = prev.end_frame - shot.start_frame;
            if (overlap > prev.transition_frames) return false;
        }
        // Gap (start > prev.end) is allowed but logged as a validation warning.
    }

    shots_.push_back(std::move(shot));
    return true;
}

ShotTimelineValidationResult ShotTimeline::validate() const {
    ShotTimelineValidationResult r;
    for (int i = 0; i < static_cast<int>(shots_.size()); ++i) {
        const auto& s = shots_[i];
        if (s.end_frame <= s.start_frame) {
            r.ok = false;
            r.errors.push_back("shot " + std::to_string(i) +
                " (\"" + s.name + "\"): zero or negative duration");
        }
        if (s.transition_frames > (s.end_frame - s.start_frame)) {
            r.ok = false;
            r.errors.push_back("shot " + std::to_string(i) +
                " (\"" + s.name + "\"): transition longer than shot");
        }
        if (i > 0) {
            const auto& prev = shots_[i - 1];
            if (s.start_frame < prev.end_frame && prev.transition_frames <= 0) {
                r.ok = false;
                r.errors.push_back("shot " + std::to_string(i) +
                    " (\"" + s.name + "\"): overlaps previous shot with no transition");
            }
            if (s.start_frame > prev.end_frame) {
                r.ok = false;
                r.errors.push_back("gap between shot " + std::to_string(i - 1) +
                    " (\"" + prev.name + "\") and shot " + std::to_string(i) +
                    " (\"" + s.name + "\"): " + std::to_string(s.start_frame - prev.end_frame) + " frames");
            }
        }
    }
    return r;
}

const CameraShot* ShotTimeline::find_shot(int frame) const {
    for (auto& s : shots_) {
        if (frame >= s.start_frame && frame < s.end_frame)
            return &s;
    }
    return nullptr;
}

ShotTimeline::ShotPair ShotTimeline::find_pair(int frame) const {
    for (int i = 0; i < static_cast<int>(shots_.size()); ++i) {
        if (frame >= shots_[i].start_frame && frame < shots_[i].end_frame) {
            const CameraShot* next = (i + 1 < static_cast<int>(shots_.size()))
                ? &shots_[i + 1] : nullptr;
            return {i, &shots_[i], next};
        }
    }
    return {-1, nullptr, nullptr};
}

int ShotTimeline::total_frames() const {
    if (shots_.empty()) return 0;
    return shots_.back().end_frame;
}

// =========================================================================
// Built-in transitions — all satisfy endpoint parity contract.
// =========================================================================

namespace {

// --- Cut ------------------------------------------------------------------
class CutTransition final : public CameraTransition {
public:
    std::string id() const override { return "camera.transition.cut"; }
    Camera2_5D evaluate(float t, const Camera2_5D& from, const Camera2_5D& to) const override {
        return (t < 1.0f) ? from : to;  // show 'from' until t=1, then 'to'
    }
};

// --- SmoothBlend ----------------------------------------------------------
class SmoothBlendTransition final : public CameraTransition {
public:
    std::string id() const override { return "camera.transition.smooth_blend"; }
    Camera2_5D evaluate(float t, const Camera2_5D& from, const Camera2_5D& to) const override {
        // Bug 4 fix: endpoint parity (matches contract transition(0)==from,
        // transition(1)==to) AND preserve every non-animated field (is_animated,
        // enabled, lens, hierarchy, motion_blur, dof.use_* etc.) by initializing
        // out from `from` and only mutating the animated channels.
        if (t <= 0.0f) return from;
        if (t >= 1.0f) return to;
        Camera2_5D out = from;
        out.position = glm::mix(from.position, to.position, t);
        if (from.point_of_interest_enabled && to.point_of_interest_enabled) {
            out.point_of_interest = glm::mix(from.point_of_interest, to.point_of_interest, t);
            out.point_of_interest_enabled = true;
        } else {
            out.point_of_interest = from.point_of_interest;
            out.point_of_interest_enabled = from.point_of_interest_enabled;
        }
        out.rotation = slerp_rotation(from.rotation, to.rotation, t);
        out.fov_deg = glm::mix(from.fov_deg, to.fov_deg, t);
        out.zoom = glm::mix(from.zoom, to.zoom, t);
        out.dof.focus_distance = glm::mix(from.dof.focus_distance, to.dof.focus_distance, t);
        return out;
    }
};

// --- Push -----------------------------------------------------------------
class PushTransition final : public CameraTransition {
public:
    std::string id() const override { return "camera.transition.push"; }
    Camera2_5D evaluate(float t, const Camera2_5D& from, const Camera2_5D& to) const override {
        // Bug 4 fix: see SmoothBlend above.
        if (t <= 0.0f) return from;
        if (t >= 1.0f) return to;
        float et = t * (2.0f - t);  // ease-out
        Camera2_5D out = from;
        out.position = glm::mix(from.position, to.position, et);
        if (from.point_of_interest_enabled && to.point_of_interest_enabled) {
            out.point_of_interest = glm::mix(from.point_of_interest, to.point_of_interest, t);
            out.point_of_interest_enabled = true;
        } else {
            out.point_of_interest = from.point_of_interest;
            out.point_of_interest_enabled = from.point_of_interest_enabled;
        }
        out.rotation = slerp_rotation(from.rotation, to.rotation, t);
        out.fov_deg = glm::mix(from.fov_deg, to.fov_deg, t);
        out.zoom = glm::mix(from.zoom, to.zoom, t);
        out.dof.focus_distance = glm::mix(from.dof.focus_distance, to.dof.focus_distance, t);
        return out;
    }
};

// --- WhipPan --------------------------------------------------------------
class WhipPanTransition final : public CameraTransition {
public:
    std::string id() const override { return "camera.transition.whip_pan"; }
    Camera2_5D evaluate(float t, const Camera2_5D& from, const Camera2_5D& to) const override {
        // Bug 4 fix: see SmoothBlend above.
        if (t <= 0.0f) return from;
        if (t >= 1.0f) return to;
        float et = t * t * (3.0f - 2.0f * t);  // smoothstep
        Camera2_5D out = from;
        out.position = glm::mix(from.position, to.position, t);
        out.rotation = slerp_rotation(from.rotation, to.rotation, et);
        if (from.point_of_interest_enabled && to.point_of_interest_enabled) {
            out.point_of_interest = glm::mix(from.point_of_interest, to.point_of_interest, et);
            out.point_of_interest_enabled = true;
        } else {
            out.point_of_interest = from.point_of_interest;
            out.point_of_interest_enabled = from.point_of_interest_enabled;
        }
        out.fov_deg = glm::mix(from.fov_deg, to.fov_deg, t);
        out.zoom = glm::mix(from.zoom, to.zoom, t);
        out.dof.focus_distance = glm::mix(from.dof.focus_distance, to.dof.focus_distance, t);
        return out;
    }
};

// --- FocusHandoff ---------------------------------------------------------
class FocusHandoffTransition final : public CameraTransition {
public:
    std::string id() const override { return "camera.transition.focus_handoff"; }
    Camera2_5D evaluate(float t, const Camera2_5D& from, const Camera2_5D& to) const override {
        // Bug 4 fix: see SmoothBlend above.
        if (t <= 0.0f) return from;
        if (t >= 1.0f) return to;
        Camera2_5D out = from;
        out.position = glm::mix(from.position, to.position, t);
        out.rotation = slerp_rotation(from.rotation, to.rotation, t);
        out.fov_deg = glm::mix(from.fov_deg, to.fov_deg, t);
        out.zoom = glm::mix(from.zoom, to.zoom, t);
        out.dof.focus_distance = glm::mix(from.dof.focus_distance, to.dof.focus_distance, t);
        if (from.point_of_interest_enabled && to.point_of_interest_enabled) {
            out.point_of_interest = glm::mix(from.point_of_interest, to.point_of_interest, t);
            out.point_of_interest_enabled = true;
        }
        return out;
    }
};

} // namespace

// =========================================================================
// Default transition factories
// =========================================================================

std::shared_ptr<CameraTransition> ShotTimelineResolver::default_cut() {
    return std::make_shared<CutTransition>();
}
std::shared_ptr<CameraTransition> ShotTimelineResolver::default_smooth_blend() {
    return std::make_shared<SmoothBlendTransition>();
}
std::shared_ptr<CameraTransition> ShotTimelineResolver::default_push() {
    return std::make_shared<PushTransition>();
}
std::shared_ptr<CameraTransition> ShotTimelineResolver::default_whip_pan() {
    return std::make_shared<WhipPanTransition>();
}
std::shared_ptr<CameraTransition> ShotTimelineResolver::default_focus_handoff() {
    return std::make_shared<FocusHandoffTransition>();
}

// =========================================================================
// ShotTimelineResolver
// =========================================================================

ShotTimelineResolver::ShotTimelineResolver(std::shared_ptr<ShotTimeline> timeline,
                                           const CameraTransitionCatalog* catalog)
    : timeline_(std::move(timeline)) {
    // Pull transitions from the catalog (single source of truth, populated by
    // register_camera_v1_builtins()). Fall back to local defaults when catalog
    // is null (e.g. tests that bypassed the bootstrap).
    const auto* ctlg = catalog;
    auto fetch = [&](CameraTransitionKind k,
                     std::shared_ptr<CameraTransition> fallback)
        -> std::shared_ptr<CameraTransition> {
        auto t = ctlg ? ctlg->create(k) : nullptr;
        return t ? t : fallback;
    };
    transitions_[CameraTransitionKind::Cut]          = fetch(CameraTransitionKind::Cut,          default_cut());
    transitions_[CameraTransitionKind::SmoothBlend]  = fetch(CameraTransitionKind::SmoothBlend,  default_smooth_blend());
    transitions_[CameraTransitionKind::Push]         = fetch(CameraTransitionKind::Push,         default_push());
    transitions_[CameraTransitionKind::WhipPan]      = fetch(CameraTransitionKind::WhipPan,      default_whip_pan());
    transitions_[CameraTransitionKind::FocusHandoff] = fetch(CameraTransitionKind::FocusHandoff, default_focus_handoff());
}

void ShotTimelineResolver::set_transition(CameraTransitionKind kind,
                                           std::shared_ptr<CameraTransition> t) {
    if (t) transitions_[kind] = std::move(t);
}

std::shared_ptr<CameraTransition> ShotTimelineResolver::get_transition(
        CameraTransitionKind kind) const {
    auto it = transitions_.find(kind);
    if (it != transitions_.end()) return it->second;
    return default_cut();
}

// =============================================================================
// P3-H + TICKET-CAMERA-FULL-LINUX sub-ticket C — 6-field ripple-through
// diagnostic enrichment.
//
// `derive_camera_id(prog)` — returns the canonical id used by the
// renderer-facing `CameraResolveDiagnostic.camera_id` field.  Falls back
// to fingerprint hex when the descriptor has no id, and to "uncompiled"
// when the program is not yet compiled (program-only standalone path).
// =============================================================================
namespace {
inline std::string derive_camera_id(const CameraProgram& prog,
                                     int shot_idx) {
    if (const auto* d = prog.descriptor()) {
        if (!d->id.empty()) return d->id;
        if (prog.is_compiled()) {
            char hex[24];
            std::snprintf(hex, sizeof(hex),
                          "shot%d-fp-%016llx",
                          shot_idx,
                          static_cast<unsigned long long>(prog.fingerprint()));
            return std::string(hex);
        }
    }
    return "uncompiled-shot" + std::to_string(shot_idx);
}

// Heuristic code-derivation.  Greps `d.message` for canonical substrings
// so a serialised log can be filtered on `code == "Uncompiled"` etc.
// These patterns match the failure-messages emitted by:
inline std::string derive_code_from_message(const std::string& msg) {
    if (msg.find("Uncompiled") != std::string::npos ||
        msg.find("uncompiled") != std::string::npos ||
        msg.find("compile_camera") != std::string::npos) {
        return "Uncompiled";
    }
    if (msg.find("ConstraintFailure") != std::string::npos ||
        msg.find("constraint") != std::string::npos) {
        return "ConstraintFailure";
    }
    if (msg.find("LookAtLayer") != std::string::npos ||
        msg.find("look-at") != std::string::npos ||
        msg.find("MissingTransforms") != std::string::npos) {
        return "LookAtLayerMissingTarget";
    }
    if (msg.find("TransitionEvaluationFailed") != std::string::npos) {
        return "TransitionEvaluationFailed";
    }
    return "ProgramDiagnostic";
}

// In-place enrich `EvaluatedCamera::resolve_diagnostics` with the 6-field
// sub-ticket C contract.  Reads from the program's pre-existing
// `diagnostics` vector (program-level) and emits a parallel
// `resolve_diagnostics` vector (timeline-level) with `shot_index` +
// `sample_time_seconds` + `camera_id` populated.  Severity is bit-cast
// from `CameraProgramDiagnostic::Severity` (the two enums share the same
// 3 values — Info / Warning / Error).
inline void enrich_resolve_diagnostics(EvaluatedCamera& eval,
                                         const CameraProgram& prog,
                                         int                shot_idx,
                                         double             sample_time_s) {
    if (eval.diagnostics.empty()) return;
    eval.resolve_diagnostics.clear();
    eval.resolve_diagnostics.reserve(eval.diagnostics.size());
    for (const auto& d : eval.diagnostics) {
        CameraResolveDiagnostic rd;
        rd.camera_id           = derive_camera_id(prog, shot_idx);
        rd.shot_index          = shot_idx;
        rd.sample_time_seconds = sample_time_s;
        rd.severity            = static_cast<CameraResolveDiagnostic::Severity>(
            static_cast<int>(d.severity));
        rd.code                = derive_code_from_message(d.message);
        rd.message             = d.message;
        eval.resolve_diagnostics.push_back(std::move(rd));
    }
}
} // anonymous namespace

chronon3d::Result<EvaluatedCamera, CameraEvaluationError>
ShotTimelineResolver::evaluate(int frame,
                                ShotTimelineSession& timeline_session,
                                FrameRate             fps) const {
    if (!timeline_ || timeline_->empty())
        return EvaluatedCamera{Camera2_5D{}, {}, {}};

    auto pair = timeline_->find_pair(frame);
    if (!pair.current)
        return EvaluatedCamera{Camera2_5D{}, {}, {}};

    const CameraShot& shot = *pair.current;
    int local_frame = frame - shot.start_frame;  // local time

    // P3-H + sub-ticket C — cache-controlled session acquisition so a
    // direct frame-100 render pre-rolls state as if frames 0..99 had been
    // committed sequentially.  The cache is private to this resolver
    // (mutable CameraSessionCache cache_) so simultaneous render-jobs
    // each own their own session state (WP-3 isolation per the test
    // `random_access_two_simultaneous_render_jobs_isolated` in
    // tests/scene/camera/test_shot_timeline_random_access.cpp).

    // Check if we're in a transition overlap with the next shot.
    if (pair.next && shot.transition_frames > 0 &&
        shot.transition_out != CameraTransitionKind::Cut &&
        frame >= shot.end_frame - shot.transition_frames) {

        int overlap_start = shot.end_frame - shot.transition_frames;
        int local_idx = frame - overlap_start;

        // t ∈ [0, 1] inclusive: last overlap frame reaches t=1.
        int denom = std::max(1, shot.transition_frames - 1);
        float t = static_cast<float>(local_idx) / static_cast<float>(denom);
        t = std::clamp(t, 0.0f, 1.0f);

        // Use persistent sessions so DampedFollow survives across frames.
        // CameraEvalContext has no base_target — the compiled path reads
        // base state from the descriptor (CameraBaseSpec) directly.
        // CAM-05 / TICKET-A3-CTX-FRAMERATE: FrameRate is forwarded bit-exact
        // from the resolver caller (no 30 fps fixture inside the resolver).
        auto ctx_from = CameraEvalContext::at(local_frame, fps);

        int next_local = frame - pair.next->start_frame;
        auto ctx_to = CameraEvalContext::at(std::max(0, next_local), fps);

        // P3-H + sub-ticket C — random-access parity for transition overlap.
        auto lease_from = cache_.acquire(shot.program, pair.idx,
                                          shot.start_frame, frame, fps);
        auto  eval_from = shot.program.evaluate(ctx_from, lease_from.session());
        if (!eval_from) {
            return CameraEvaluationError{
                .code    = CameraErrorCode::TransitionEvaluationFailed,
                .message = std::string("ShotTimeline transition from-shot '") + shot.name +
                           "' evaluate failed: " + eval_from.error().message
            };
        }
        lease_from.commit(eval_from.value().camera);

        auto lease_to = cache_.acquire(pair.next->program, pair.idx + 1,
                                        pair.next->start_frame, frame, fps);
        auto  eval_to = pair.next->program.evaluate(ctx_to, lease_to.session());
        if (!eval_to) {
            return CameraEvaluationError{
                .code    = CameraErrorCode::TransitionEvaluationFailed,
                .message = std::string("ShotTimeline transition to-shot '") + pair.next->name +
                           "' evaluate failed: " + eval_to.error().message
            };
        }
        lease_to.commit(eval_to.value().camera);

        Camera2_5D from_cam = eval_from.value().camera;
        Camera2_5D to_cam   = eval_to.value().camera;
        auto transition = get_transition(shot.transition_out);

        // P3-H + sub-ticket C — merged ripple-through: both shots'
        // resolve_diagnostics are concatenated with their respective
        // shot_indices preserved (so a downstream dashboard can group
        // diagnostics by shot_idx 0 / 1 + filter by code / severity).
        EvaluatedCamera merged;
        merged.camera = transition->evaluate(t, from_cam, to_cam);
        EvaluatedCamera from_eval = eval_from.value();
        EvaluatedCamera to_eval   = eval_to.value();
        enrich_resolve_diagnostics(from_eval, shot.program, pair.idx,
                                    ctx_from.sample_time.seconds());
        enrich_resolve_diagnostics(to_eval, pair.next->program, pair.idx + 1,
                                    ctx_to.sample_time.seconds());
        merged.diagnostics.clear();
        merged.resolve_diagnostics = std::move(from_eval.resolve_diagnostics);
        auto tail = std::move(to_eval.resolve_diagnostics);
        for (auto& rd : tail) merged.resolve_diagnostics.push_back(std::move(rd));
        return merged;
    }

    // No transition — evaluate the current shot directly with local time.
    // CameraEvalContext has no base_target; the compiled path uses the
    // descriptor's CameraBaseSpec directly for base state.
    // CAM-05 / TICKET-A3-CTX-FRAMERATE: FrameRate is forwarded bit-exact
    // from the resolver caller (no 30 fps fixture inside the resolver).
    auto ctx = CameraEvalContext::at(local_frame, fps);

    // P3-H + sub-ticket C — observe Cut transitions the first time the
    // resolver enters frame shot.end_frame for a shot whose exit is Cut.
    // (The cache.apply_cut_between is also called by the OPP integration
    // before cross-shot acquisition; marking the observer pattern here
    // keeps the lease-acquisition pre-roll consistent even when calls
    // go directly through the public evaluate() path.)
    if (pair.next && shot.transition_out == CameraTransitionKind::Cut &&
        frame == shot.end_frame - 1) {
        cache_.observe_cut_between(pair.idx, pair.idx + 1);
    }

    // P3-H + sub-ticket C — cache-controlled acquisition for the single-shot
    // path.  The cache handles pre-roll walk (stateful programs) +
    // fingerprint invalidation + Cut-reset so direct-frame-100 produces
    // bit-exact state with sequential-0..100.
    auto lease = cache_.acquire(shot.program, pair.idx,
                                 shot.start_frame, frame, fps);
    auto eval_result = shot.program.evaluate(ctx, lease.session());
    if (!eval_result) {
        return CameraEvaluationError{
            .code    = CameraErrorCode::TransitionEvaluationFailed,
            .message = std::string("ShotTimeline evaluate failed for shot '") + shot.name +
                       "': " + eval_result.error().message
        };
    }
    // P3-H + sub-ticket C — lease-commit persists the working session
    // back into the cache's checkpoint slot (this is the commit step that
    // makes the next direct-frame-eval bit-exact with a sequential render).
    lease.commit(eval_result.value().camera);

    // P3-H + sub-ticket C — populate the 6-field ripple-through surface.
    EvaluatedCamera enriched = eval_result.value();
    enrich_resolve_diagnostics(enriched, shot.program, pair.idx,
                                ctx.sample_time.seconds());
    return enriched;
}

// =========================================================================
// CameraTransitionCatalog
// =========================================================================

void CameraTransitionCatalog::register_transition(CameraTransitionKind kind, Factory f) {
    if (!f) throw std::invalid_argument("CameraTransitionCatalog: null factory");
    std::unique_lock<std::shared_mutex> lk(mu_);
    // Relaxed check is sufficient here: the unique_lock already serialises
    // us with freeze()'s store, and register_transition is not on any
    // lock-free fast path.
    if (frozen_.load(std::memory_order_relaxed))
        throw std::logic_error("CameraTransitionCatalog: frozen");
    factories_[kind] = std::move(f);
}

std::shared_ptr<CameraTransition> CameraTransitionCatalog::create(
        CameraTransitionKind kind) const {
    // Lock-skip optimisation: after freeze(), the factories_ map is
    // immutable. The acquire-load of frozen_ synchronises with freeze()'s
    // release-store, so any factories_[k] write committed BEFORE freeze
    // is safely visible here. Zero-cost on the hot path that runs every
    // frame inside ShotTimelineResolver::evaluate().
    if (frozen_.load(std::memory_order_acquire)) {
        auto it = factories_.find(kind);
        return (it != factories_.end()) ? it->second() : nullptr;
    }
    std::shared_lock<std::shared_mutex> lk(mu_);
    auto it = factories_.find(kind);
    return (it != factories_.end()) ? it->second() : nullptr;
}

bool CameraTransitionCatalog::has(CameraTransitionKind kind) const {
    // Same lock-skip optimisation as create(): once the catalog is
    // frozen, the factories_ map is read-only and the count check is
    // safe without holding the mutex. Acquire-load pairs with freeze()'s
    // release-store to guarantee happens-before visibility.
    if (frozen_.load(std::memory_order_acquire)) {
        return factories_.count(kind) > 0;
    }
    std::shared_lock<std::shared_mutex> lk(mu_);
    return factories_.count(kind) > 0;
}

void CameraTransitionCatalog::freeze() {
    std::unique_lock<std::shared_mutex> lk(mu_);
    // Release-store publishes the freeze decision + all factories_[k]
    // writes that happened-before this point, so concurrent readers
    // that observe frozen_=true via acquire-load (in create()/has())
    // see the fully committed map.
    frozen_.store(true, std::memory_order_release);
}

void CameraTransitionCatalog::register_defaults() {
    if (!has(CameraTransitionKind::Cut))
        register_transition(CameraTransitionKind::Cut, ShotTimelineResolver::default_cut);
    if (!has(CameraTransitionKind::SmoothBlend))
        register_transition(CameraTransitionKind::SmoothBlend, ShotTimelineResolver::default_smooth_blend);
    if (!has(CameraTransitionKind::Push))
        register_transition(CameraTransitionKind::Push, ShotTimelineResolver::default_push);
    if (!has(CameraTransitionKind::WhipPan))
        register_transition(CameraTransitionKind::WhipPan, ShotTimelineResolver::default_whip_pan);
    if (!has(CameraTransitionKind::FocusHandoff))
        register_transition(CameraTransitionKind::FocusHandoff, ShotTimelineResolver::default_focus_handoff);
}

} // namespace chronon3d::camera_v1
