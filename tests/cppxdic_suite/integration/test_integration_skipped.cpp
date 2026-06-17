/**
 * @file test_integration_skipped.cpp
 * @brief Stereo-dependent integration tests (IT-1, IT-2, IT-4..IT-7) — registered SKIPPED.
 *
 * These pipeline-stage tests from TESTS_PLAN.md require a lightweight *stereo* fixture
 * (calibration .mat files + a 2-camera video/frame pair) that is not present in the repo.
 * The ohtcfrp example only exercises the 2D path. Rather than delete them from the plan,
 * each is represented here as a SKIPPED placeholder so the suite is green and the coverage
 * gap is explicit and self-documenting.
 *
 * To activate any of these: drop a stereo fixture under tests/test_data/, replace the SKIP_TEST()
 * with the assertions described in the doc comment, and wire the fixture path via CMake.
 */

#include "../framework/test_harness.h"

TEST(it1_calibration_load, skipped_needs_stereo_fixture) {
    // Input: a DLT calibration .mat. Expect DLTCalibrationData with 11 DLT params,
    // correct camera index, non-empty C3Dtrue.
    SKIP_TEST("IT-1 needs a sample DLT calibration .mat (no stereo fixture in repo)");
}

TEST(it2_video_ingestion, skipped_needs_stereo_fixture) {
    // Input: a short 2-camera .mp4 pair. Expect per-camera PNGs, frame count
    // = (end-start)/jump + 1, single-channel (Red) output.
    SKIP_TEST("IT-2 needs a sample 2-camera video pair (no stereo fixture in repo)");
}

TEST(it4_step_d_workflow, skipped_needs_stereo_fixture) {
    // Input: protocol + ROI/seed for one trial/pair. Expect a matching-results file path,
    // a non-empty trial index list, success flag true.
    SKIP_TEST("IT-4 needs protocol + ROI/seed stereo fixture (not in repo)");
}

TEST(it5_dic_3d_reconstruction, skipped_needs_stereo_fixture) {
    // Input: 2D DIC pair results + calibration. Expect DIC3DpairResults with consistent
    // point/face counts and finite 3D coordinates.
    SKIP_TEST("IT-5 needs 2D pair results + calibration stereo fixture (not in repo)");
}

TEST(it6_deformation_analysis, skipped_needs_stereo_fixture) {
    // Input: combined 3D surface across frames. Expect a DeformationResult with orthonormal
    // director triads per point and finite strain values.
    SKIP_TEST("IT-6 needs a combined 3D surface stereo fixture (not in repo)");
}

TEST(it7_stitch_two_pairs_geometric, skipped_needs_stereo_fixture) {
    // Input: two overlapping DIC3DpairResults. Expect a single DIC3Dcombined with the seam
    // handled (gapLogic), valid face indices, total points ~ sum minus overlap.
    // NOTE: the *append-based* stitch (stitchPairsSimple) is covered by the unit suite;
    // only the geometric overlap-removal path needs realistic overlapping surfaces.
    SKIP_TEST("IT-7 geometric stitch needs realistic overlapping stereo surfaces (not in repo)");
}

TEST_MAIN()
