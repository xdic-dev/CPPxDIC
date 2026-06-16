/**
 * @file test_image_folder_reader.cpp
 * @brief Unit tests for ImageFolderReader::listFrames (include/input/image_folder_reader.h).
 *
 * Verifies: extension filtering, sorting, hidden-file skipping, roi/ref skipping toggle,
 * and graceful empty result for a non-existent directory. Uses a self-cleaning temp dir.
 */

#include "../framework/test_harness.h"
#include "input/image_folder_reader.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

namespace fs = std::filesystem;
using cppxdic::input::ImageFolderReader;

namespace {

/// Create a unique temp directory and populate it with the given filenames (empty content).
fs::path make_temp_with_files(const std::vector<std::string>& names) {
    fs::path dir = fs::temp_directory_path() /
                   ("ifr_test_" + std::to_string(::getpid()) + "_" +
                    std::to_string(reinterpret_cast<uintptr_t>(&names)));
    fs::create_directories(dir);
    for (const auto& n : names) {
        std::ofstream(dir / n) << "x";
    }
    return dir;
}

std::string basename_of(const std::string& p) {
    return fs::path(p).filename().string();
}

} // namespace

TEST(image_folder_reader, filters_and_sorts) {
    fs::path dir = make_temp_with_files({
        "ohtcfrp_02.png", "ohtcfrp_00.png", "ohtcfrp_01.png",
        "notes.txt", "data.csv", "img.JPG", "scan.tiff",
    });
    ImageFolderReader reader(dir.string());
    auto frames = reader.listFrames(/*skip_roi_ref=*/true);

    // Only the 5 image files (3 png + JPG + tiff), and sorted lexicographically.
    CHECK_EQ(frames.size(), static_cast<size_t>(5));
    if (frames.size() >= 3) {
        CHECK_EQ(basename_of(frames[0]), std::string("img.JPG")); // 'i' < 'o' < 's'
        CHECK_EQ(basename_of(frames[1]), std::string("ohtcfrp_00.png"));
        CHECK_EQ(basename_of(frames[2]), std::string("ohtcfrp_01.png"));
    }
    fs::remove_all(dir);
}

TEST(image_folder_reader, skips_hidden_files) {
    fs::path dir = make_temp_with_files({".hidden.png", "visible.png"});
    ImageFolderReader reader(dir.string());
    auto frames = reader.listFrames();
    CHECK_EQ(frames.size(), static_cast<size_t>(1));
    if (!frames.empty()) CHECK_EQ(basename_of(frames[0]), std::string("visible.png"));
    fs::remove_all(dir);
}

TEST(image_folder_reader, roi_ref_skip_toggle) {
    fs::path dir = make_temp_with_files({"roi.png", "ref.png", "frame_00.png"});
    ImageFolderReader reader(dir.string());

    auto skipped = reader.listFrames(/*skip_roi_ref=*/true);
    CHECK_EQ(skipped.size(), static_cast<size_t>(1));

    auto kept = reader.listFrames(/*skip_roi_ref=*/false);
    CHECK_EQ(kept.size(), static_cast<size_t>(3));

    fs::remove_all(dir);
}

TEST(image_folder_reader, nonexistent_dir_returns_empty) {
    ImageFolderReader reader("/no/such/dir/exists/cppxdic_12345");
    auto frames = reader.listFrames();
    CHECK_TRUE(frames.empty());
}

TEST(image_folder_reader, folder_accessor) {
    ImageFolderReader reader("/tmp/whatever");
    CHECK_EQ(reader.folder(), std::string("/tmp/whatever"));
}

TEST_MAIN()
