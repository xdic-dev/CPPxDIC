/**
 * MASK + Seed setup for the xDIC stepsABC tool
 *
 * Provides interactive (OpenCV highgui) and non-interactive (file/config based)
 * setup of the reference MASK (polygon ROI) and Seed point(s) used to initialise
 * DIC tracking.
 *
 * This mirrors the MATLAB MultiDIC interactive helpers draw_ref_roi.m and
 * draw_ref_seed.m (polygon ROI via roipoly + seed point picking) but exposes a
 * headless path so the exact same binary runs on a cluster node with no display.
 *
 * The mask/seed *data structures* and any persistence to/from the cppxdic MAT
 * format are delegated to cppxdic::ROIManager so this tool does not duplicate
 * ROI/seed logic.
 */

#ifndef STEPSABC_MASK_SEED_SETUP_H
#define STEPSABC_MASK_SEED_SETUP_H

#include "parameters.h"
#include <opencv2/opencv.hpp>
#include <string>
#include <vector>

namespace cppxdic {
namespace stepsABC {

/**
 * Result of a mask/seed setup operation.
 */
struct MaskSeedResult {
    cv::Mat mask;                          // uint8 ROI mask (0 outside, 255 inside)
    std::vector<cv::Point> polygon;        // polygon vertices defining the mask (pixel coords)
    std::vector<SeedPoint> seeds;          // one or more seed points (pixel world coords)
    bool valid = false;                    // true when mask + at least one seed are set
};

/**
 * MaskSeedSetup
 *
 * Static helpers to create (GUI), load (non-GUI) and persist mask + seed data.
 *
 * File formats (documented in README_xdic_stepsABC.md):
 *   - Mask polygon : plain-text ".poly" file, one "x y" pair per line (pixel coords),
 *                    header line "# xdic mask polygon vN".
 *   - Mask image   : 8-bit single channel PNG (0 / 255). Optional; regenerated from
 *                    the polygon when only the polygon file is present.
 *   - Seed         : plain-text ".seed" file, one "x y" pair per line, header line
 *                    "# xdic seed points vN".
 */
class MaskSeedSetup {
public:
    /**
     * Interactive GUI setup using OpenCV highgui.
     *
     * Opens a window on the reference image, lets the user draw a polygon MASK
     * (left-click to add vertices, right-click / ENTER to close the polygon) and
     * then pick one or more seed points. Only compiled when XDIC_STEPSABC_GUI is
     * defined; otherwise returns an invalid result with an error message.
     *
     * @param reference_image  Reference image to draw on (grayscale or BGR).
     * @param num_seeds        Number of seed points to collect (>=1).
     * @param error_message    Filled with a human readable message on failure.
     * @return MaskSeedResult (valid==false on cancel / unavailable GUI).
     */
    static MaskSeedResult runGui(const cv::Mat& reference_image,
                                 int num_seeds,
                                 std::string& error_message);

    /**
     * Non-interactive setup: load mask polygon (or mask image) and seed points
     * from files. No window is created. This is the cluster path.
     *
     * @param polygon_or_mask_path  Path to a ".poly" polygon file OR an 8-bit mask image.
     * @param seed_path             Path to a ".seed" file.
     * @param image_size            Reference image size (used to rasterise the polygon).
     * @param error_message         Filled with a human readable message on failure.
     * @return MaskSeedResult (valid==false on error).
     */
    static MaskSeedResult loadFromFiles(const std::string& polygon_or_mask_path,
                                        const std::string& seed_path,
                                        const cv::Size& image_size,
                                        std::string& error_message);

    /**
     * Persist a result to disk. Writes <out_prefix>.poly, <out_prefix>_mask.png
     * and <out_prefix>.seed. Returns false (and sets error_message) on I/O error.
     */
    static bool save(const MaskSeedResult& result,
                     const std::string& out_prefix,
                     std::string& error_message);

    /** Rasterise a polygon into an 8-bit (0/255) mask of the given size. */
    static cv::Mat polygonToMask(const std::vector<cv::Point>& polygon,
                                 const cv::Size& image_size);

    /** Read a ".poly" polygon file. Returns empty vector on error. */
    static std::vector<cv::Point> readPolygonFile(const std::string& path,
                                                  std::string& error_message);

    /** Read a ".seed" file. Returns empty vector on error. */
    static std::vector<SeedPoint> readSeedFile(const std::string& path,
                                               std::string& error_message);

    /** Returns true if this binary was compiled with GUI support. */
    static bool guiAvailable();
};

} // namespace stepsABC
} // namespace cppxdic

#endif // STEPSABC_MASK_SEED_SETUP_H
