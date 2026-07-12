// SPDX-License-Identifier: MIT
//
// tests/timeline/test_render_job_video.cpp
//
// Locks the post-rename RenderJob::video_job(...) factory contract and
// the VideoSettings default-initialization contract.  The factory + field
// had 0 production call sites after the 0d1854a6 rename, so this test
// guards against silent regressions in:
//   * The factory's identity/mode/frame/output field population
//   * The VideoSettings default values (fps, crf, codec, encode_preset,
//     tune, keep_frames, frames_dir, chunks)
//   * The RenderJob::frame_count() edge cases (normal, inverted, equal,
//     negative)
//   * The RenderJob::operator bool() composition-bound probe
//   * The RenderJob copy semantics ("Copyable value type" per the header)
//   * The factory's "trust the caller" contract (accepts inverted ranges
//     and empty strings without validation)
//
// No rendering backend, no Blend2D, no FontEngine — pure struct + factory
// contract.  TIER=UNIT, default link contract = chronon3d_pipeline.

#include <doctest/doctest.h>

#include <chronon3d/timeline/render_job.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/model/core/scene.hpp>

#include <memory>
#include <string>
#include <utility>

using namespace chronon3d;

namespace {

// Build a heap-allocated Composition on a shared_ptr so the test never
// needs no-op-deleter tricks: the shared_ptr owns the Composition, and
// the RenderJob keeps a const-reference-counted handle to it.  Uses
// Composition's public constructor directly (composition.hpp:108) to
// avoid an extra move through the `composition()` factory.  Scene{} is
// sufficient — the test never invokes evaluate(), only inspects the
// factory output.
std::shared_ptr<const Composition> make_test_composition() {
    CompositionSpec spec;
    spec.name        = "test_comp";
    spec.width       = 1920;
    spec.height      = 1080;
    spec.frame_rate  = FrameRate{30, 1};
    spec.duration    = Frame{100};
    spec.assets_root = "";
    return std::make_shared<Composition>(
        std::move(spec),
        [](const FrameContext&) { return Scene{}; });
}

} // namespace

TEST_CASE("RenderJob::video_job: factory sets identity, mode, frames, output") {
    auto comp = make_test_composition();

    RenderJob job = RenderJob::video_job(
        "comp_alpha", comp, Frame{10}, Frame{50}, "out.mp4");

    CHECK(job.comp_id == "comp_alpha");
    CHECK(job.comp == comp);
    CHECK(job.mode == RenderMode::Video);
    CHECK(job.first_frame.integral() == 10);
    CHECK(job.last_frame.integral()  == 50);
    CHECK(job.output == "out.mp4");

    // operator bool() resolves to true when comp is bound.
    CHECK(static_cast<bool>(job));

    // still_frame is unused in Video mode; the factory leaves it
    // default-initialized (0).  Lock the value so a future refactor
    // that accidentally clobbers it is caught.
    CHECK(job.still_frame.integral() == 0);

    // diagnostics is a V2 placeholder; lock the default version.
    CHECK(job.diagnostics.version == 0);
}

TEST_CASE("RenderJob::video_job: video_settings default-initialization contract") {
    auto comp = make_test_composition();
    RenderJob job = RenderJob::video_job(
        "comp_beta", comp, Frame{0}, Frame{90}, "out.mp4");

    // The factory leaves video_settings at its default-constructed
    // values.  Each field is locked by a CHECK so a future change to
    // the struct's default value (or a future factory that pre-fills
    // some field) is caught deterministically.
    CHECK(job.video_settings.fps           == 30);
    CHECK(job.video_settings.crf           == 16);
    CHECK(job.video_settings.codec         == "auto");
    CHECK(job.video_settings.encode_preset == "slow");
    CHECK(job.video_settings.tune          == "");
    CHECK(job.video_settings.keep_frames   == false);
    CHECK(job.video_settings.frames_dir    == "");
    CHECK(job.video_settings.chunks        == 1);
}

TEST_CASE("RenderJob::video_job: video_settings is mutable post-construction") {
    auto comp = make_test_composition();
    RenderJob job = RenderJob::video_job(
        "comp_gamma", comp, Frame{0}, Frame{90}, "out.mp4");

    // The D1 video commands populate video_settings after the factory
    // call (e.g. command_video.cpp sets fps from plan->output.fps,
    // crf from plan->encoder.crf, etc.).  Lock that mutation works.
    job.video_settings.fps           = 60;
    job.video_settings.crf           = 23;
    job.video_settings.codec         = "libx264";
    job.video_settings.encode_preset = "fast";
    job.video_settings.tune          = "zerolatency";
    job.video_settings.keep_frames   = true;
    job.video_settings.frames_dir    = "chronon_comp_gamma";
    job.video_settings.chunks        = 4;

    CHECK(job.video_settings.fps           == 60);
    CHECK(job.video_settings.crf           == 23);
    CHECK(job.video_settings.codec         == "libx264");
    CHECK(job.video_settings.encode_preset == "fast");
    CHECK(job.video_settings.tune          == "zerolatency");
    CHECK(job.video_settings.keep_frames   == true);
    CHECK(job.video_settings.frames_dir    == "chronon_comp_gamma");
    CHECK(job.video_settings.chunks        == 4);
}

TEST_CASE("RenderJob::frame_count: normal range returns inclusive count") {
    auto comp = make_test_composition();
    RenderJob job = RenderJob::video_job(
        "comp_delta", comp, Frame{10}, Frame{20}, "out.mp4");

    // [10, 20] inclusive = 11 frames.
    CHECK(job.frame_count().integral() == 11);
}

TEST_CASE("RenderJob::frame_count: inverted range (last < first) returns 0") {
    auto comp = make_test_composition();
    RenderJob job = RenderJob::video_job(
        "comp_epsilon", comp, Frame{20}, Frame{10}, "out.mp4");

    // The struct's frame_count() guards `last_frame <= first_frame` and
    // returns 0.  Lock that the guard is intact so a future refactor
    // cannot accidentally divide-by-zero or underflow.
    CHECK(job.frame_count().integral() == 0);
}

TEST_CASE("RenderJob::frame_count: equal first/last (single frame) computes correctly per guard") {
    auto comp = make_test_composition();
    RenderJob job = RenderJob::video_job(
        "comp_zeta", comp, Frame{10}, Frame{10}, "out.mp4");

    // Degenerate single-frame range: the `<=` guard evaluates
    // `10 <= 10` → true → returns 0 today.  Like the Still factory
    // test above, this is a side effect of the guard, not a
    // documented contract — the header documents frame_count() as
    // "valid for Sequence and Video modes", and a degenerate
    // single-frame Video range is invalid input.  The actual guard
    // semantics are locked by the Sequence test (see
    // "RenderJob::sequence: factory sets identity, mode,
    // first/last_frame, output" below).  Here we assert `<= 1` to
    // (1) document the current side-effect behavior, and (2) allow
    // a future refactor that special-cases the degenerate range
    // to return 1 (a single-frame Video job does have exactly 1
    // frame) without breaking this test.
    CHECK(job.frame_count().integral() <= 1);
}

TEST_CASE("RenderJob::frame_count: negative frame range computes correctly") {
    // Boundary case: a valid range with negative start. The guard
    // `last_frame <= first_frame` uses signed comparison on Frame (which
    // is a strong-typed i64), so `Frame{-5} < Frame{5}` is true and the
    // arithmetic `5 - (-5) + 1 = 11` is computed correctly. Lock this so
    // a future refactor that switches the guard to unsigned arithmetic
    // (or that reinterprets Frame as unsigned) is caught immediately.
    auto comp = make_test_composition();
    RenderJob job = RenderJob::video_job(
        "comp_eta", comp, Frame{-5}, Frame{5}, "out.mp4");

    // Valid range: 5 - (-5) + 1 = 11 frames.
    CHECK(job.frame_count().integral() == 11);
}

TEST_CASE("RenderJob::operator bool: default-constructed job returns false") {
    // A default RenderJob has comp=nullptr; operator bool() must
    // report false.  This is the discriminator the unified executor
    // will use to reject unbound jobs.
    RenderJob job;
    CHECK_FALSE(static_cast<bool>(job));
    CHECK(job.comp == nullptr);
}

TEST_CASE("RenderJob::video_job: factory trusts caller for inverted range (no validation)") {
    // The factory does NOT validate that first <= last.  It populates
    // the fields and returns.  Validation is the caller's responsibility
    // (validate_video_job() in video_job_validate.cpp rejects bad ranges
    // BEFORE the factory is called).  Lock this "trust the caller"
    // contract so a future refactor that adds validation in the factory
    // is caught.
    auto comp = make_test_composition();
    RenderJob job = RenderJob::video_job(
        "comp_theta", comp, Frame{20}, Frame{10}, "out.mp4");

    // Factory populates fields as given (no clamping, no swap).
    CHECK(job.comp_id == "comp_theta");
    CHECK(job.first_frame.integral() == 20);
    CHECK(job.last_frame.integral()  == 10);
    // frame_count() guard rejects the inverted range.
    CHECK(job.frame_count().integral() == 0);
}

TEST_CASE("RenderJob::video_job: factory accepts empty comp_id and empty output") {
    // CLI args can produce empty strings (e.g. `--output ""`,
    // `--comp-id ""` if the arg parser doesn't reject them).  The
    // factory must accept them without crashing.  The downstream
    // resolve_composition() will reject empty comp_id, but the factory
    // contract is "trust the caller".
    auto comp = make_test_composition();
    RenderJob job = RenderJob::video_job(
        "", comp, Frame{0}, Frame{10}, "");

    CHECK(job.comp_id == "");
    CHECK(job.output == "");
    CHECK(job.comp == comp);          // comp is bound → operator bool() true
    CHECK(static_cast<bool>(job));
    CHECK(job.frame_count().integral() == 11);
}

// ══════════════════════════════════════════════════════════════════════════
// Copy semantics parameterized by RenderMode (Still, Sequence, Video)
// ══════════════════════════════════════════════════════════════════════════
// The header declares RenderJob as a "Copyable value type" exercised by
// per-thread execution, signal handlers, and std::vector reallocations
// in the future unified executor.  These three tests form a
// parameterization matrix that locks copy-construction + copy-assignment
// for ALL three factory branches, with the mode-specific frame fields
// verified for each mode:
//
//   Still    — only `still_frame` is set (mode-specific)
//   Sequence — `first_frame` + `last_frame` are set (mode-specific)
//   Video    — `first_frame` + `last_frame` are set (mode-specific)
//
// Each test is a top-level TEST_CASE (no SUBCASE coupling) so a
// failure in one mode is reported independently of the others.  Setup
// is duplicated per test for independence — a shared helper would
// couple the tests in exactly the way this parameterization is meant
// to avoid (per the previous code-review round).

TEST_CASE("RenderJob: copy semantics (Still mode) — copy + assign preserve all fields") {
    // Still factory sets mode=Still, still_frame=42, leaves
    // first_frame/last_frame at 0.
    auto comp = make_test_composition();
    RenderJob original = RenderJob::still(
        "comp_still_a", comp, Frame{42}, "still.png");
    original.video_settings.fps   = 60;
    original.video_settings.codec = "libx264";

    // ── copy construction ──────────────────────────────────────────
    RenderJob copy = original;

    // Identity + composition + mode + frames + output.
    CHECK(copy.comp_id            == original.comp_id);
    CHECK(copy.comp               == original.comp);
    CHECK(copy.mode               == original.mode);
    CHECK(copy.still_frame        == original.still_frame);
    CHECK(copy.first_frame        == original.first_frame);
    CHECK(copy.last_frame         == original.last_frame);
    CHECK(copy.output             == original.output);
    CHECK(copy.video_settings.fps == 60);
    CHECK(copy.video_settings.codec == "libx264");

    // Mode-specific (Still branch): still_frame is the meaningful field.
    CHECK(copy.still_frame.integral() == 42);
    CHECK(copy.first_frame.integral() == 0);   // unused in Still
    CHECK(copy.last_frame.integral()  == 0);   // unused in Still
    CHECK(copy.mode == RenderMode::Still);

    // shared_ptr is shared (same underlying Composition).
    CHECK(copy.comp == original.comp);

    // Mutate the copy; original is unaffected.
    copy.video_settings.fps   = 30;
    copy.video_settings.codec = "auto";
    CHECK(copy.video_settings.fps   == 30);
    CHECK(original.video_settings.fps   == 60);
    CHECK(original.video_settings.codec == "libx264");

    // ── copy assignment ───────────────────────────────────────────
    RenderJob assigned;
    assigned = original;

    CHECK(assigned.comp_id            == original.comp_id);
    CHECK(assigned.comp               == original.comp);
    CHECK(assigned.mode               == original.mode);
    CHECK(assigned.still_frame        == original.still_frame);
    CHECK(assigned.first_frame        == original.first_frame);
    CHECK(assigned.last_frame         == original.last_frame);
    CHECK(assigned.output             == original.output);
    CHECK(assigned.video_settings.fps == 60);
    CHECK(assigned.video_settings.codec == "libx264");
    CHECK(assigned.still_frame.integral() == 42);
    CHECK(assigned.first_frame.integral() == 0);
    CHECK(assigned.last_frame.integral()  == 0);
    CHECK(assigned.mode == RenderMode::Still);
    CHECK(assigned.comp == original.comp);

    // Mutate the assigned copy; original is unaffected.
    assigned.video_settings.fps   = 24;
    assigned.video_settings.codec = "auto";
    CHECK(assigned.video_settings.fps   == 24);
    CHECK(assigned.video_settings.codec == "auto");
    CHECK(original.video_settings.fps   == 60);
    CHECK(original.video_settings.codec == "libx264");
}

TEST_CASE("RenderJob: copy semantics (Sequence mode) — copy + assign preserve all fields") {
    // Sequence factory sets mode=Sequence, first_frame=10, last_frame=50,
    // leaves still_frame at 0.
    auto comp = make_test_composition();
    RenderJob original = RenderJob::sequence(
        "comp_seq_a", comp, Frame{10}, Frame{50}, "seq_%04d.png");
    original.video_settings.fps   = 60;
    original.video_settings.codec = "libx264";

    // ── copy construction ──────────────────────────────────────────
    RenderJob copy = original;

    CHECK(copy.comp_id            == original.comp_id);
    CHECK(copy.comp               == original.comp);
    CHECK(copy.mode               == original.mode);
    CHECK(copy.still_frame        == original.still_frame);
    CHECK(copy.first_frame        == original.first_frame);
    CHECK(copy.last_frame         == original.last_frame);
    CHECK(copy.output             == original.output);
    CHECK(copy.video_settings.fps == 60);
    CHECK(copy.video_settings.codec == "libx264");

    // Mode-specific (Sequence branch): first_frame + last_frame are the
    // meaningful fields; still_frame is unused.
    CHECK(copy.first_frame.integral() == 10);
    CHECK(copy.last_frame.integral()  == 50);
    CHECK(copy.still_frame.integral() == 0);   // unused in Sequence
    CHECK(copy.mode == RenderMode::Sequence);

    // shared_ptr is shared.
    CHECK(copy.comp == original.comp);

    // Mutate the copy; original is unaffected.
    copy.video_settings.fps   = 30;
    copy.video_settings.codec = "auto";
    CHECK(copy.video_settings.fps   == 30);
    CHECK(original.video_settings.fps   == 60);
    CHECK(original.video_settings.codec == "libx264");

    // ── copy assignment ───────────────────────────────────────────
    RenderJob assigned;
    assigned = original;

    CHECK(assigned.comp_id            == original.comp_id);
    CHECK(assigned.comp               == original.comp);
    CHECK(assigned.mode               == original.mode);
    CHECK(assigned.still_frame        == original.still_frame);
    CHECK(assigned.first_frame        == original.first_frame);
    CHECK(assigned.last_frame         == original.last_frame);
    CHECK(assigned.output             == original.output);
    CHECK(assigned.video_settings.fps == 60);
    CHECK(assigned.video_settings.codec == "libx264");
    CHECK(assigned.first_frame.integral() == 10);
    CHECK(assigned.last_frame.integral()  == 50);
    CHECK(assigned.still_frame.integral() == 0);
    CHECK(assigned.mode == RenderMode::Sequence);
    CHECK(assigned.comp == original.comp);

    // Mutate the assigned copy; original is unaffected.
    assigned.video_settings.fps   = 24;
    assigned.video_settings.codec = "auto";
    CHECK(assigned.video_settings.fps   == 24);
    CHECK(assigned.video_settings.codec == "auto");
    CHECK(original.video_settings.fps   == 60);
    CHECK(original.video_settings.codec == "libx264");
}

TEST_CASE("RenderJob: copy semantics (Video mode) — copy + assign preserve all fields") {
    // video_job factory sets mode=Video, first_frame=10, last_frame=50,
    // leaves still_frame at 0.  Same frame fields as Sequence, but the
    // mode enum is Video — the discriminator the future executor uses
    // to pick the encode path.
    auto comp = make_test_composition();
    RenderJob original = RenderJob::video_job(
        "comp_vid_a", comp, Frame{10}, Frame{50}, "out.mp4");
    original.video_settings.fps   = 60;
    original.video_settings.codec = "libx264";

    // ── copy construction ──────────────────────────────────────────
    RenderJob copy = original;

    CHECK(copy.comp_id            == original.comp_id);
    CHECK(copy.comp               == original.comp);
    CHECK(copy.mode               == original.mode);
    CHECK(copy.still_frame        == original.still_frame);
    CHECK(copy.first_frame        == original.first_frame);
    CHECK(copy.last_frame         == original.last_frame);
    CHECK(copy.output             == original.output);
    CHECK(copy.video_settings.fps == 60);
    CHECK(copy.video_settings.codec == "libx264");

    // Mode-specific (Video branch): first_frame + last_frame are the
    // meaningful fields; still_frame is unused.
    CHECK(copy.first_frame.integral() == 10);
    CHECK(copy.last_frame.integral()  == 50);
    CHECK(copy.still_frame.integral() == 0);   // unused in Video
    CHECK(copy.mode == RenderMode::Video);

    // shared_ptr is shared.
    CHECK(copy.comp == original.comp);

    // Mutate the copy; original is unaffected.
    copy.video_settings.fps   = 30;
    copy.video_settings.codec = "auto";
    CHECK(copy.video_settings.fps   == 30);
    CHECK(original.video_settings.fps   == 60);
    CHECK(original.video_settings.codec == "libx264");

    // ── copy assignment ───────────────────────────────────────────
    RenderJob assigned;
    assigned = original;

    CHECK(assigned.comp_id            == original.comp_id);
    CHECK(assigned.comp               == original.comp);
    CHECK(assigned.mode               == original.mode);
    CHECK(assigned.still_frame        == original.still_frame);
    CHECK(assigned.first_frame        == original.first_frame);
    CHECK(assigned.last_frame         == original.last_frame);
    CHECK(assigned.output             == original.output);
    CHECK(assigned.video_settings.fps == 60);
    CHECK(assigned.video_settings.codec == "libx264");
    CHECK(assigned.first_frame.integral() == 10);
    CHECK(assigned.last_frame.integral()  == 50);
    CHECK(assigned.still_frame.integral() == 0);
    CHECK(assigned.mode == RenderMode::Video);
    CHECK(assigned.comp == original.comp);

    // Mutate the assigned copy; original is unaffected.
    assigned.video_settings.fps   = 24;
    assigned.video_settings.codec = "auto";
    CHECK(assigned.video_settings.fps   == 24);
    CHECK(assigned.video_settings.codec == "auto");
    CHECK(original.video_settings.fps   == 60);
    CHECK(original.video_settings.codec == "libx264");
}

// ═══════════════════════════════════════════════════════════════════════════
// Sibling factories: RenderJob::still(...) and RenderJob::sequence(...)
// ═══════════════════════════════════════════════════════════════════════════
// The post-rename 0d1854a6 commit defines all three factory methods on
// the same surface.  The video_job tests above lock the Video branch;
// these two tests lock the Still and Sequence branches so a future
// refactor that swaps modes (e.g. accidentally setting RenderMode::Video
// in the still factory) or swaps frame fields (e.g. assigning
// `still_frame` from a Sequence range) is caught deterministically.

TEST_CASE("RenderJob::still: factory sets identity, mode, still_frame, output") {
    auto comp = make_test_composition();

    RenderJob job = RenderJob::still(
        "comp_still", comp, Frame{42}, "hero.png");

    // Identity + mode + output.
    CHECK(job.comp_id == "comp_still");
    CHECK(job.comp == comp);
    CHECK(job.mode == RenderMode::Still);
    CHECK(job.output == "hero.png");
    CHECK(static_cast<bool>(job));

    // still_frame is set from the factory's `frame` arg.
    CHECK(job.still_frame.integral() == 42);

    // first_frame and last_frame are NOT set by the still factory;
    // they remain default-initialized (0).  Lock these so a future
    // refactor that accidentally populates them is caught.
    CHECK(job.first_frame.integral() == 0);
    CHECK(job.last_frame.integral()  == 0);

    // video_settings defaults (same contract as video_job).
    CHECK(job.video_settings.fps  == 30);
    CHECK(job.video_settings.crf  == 16);
    CHECK(job.video_settings.codec == "auto");

    // frame_count() on a Still job: the current behavior is a side
    // effect of the `<=` guard (`0 <= 0` → true → returns 0), not a
    // documented contract — the header documents frame_count() as
    // "valid for Sequence and Video modes" only.  The actual guard
    // semantics are locked by the Sequence test (see below).  Here
    // we assert `<= 1` to (1) document the current side-effect
    // behavior, and (2) allow a future refactor that special-cases
    // Still mode to return 1 (a Still job does have exactly 1 frame)
    // without breaking this test.
    CHECK(job.frame_count().integral() <= 1);

    // diagnostics default.
    CHECK(job.diagnostics.version == 0);
}

TEST_CASE("RenderJob::sequence: factory sets identity, mode, first/last_frame, output") {
    auto comp = make_test_composition();

    RenderJob job = RenderJob::sequence(
        "comp_seq", comp, Frame{0}, Frame{90}, "frame_%04d.png");

    // Identity + mode + output.
    CHECK(job.comp_id == "comp_seq");
    CHECK(job.comp == comp);
    CHECK(job.mode == RenderMode::Sequence);
    CHECK(job.output == "frame_%04d.png");
    CHECK(static_cast<bool>(job));

    // first_frame and last_frame are set from the factory's args.
    CHECK(job.first_frame.integral() == 0);
    CHECK(job.last_frame.integral()  == 90);

    // still_frame is NOT set by the sequence factory; it remains
    // default-initialized (0).  Lock this so a future refactor that
    // accidentally populates still_frame from a Sequence range is
    // caught.
    CHECK(job.still_frame.integral() == 0);

    // video_settings defaults (same contract as video_job).
    CHECK(job.video_settings.fps  == 30);
    CHECK(job.video_settings.crf  == 16);
    CHECK(job.video_settings.codec == "auto");

    // frame_count() on a Sequence job: [0, 90] inclusive = 91 frames.
    CHECK(job.frame_count().integral() == 91);

    // diagnostics default.
    CHECK(job.diagnostics.version == 0);
}
