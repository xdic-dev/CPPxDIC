/**
 * @file mirrored_mode.h
 * @brief Interface for the mirrored-camera xDIC mode ("MNG" variant).
 *
 * The mirrored mode reconstructs surface kinematics from a single physical camera that
 * observes the specimen together with one or more mirrors, yielding multiple virtual
 * viewpoints inside a single video frame. In the reference MATLAB project this corresponds
 * to the MNG-prefixed steps:
 *   - Tools/MultiDIC/main_script/stepD_2DDIC_MNG.m
 *   - Tools/MultiDIC/lib_script/import_vid_MNG.m / import_raw_vid_MNG.m
 *   - Tools/MultiDIC/lib_script/get_cam_view_stereopair_param.m / camera_info_from_view.m
 *
 * Unlike the camerapairs mode (two physical cameras, one video each), the mirrored mode
 * uses a binary *mask* over the image width to slice each physical-camera frame into
 * per-mirror full-frame "views". With cam_order = [2 1 4 3] and a left/right half mask
 * there are 4 physical cameras x 2 halves = 8 logical views, paired into 7 overlapping
 * stereopairs (see get_cam_view_stereopair_param). Standard 2D DIC then runs on each
 * extracted view exactly as in the camerapairs path.
 *
 * Status: implemented. Compiled into `cppxdic` only when configured with
 * `-DXDIC_MODE=mirrored` (which defines XDIC_MODE_MIRRORED).
 */

#pragma once

#include <opencv2/opencv.hpp>
#include <string>
#include <vector>

class Config;

namespace xdic {
namespace mirrored {

/**
 * @brief Half-width mask region used to extract a single mirror view from a frame.
 *
 * Mirrors the MATLAB im_mask_width cell array in camera_info_from_view.m:
 *   half 1 -> columns [0, W/2)     (MATLAB 1:W/2)
 *   half 2 -> columns [W/2, W)     (MATLAB W/2+1:W)
 */
enum class MaskHalf { Left = 1, Right = 2 };

/**
 * @brief Logical view = (physical camera id, which half of that camera's frame).
 *
 * Equivalent to the (cam_nbr, im_mask_width) pair returned by camera_info_from_view.m.
 */
struct ViewInfo {
    int view_nbr = 0; ///< 1..8 logical view index (MATLAB view_nbr)
    int cam_nbr = 0;  ///< physical camera id (from cam_order), used to find the video file
    MaskHalf half = MaskHalf::Left;
};

/**
 * @brief A mirrored stereopair: the two logical views to correlate against each other.
 *
 * Equivalent to the stereo_param struct from get_cam_view_stereopair_param.m.
 */
struct ViewPair {
    int stereopair = 0; ///< 1-based stereopair index
    ViewInfo view1;     ///< first view of the pair
    ViewInfo view2;     ///< second view of the pair
};

/**
 * @brief Resolve the two views that form a given mirrored stereopair.
 *
 * Direct port of get_cam_view_stereopair_param.m + camera_info_from_view.m.
 *
 * @param stereopair 1-based stereopair number (1..7 for the default 4-camera rig).
 * @param cam_order  Physical-camera ordering (MATLAB theGlobalSettings_MNG.cam_order,
 *                   default {2,1,4,3}). 1-based camera ids.
 * @return The resolved ViewPair.
 */
ViewPair resolveViewPair(int stereopair, const std::vector<int>& cam_order);

/**
 * @brief Extract a single mirror view (full frame) from a decoded camera frame.
 *
 * Applies the half-width mask (Left/Right) and returns the cropped region as a stand-alone
 * full-frame image, matching MATLAB `temp(:, stereo_param.mask_nbr_X, :)`.
 *
 * @param frame  Decoded single-channel (or BGR) source frame.
 * @param half   Which half to keep.
 * @return The extracted view as a single-channel cv::Mat.
 */
cv::Mat extractViewFromFrame(const cv::Mat& frame, MaskHalf half);

/**
 * @brief Decode a physical-camera video and write per-view masked frames to disk.
 *
 * This is the missing C++ counterpart of import_raw_vid_MNG.m: it reads ONE camera video,
 * slices each requested frame with the half mask, and writes the extracted view as a PNG
 * sequence (the same on-disk representation the CppNCorr DIC engine consumes).
 *
 * @param config       Resolved configuration (paths, subject, material, frames).
 * @param trial        Trial number (integer).
 * @param view         Which logical view to extract.
 * @param frameStart   1-based first frame (MATLAB convention).
 * @param frameEnd     1-based last frame (<=0 means "to end").
 * @param frameJump    Frame stride (<=0 treated as 1).
 * @param out_frames   Output: paths of the written per-view PNG frames, in order.
 * @return true if at least one frame was extracted.
 */
bool importRawViewMirrored(const Config& config, int trial, const ViewInfo& view, int frameStart,
                           int frameEnd, int frameJump, std::vector<std::string>& out_frames);

/**
 * @brief Run the mirrored-camera 2D DIC pipeline.
 *
 * For each stereopair: resolve its two views, extract them as masked full frames, then run
 * standard 2D DIC (matching view1->view2 at the reference frame plus temporal tracking of
 * each view), reusing the CppNCorr engine and ROIManager. Results are written under the
 * usual per-trial/per-pair output directory.
 *
 * @param config Fully-resolved configuration.
 * @return true on success.
 */
bool run(const Config& config);

} // namespace mirrored
} // namespace xdic
