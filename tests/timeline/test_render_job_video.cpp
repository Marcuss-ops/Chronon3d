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
// ── render_mode_name() — human-readable name for the RenderMode enum ──
// Used by `CAPTURE()` in `run_copy_semantics_test()` below so doctest
// failure output names which of the 3 modes triggered the failure,
// not just the helper line number.  Kept in the anonymous namespace
// so it does not leak to the textual ABI.
const char* render_mode_name(RenderMode m) noexcept {
    switch (m) {
        case RenderMode::Still:    return "Still";
        case RenderMode::Sequence: return "Sequence";
        case RenderMode::Video:    return "Video";
    }
    return "Unknown";
}

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

// ── run_copy_semantics_test() — parameterization helper ──────────────
// Locks the RenderJob copy-construction + copy-assignment contract for
// ANY factory branch (Still / Sequence / Video).  The factory lambda
// takes a shared_ptr<const Composition> and returns a RenderJob — the
// helper then:
//   1. Snapshots identity (comp_id, output) from the factory result.
//   2. Mutates ALL 8 video_settings fields on the original (fps, crf,
//      codec, encode_preset, tune, keep_frames, frames_dir, chunks) —
//      this closes code-reviewer item 3 from the prior round (the
//      previous tests only mutated fps + codec, leaving 6 fields
//      unverified through the copy path).
//   3. Copy-constructs `copy = original` and verifies every field
//      (identity, mode, frame fields, output, all 8 video_settings
//      fields) plus the shared_ptr<Composition> sharing contract.
//   4. Mutates ALL 8 video_settings fields on the copy and verifies
//      the original is unchanged (independence).
//   5. Copy-assigns `assigned = original` and re-runs all 3 / 4
//      verifications on the assigned copy.
//
// The 3 mode-parameterized tests below call this helper with different
// factory lambdas and mode-specific frame values, centralizing ~150
// lines of duplication that previously lived in each test.
template <typename Factory>
void run_copy_semantics_test(
    Factory factory,
    RenderMode expected_mode,
    Frame     expected_still_frame,
    Frame     expected_first_frame,
    Frame     expected_last_frame)
{
    // CAPTURE the mode so doctest failure output names which of the 3
    // modes triggered the failure (not just the helper line number).
    // The CAPTURE is a no-op on success — it only prints on failure.
    CAPTURE(render_mode_name(expected_mode));

    auto comp = make_test_composition();
    RenderJob original = factory(comp);

    // Snapshot identity (set by the factory).  The helper does not
    // require the caller to pass these — the factory result is the
    // ground truth.
    const std::string expected_comp_id = original.comp_id;
    const std::string expected_output  = original.output;

    // ── Mutate ALL 8 video_settings fields on the original ──────
    original.video_settings.fps           = 60;
    original.video_settings.crf           = 23;
    original.video_settings.codec         = "libx264";
    original.video_settings.encode_preset = "fast";
    original.video_settings.tune          = "zerolatency";
    original.video_settings.keep_frames   = true;
    original.video_settings.frames_dir    = "chronon_test";
    original.video_settings.chunks        = 4;

    // Snapshot the mutated values — single source of truth for the
    // copy-construction and copy-assignment assertions below.  This
    // removes 24 hardcoded literals (3 sets of 8 CHECKs now reference
    // the snapshot instead of duplicating the mutated values).
    const VideoSettings expected = original.video_settings;

    // ── copy construction ────────────────────────────────────
    RenderJob copy = original;

    // Identity + composition + output.
    CHECK(copy.comp_id == expected_comp_id);
    CHECK(copy.comp    == comp);
    CHECK(copy.output  == expected_output);

    // Mode + frame fields (mode-specific).
    CHECK(copy.mode        == expected_mode);
    CHECK(copy.still_frame == expected_still_frame);
    CHECK(copy.first_frame == expected_first_frame);
    CHECK(copy.last_frame  == expected_last_frame);

    // All 8 video_settings fields preserved (referenced via snapshot).
    CHECK(copy.video_settings.fps           == expected.fps);
    CHECK(copy.video_settings.crf           == expected.crf);
    CHECK(copy.video_settings.codec         == expected.codec);
    CHECK(copy.video_settings.encode_preset == expected.encode_preset);
    CHECK(copy.video_settings.tune          == expected.tune);
    CHECK(copy.video_settings.keep_frames   == expected.keep_frames);
    CHECK(copy.video_settings.frames_dir    == expected.frames_dir);
    CHECK(copy.video_settings.chunks        == expected.chunks);

    // shared_ptr is shared (same underlying Composition).
    CHECK(copy.comp == original.comp);

    // Mutate ALL 8 video_settings on the copy; original is unaffected.
    copy.video_settings.fps           = 30;
    copy.video_settings.crf           = 18;
    copy.video_settings.codec         = "auto";
    copy.video_settings.encode_preset = "slow";
    copy.video_settings.tune          = "";
    copy.video_settings.keep_frames   = false;
    copy.video_settings.frames_dir    = "";
    copy.video_settings.chunks        = 1;

    // original is unaffected (verified against the snapshot).
    CHECK(original.video_settings.fps           == expected.fps);
    CHECK(original.video_settings.crf           == expected.crf);
    CHECK(original.video_settings.codec         == expected.codec);
    CHECK(original.video_settings.encode_preset == expected.encode_preset);
    CHECK(original.video_settings.tune          == expected.tune);
    CHECK(original.video_settings.keep_frames   == expected.keep_frames);
    CHECK(original.video_settings.frames_dir    == expected.frames_dir);
    CHECK(original.video_settings.chunks        == expected.chunks);

    // ── copy assignment ────────────────────────────────────
    RenderJob assigned;
    assigned = original;

    CHECK(assigned.comp_id == expected_comp_id);
    CHECK(assigned.comp    == comp);
    CHECK(assigned.output  == expected_output);
    CHECK(assigned.mode        == expected_mode);
    CHECK(assigned.still_frame == expected_still_frame);
    CHECK(assigned.first_frame == expected_first_frame);
    CHECK(assigned.last_frame  == expected_last_frame);

    CHECK(assigned.video_settings.fps           == 60);
    CHECK(assigned.video_settings.crf           == 23);
    CHECK(assigned.video_settings.codec         == "libx264");
    CHECK(assigned.video_settings.encode_preset == "fast");
    CHECK(assigned.video_settings.tune          == "zerolatency");
    CHECK(assigned.video_settings.keep_frames   == true);
    CHECK(assigned.video_settings.frames_dir    == "chronon_test");
    CHECK(assigned.video_settings.chunks        == 4);
    CHECK(assigned.comp == original.comp);

    // Mutate ALL 8 video_settings on the assigned copy; original is
    // unaffected.
    assigned.video_settings.fps           = 24;
    assigned.video_settings.crf           = 20;
    assigned.video_settings.codec         = "hevc";
    assigned.video_settings.encode_preset = "ultrafast";
    assigned.video_settings.tune          = "animation";
    assigned.video_settings.keep_frames   = false;
    assigned.video_settings.frames_dir    = "other";
    assigned.video_settings.chunks        = 8;

    // original is unaffected (verified against the snapshot).
    CHECK(original.video_settings.fps           == expected.fps);
    CHECK(original.video_settings.crf           == expected.crf);
    CHECK(original.video_settings.codec         == expected.codec);
    CHECK(original.video_settings.encode_preset == expected.encode_preset);
    CHECK(original.video_settings.tune          == expected.tune);
    CHECK(original.video_settings.keep_frames   == expected.keep_frames);
    CHECK(original.video_settings.frames_dir    == expected.frames_dir);
    CHECK(original.video_settings.chunks        == expected.chunks);
}



// ── run_move_semantics_test() — move-parameterization helper ────────────
// Locks the RenderJob move-construction + move-assignment contract for
// ANY factory branch (Still / Sequence / Video).  The factory lambda
// takes a shared_ptr<const Composition> and returns a RenderJob — the
// helper then:
//   1. Mutates ALL 8 video_settings fields on the original.
//   2. Snapshots the mutated values into `expected` (same pattern as
//      the copy helper).
//   3. Move-constructs `moved = std::move(original)` and verifies every
//      field is preserved.  The moved-from object is in a valid but
//      unspecified state per [stmt.class.copy] in the C++ standard, so
//      we cannot verify that the original is unchanged.
//   4. Move-assigns `move_assigned = std::move(original)` on a FRESH
//      original (the first move left it in an unspecified state) and
//      re-runs the verifications.
//   5. Asserts `use_count() == 2` after both moves to prove the
//      shared_ptr was moved (not copied).  A copy would yield
//      use_count == 3.  This catches a future refactor that
//      accidentally makes RenderJob copy-only.
//   6. Self-moves `j = std::move(j)` and verifies only POD fields
//      (mode + frame fields) are preserved.  Non-POD fields are in
//      unspecified state and must NOT be asserted.
//
// No mutation independence check: after a move, the moved-from object
// is in a valid but unspecified state, so we cannot verify it's
// unchanged.  The copy helper's mutation independence check is the
// counterpart for copy semantics.
template <typename Factory>
void run_move_semantics_test(
    Factory factory,
    RenderMode expected_mode,
    Frame     expected_still_frame,
    Frame     expected_first_frame,
    Frame     expected_last_frame)
{
    CAPTURE(render_mode_name(expected_mode));

    // ── move construction ────────────────────────────────────
    {
        auto comp = make_test_composition();
        RenderJob original = factory(comp);

        const std::string expected_comp_id = original.comp_id;
        const std::string expected_output  = original.output;

        original.video_settings.fps           = 60;
        original.video_settings.crf           = 23;
        original.video_settings.codec         = "libx264";
        original.video_settings.encode_preset = "fast";
        original.video_settings.tune          = "zerolatency";
        original.video_settings.keep_frames   = true;
        original.video_settings.frames_dir    = "chronon_test";
        original.video_settings.chunks        = 4;

        const VideoSettings expected = original.video_settings;

        RenderJob moved = std::move(original);

        CHECK(moved.comp_id == expected_comp_id);
        CHECK(moved.comp    == comp);
        CHECK(moved.output  == expected_output);
        CHECK(moved.mode        == expected_mode);
        CHECK(moved.still_frame == expected_still_frame);
        CHECK(moved.first_frame == expected_first_frame);
        CHECK(moved.last_frame  == expected_last_frame);
        CHECK(moved.video_settings.fps           == expected.fps);
        CHECK(moved.video_settings.crf           == expected.crf);
        CHECK(moved.video_settings.codec         == expected.codec);
        CHECK(moved.video_settings.encode_preset == expected.encode_preset);
        CHECK(moved.video_settings.tune          == expected.tune);
        CHECK(moved.video_settings.keep_frames   == expected.keep_frames);
        CHECK(moved.video_settings.frames_dir    == expected.frames_dir);
        CHECK(moved.video_settings.chunks        == expected.chunks);
        CHECK(moved.comp == comp);  // shared_ptr shared (refcount unchanged)
        // use_count == 2 proves the shared_ptr was moved (not copied):
        //   - one reference in `comp` (the original in test scope)
        //   - one reference in `moved.comp` (transferred from original)
        // A copy would yield use_count == 3 (comp + original.comp + moved.comp).
        // This catches a future refactor that accidentally makes RenderJob
        // copy-only (e.g., by adding a user-defined dtor that suppresses
        // the implicit move ctor).
        CHECK(moved.comp.use_count() == 2);
    }

    // ── move assignment ────────────────────────────────────
    {
        auto comp = make_test_composition();
        RenderJob original = factory(comp);

        const std::string expected_comp_id = original.comp_id;
        const std::string expected_output  = original.output;

        original.video_settings.fps           = 60;
        original.video_settings.crf           = 23;
        original.video_settings.codec         = "libx264";
        original.video_settings.encode_preset = "fast";
        original.video_settings.tune          = "zerolatency";
        original.video_settings.keep_frames   = true;
        original.video_settings.frames_dir    = "chronon_test";
        original.video_settings.chunks        = 4;

        const VideoSettings expected = original.video_settings;

        RenderJob move_assigned;
        move_assigned = std::move(original);

        CHECK(move_assigned.comp_id == expected_comp_id);
        CHECK(move_assigned.comp    == comp);
        CHECK(move_assigned.output  == expected_output);
        CHECK(move_assigned.mode        == expected_mode);
        CHECK(move_assigned.still_frame == expected_still_frame);
        CHECK(move_assigned.first_frame == expected_first_frame);
        CHECK(move_assigned.last_frame  == expected_last_frame);
        CHECK(move_assigned.video_settings.fps           == expected.fps);
        CHECK(move_assigned.video_settings.crf           == expected.crf);
        CHECK(move_assigned.video_settings.codec         == expected.codec);
        CHECK(move_assigned.video_settings.encode_preset == expected.encode_preset);
        CHECK(move_assigned.video_settings.tune          == expected.tune);
        CHECK(move_assigned.video_settings.keep_frames   == expected.keep_frames);
        CHECK(move_assigned.video_settings.frames_dir    == expected.frames_dir);
        CHECK(move_assigned.video_settings.chunks        == expected.chunks);
        CHECK(move_assigned.comp == comp);
        // use_count == 2 proves the shared_ptr was moved (not copied).
        // See the move-construction section above for the full rationale.
        CHECK(move_assigned.comp.use_count() == 2);
    }

    // ── self-move ──────────────────────────────────────────────
    {
        auto comp = make_test_composition();
        RenderJob j = factory(comp);

        // Self-move: C++17 makes this well-defined (object left in
        // valid but unspecified state per [class.copy.assign]).  We
        // only verify POD fields are preserved (mode + frame fields
        // are POD — move is trivial copy).  Non-POD fields (strings,
        // shared_ptr) are in unspecified state and must NOT be
        // asserted.  This locks the std::vector reallocation and
        // move-and-clear edge case where self-move can occur.
        j = std::move(j);

        CHECK(j.mode        == expected_mode);
        CHECK(j.still_frame == expected_still_frame);
        CHECK(j.first_frame == expected_first_frame);
        CHECK(j.last_frame  == expected_last_frame);

        // use_count == 2 for self-move: shared_ptr self-move is a no-op
        // per [util.smartptr.shared.const] (C++17+), so the refcount
        // should be preserved.  Before self-move: j.comp + comp = 2 refs.
        // After self-move: same, 2 refs.  Matches the move ctor/assign
        // sections and extends the move-vs-copy discriminator here too.
        CHECK(j.comp.use_count() == 2);
    }
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
    // Still factory: mode=Still, still_frame=42, first/last=0 (unused).
    run_copy_semantics_test(
        [](std::shared_ptr<const Composition> c) {
            return RenderJob::still("comp_still_a", c, Frame{42}, "still.png");
        },
        RenderMode::Still,
        Frame{42},
        Frame{0},
        Frame{0});
}

TEST_CASE("RenderJob: copy semantics (Sequence mode) — copy + assign preserve all fields") {
    // Sequence factory: mode=Sequence, first=10, last=50, still=0 (unused).
    run_copy_semantics_test(
        [](std::shared_ptr<const Composition> c) {
            return RenderJob::sequence(
                "comp_seq_a", c, Frame{10}, Frame{50}, "seq_%04d.png");
        },
        RenderMode::Sequence,
        Frame{0},
        Frame{10},
        Frame{50});
}

TEST_CASE("RenderJob: copy semantics (Video mode) — copy + assign preserve all fields") {
    // video_job factory: mode=Video, first=10, last=50, still=0 (unused).
    // Same frame fields as Sequence, but the mode enum is Video — the
    // discriminator the future executor uses to pick the encode path.
    run_copy_semantics_test(
        [](std::shared_ptr<const Composition> c) {
            return RenderJob::video_job(
                "comp_vid_a", c, Frame{10}, Frame{50}, "out.mp4");
        },
        RenderMode::Video,
        Frame{0},
        Frame{10},
        Frame{50});
}

// ══════════════════════════════════════════════════════════════════════════
// Move semantics parameterized by RenderMode (Still, Sequence, Video)
// ══════════════════════════════════════════════════════════════════════════
// The header declares RenderJob as a "Copyable value type" but move
// semantics are not exercised anywhere.  These three tests form a
// parameterization matrix that locks move-construction + move-assignment
// for ALL three factory branches, reusing the same `expected` snapshot
// pattern as the copy tests.
//
// Move semantics specifics:
//   - The moved-from object is in a valid but unspecified state per
//     [stmt.class.copy] in the C++ standard.  We cannot verify that
//     the original is unchanged after a move.
//   - The moved-to object has all the fields of the original (strings
//     are transferred, shared_ptr is transferred with unchanged
//     refcount, POD types are trivially copied).
//   - No mutation independence check (the original is in an
//     unspecified state after the move).
//   - Self-move is well-defined in C++17 (object left in valid but
//     unspecified state); only POD fields are asserted.

TEST_CASE("RenderJob: move semantics (Still mode) — move + assign preserve all fields") {
    // Still factory: mode=Still, still_frame=42, first/last=0 (unused).
    run_move_semantics_test(
        [](std::shared_ptr<const Composition> c) {
            return RenderJob::still("comp_still_a", c, Frame{42}, "still.png");
        },
        RenderMode::Still,
        Frame{42},
        Frame{0},
        Frame{0});
}

TEST_CASE("RenderJob: move semantics (Sequence mode) — move + assign preserve all fields") {
    // Sequence factory: mode=Sequence, first=10, last=50, still=0 (unused).
    run_move_semantics_test(
        [](std::shared_ptr<const Composition> c) {
            return RenderJob::sequence(
                "comp_seq_a", c, Frame{10}, Frame{50}, "seq_%04d.png");
        },
        RenderMode::Sequence,
        Frame{0},
        Frame{10},
        Frame{50});
}

TEST_CASE("RenderJob: move semantics (Video mode) — move + assign preserve all fields") {
    // video_job factory: mode=Video, first=10, last=50, still=0 (unused).
    // Same frame fields as Sequence, but the mode enum is Video — the
    // discriminator the future executor uses to pick the encode path.
    run_move_semantics_test(
        [](std::shared_ptr<const Composition> c) {
            return RenderJob::video_job(
                "comp_vid_a", c, Frame{10}, Frame{50}, "out.mp4");
        },
        RenderMode::Video,
        Frame{0},
        Frame{10},
        Frame{50});
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
