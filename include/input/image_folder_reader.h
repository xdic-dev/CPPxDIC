/**
 * @file image_folder_reader.h
 * @brief Image-folder input reader (Section 3b) - STUB.
 *
 * Alternative input source to the video reader (src/utils.cpp Utils::importRawVid). Instead
 * of decoding .mp4 files, this reader ingests a directory of pre-extracted image frames
 * (PNG/JPG/BMP/TIFF).
 *
 * The interface mirrors the video reader's contract: given the input location, it produces a
 * sorted list of per-camera frame paths that the DIC engine can consume. Currently only the
 * directory listing + sorted filename collection is implemented; actual frame decoding into
 * pixel buffers is left as a TODO so it can be wired up alongside an in-memory frame hand-off.
 */

#pragma once

#include <string>
#include <vector>

namespace cppxdic {
namespace input {

/**
 * @brief Reads frames from a folder of image files.
 *
 * Stub status: directory listing + sorted, extension-filtered filename collection works;
 * pixel decoding is not yet implemented.
 */
class ImageFolderReader {
public:
    /**
     * @brief Construct a reader for a given image folder.
     * @param folder Absolute or relative path to the directory containing image frames.
     */
    explicit ImageFolderReader(std::string folder);

    /**
     * @brief List image frames in the folder, sorted by filename.
     *
     * Filters to common image extensions (.png .jpg .jpeg .bmp .tif .tiff, case-insensitive),
     * skips hidden files, and optionally skips `roi.png` / `ref.png` helper images. Results
     * are sorted lexicographically (zero-padded numeric filenames sort correctly).
     *
     * @param skip_roi_ref If true (default), excludes files named `roi.png` and `ref.png`.
     * @return Sorted list of full frame paths (folder + "/" + filename). Empty if the folder
     *         cannot be opened or contains no images.
     */
    std::vector<std::string> listFrames(bool skip_roi_ref = true) const;

    /**
     * @brief Decode the listed frames into pixel buffers.
     *
     * @return Empty vector for now.
     *
     * @par TODO - what "fully implemented" means
     *  - Decode each frame (cv::imread) into the same single-channel representation the video
     *    reader produces (Red channel for RGB inputs, to match MATLAB iloc(:,:,1)).
     *  - Return frames as in-memory buffers (e.g. std::vector<cv::Mat>) so the DIC pipeline can
     *    consume them without a disk round-trip, matching the planned in-memory hand-off in
     *    apps/proxyncorr.
     *  - Honour frame start/end/jump selection consistent with Config (idx_frame_start, etc.).
     */
    // std::vector<cv::Mat> decodeFrames() const;  // intentionally not declared yet (TODO)

    /// @return The folder this reader was constructed with.
    const std::string& folder() const { return folder_; }

private:
    std::string folder_; ///< Directory containing the image frames.
};

} // namespace input
} // namespace cppxdic
