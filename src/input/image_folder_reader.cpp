/**
 * @file image_folder_reader.cpp
 * @brief Implementation of the image-folder input reader (Section 3b) - STUB.
 *
 * Only directory listing + sorted filename collection is implemented here; frame decoding is
 * a documented TODO (see include/input/image_folder_reader.h).
 */

#include "input/image_folder_reader.h"

#include <algorithm>
#include <filesystem>
#include <utility>

namespace fs = std::filesystem;

namespace cppxdic {
namespace input {

namespace {

/// Lowercase a copy of @p s (ASCII).
std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

/// True if @p lower_name (already lowercased) ends with a supported image extension.
bool has_image_extension(const std::string& lower_name) {
    static const char* kExts[] = {".png", ".jpg", ".jpeg", ".bmp", ".tif", ".tiff"};
    for (const char* ext : kExts) {
        const std::string e(ext);
        if (lower_name.size() > e.size() &&
            lower_name.compare(lower_name.size() - e.size(), e.size(), e) == 0) {
            return true;
        }
    }
    return false;
}

} // namespace

ImageFolderReader::ImageFolderReader(std::string folder) : folder_(std::move(folder)) {}

std::vector<std::string> ImageFolderReader::listFrames(bool skip_roi_ref) const {
    std::vector<std::string> frames;

    std::error_code ec;
    if (!fs::is_directory(folder_, ec)) {
        return frames; // Cannot open folder -> empty list.
    }

    for (const auto& entry : fs::directory_iterator(folder_, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;

        const std::string name = entry.path().filename().string();
        if (name.empty() || name[0] == '.') continue; // skip hidden files

        const std::string lower = to_lower(name);
        if (!has_image_extension(lower)) continue;

        if (skip_roi_ref && (lower == "roi.png" || lower == "ref.png")) continue;

        frames.push_back(entry.path().string());
    }

    // Lexicographic sort; zero-padded numeric filenames order correctly.
    std::sort(frames.begin(), frames.end());
    return frames;
}

} // namespace input
} // namespace cppxdic
