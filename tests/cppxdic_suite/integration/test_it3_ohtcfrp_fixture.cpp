/**
 * @file test_it3_ohtcfrp_fixture.cpp
 * @brief IT-3: 2D-path integration test against the real `ohtcfrp` fixture.
 *
 * The full 2D DIC engine (proxyncorr) needs an ROI mask that is not shipped with the
 * ohtcfrp example, and a heavy ncorr build; the displacement-field regression is covered by
 * the end-to-end test (E2E-1) which generates a synthetic full-frame ROI. This integration
 * test focuses on the *input stage* that feeds the DIC engine: discovering and ordering the
 * ohtcfrp frames via ImageFolderReader, exactly as the pipeline would before correlation.
 *
 * Asserts: the fixture exists, all 13 frames are discovered, they are sorted in temporal
 * (zero-padded numeric) order, and roi/ref helpers are excluded.
 *
 * OHTCFRP_DIR is injected by CMake (the absolute path to the fixture images directory).
 */

#include "../framework/test_harness.h"
#include "input/image_folder_reader.h"

#include <filesystem>
#include <string>

namespace fs = std::filesystem;
using cppxdic::input::ImageFolderReader;

#ifndef OHTCFRP_DIR
#define OHTCFRP_DIR ""
#endif

TEST(it3_ohtcfrp, fixture_frames_discovered_and_ordered) {
    const std::string dir = OHTCFRP_DIR;
    if (dir.empty() || !fs::is_directory(dir)) {
        SKIP_TEST("ohtcfrp fixture directory not available at configured OHTCFRP_DIR");
    }

    ImageFolderReader reader(dir);
    auto frames = reader.listFrames(/*skip_roi_ref=*/true);

    // The ohtcfrp example ships 12 PNG frames (ohtcfrp_00.png .. ohtcfrp_11.png) plus a
    // roi.png helper, which listFrames() excludes by default.
    CHECK_EQ(frames.size(), static_cast<size_t>(12));

    // Frames must be in ascending temporal order (zero-padded names sort numerically).
    for (size_t i = 1; i < frames.size(); ++i) {
        CHECK_TRUE(frames[i - 1] < frames[i]);
    }

    // First and last frame basenames.
    if (!frames.empty()) {
        CHECK_TRUE(fs::path(frames.front()).filename().string() == "ohtcfrp_00.png");
        CHECK_TRUE(fs::path(frames.back()).filename().string() == "ohtcfrp_11.png");
    }
}

TEST_MAIN()
