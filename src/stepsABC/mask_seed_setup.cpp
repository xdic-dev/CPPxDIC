/**
 * Implementation of MASK + Seed setup for xDIC stepsABC.
 *
 * GUI drawing code is guarded by XDIC_STEPSABC_GUI so the translation unit
 * compiles and links on a headless cluster node where no highgui window backend
 * is available. When GUI support is disabled, runGui() returns a clean error.
 */

#include "stepsABC/mask_seed_setup.h"

#include "logging.h"

#include <cctype>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>

namespace cppxdic {
namespace stepsABC {

namespace {

// Write a "# header" + list of "x y" rows.
bool writePointFile(const std::string& path, const std::string& header,
                    const std::vector<cv::Point>& pts, std::string& error_message) {
    std::ofstream ofs(path);
    if (!ofs) {
        error_message = "Cannot open for writing: " + path;
        return false;
    }
    ofs << header << "\n";
    for (const auto& p : pts) {
        ofs << p.x << " " << p.y << "\n";
    }
    return true;
}

} // namespace

bool MaskSeedSetup::guiAvailable() {
#ifdef XDIC_STEPSABC_GUI
    return true;
#else
    return false;
#endif
}

cv::Mat MaskSeedSetup::polygonToMask(const std::vector<cv::Point>& polygon,
                                     const cv::Size& image_size) {
    cv::Mat mask = cv::Mat::zeros(image_size, CV_8UC1);
    if (polygon.size() >= 3) {
        std::vector<std::vector<cv::Point>> polys{polygon};
        cv::fillPoly(mask, polys, cv::Scalar(255));
    }
    return mask;
}

std::vector<cv::Point> MaskSeedSetup::readPolygonFile(const std::string& path,
                                                      std::string& error_message) {
    std::vector<cv::Point> pts;
    std::ifstream ifs(path);
    if (!ifs) {
        error_message = "Cannot open polygon file: " + path;
        return pts;
    }
    std::string line;
    while (std::getline(ifs, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream iss(line);
        double x, y;
        if (iss >> x >> y) {
            pts.emplace_back(static_cast<int>(std::lround(x)), static_cast<int>(std::lround(y)));
        }
    }
    return pts;
}

std::vector<SeedPoint> MaskSeedSetup::readSeedFile(const std::string& path,
                                                   std::string& error_message) {
    std::vector<SeedPoint> seeds;
    std::ifstream ifs(path);
    if (!ifs) {
        error_message = "Cannot open seed file: " + path;
        return seeds;
    }
    std::string line;
    while (std::getline(ifs, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream iss(line);
        double x, y;
        if (iss >> x >> y) {
            seeds.emplace_back(static_cast<int>(std::lround(x)), static_cast<int>(std::lround(y)));
        }
    }
    return seeds;
}

MaskSeedResult MaskSeedSetup::loadFromFiles(const std::string& polygon_or_mask_path,
                                            const std::string& seed_path,
                                            const cv::Size& image_size,
                                            std::string& error_message) {
    MaskSeedResult result;

    // Decide whether the mask input is a polygon text file or a raster image by
    // extension. ".poly"/".txt" -> polygon, otherwise try to imread as a mask.
    std::string lower = polygon_or_mask_path;
    for (auto& c : lower) c = static_cast<char>(std::tolower(c));

    bool as_polygon = (lower.size() >= 5 && lower.substr(lower.size() - 5) == ".poly") ||
                      (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".txt");

    if (as_polygon) {
        result.polygon = readPolygonFile(polygon_or_mask_path, error_message);
        if (result.polygon.size() < 3) {
            if (error_message.empty())
                error_message = "Polygon file has fewer than 3 vertices: " + polygon_or_mask_path;
            return result;
        }
        result.mask = polygonToMask(result.polygon, image_size);
    } else {
        result.mask = cv::imread(polygon_or_mask_path, cv::IMREAD_GRAYSCALE);
        if (result.mask.empty()) {
            error_message = "Cannot read mask image: " + polygon_or_mask_path;
            return result;
        }
        // Normalise to strict 0/255.
        cv::threshold(result.mask, result.mask, 0, 255, cv::THRESH_BINARY);
    }

    result.seeds = readSeedFile(seed_path, error_message);
    if (result.seeds.empty()) {
        if (error_message.empty()) error_message = "No seed points found in: " + seed_path;
        return result;
    }

    result.valid = !result.mask.empty() && !result.seeds.empty();
    return result;
}

bool MaskSeedSetup::save(const MaskSeedResult& result, const std::string& out_prefix,
                         std::string& error_message) {
    if (!result.valid) {
        error_message = "Refusing to save an invalid mask/seed result";
        return false;
    }

    if (!writePointFile(out_prefix + ".poly", "# xdic mask polygon v1", result.polygon,
                        error_message)) {
        return false;
    }

    std::vector<cv::Point> seed_pts;
    seed_pts.reserve(result.seeds.size());
    for (const auto& s : result.seeds) {
        seed_pts.emplace_back(s.pw[0], s.pw[1]);
    }
    if (!writePointFile(out_prefix + ".seed", "# xdic seed points v1", seed_pts, error_message)) {
        return false;
    }

    if (!result.mask.empty()) {
        if (!cv::imwrite(out_prefix + "_mask.png", result.mask)) {
            error_message = "Failed to write mask image: " + out_prefix + "_mask.png";
            return false;
        }
    }
    return true;
}

// ----------------------------------------------------------------------------
// GUI path (compiled only when XDIC_STEPSABC_GUI is defined)
// ----------------------------------------------------------------------------

#ifdef XDIC_STEPSABC_GUI

namespace {

struct GuiState {
    std::vector<cv::Point> polygon; // mask vertices being drawn
    std::vector<cv::Point> seeds;   // seed points being picked
    bool collecting_polygon = true; // phase: true=polygon, false=seeds
    cv::Point cursor{-1, -1};
};

void renderOverlay(const cv::Mat& base, const GuiState& st, cv::Mat& out) {
    if (base.channels() == 1) {
        cv::cvtColor(base, out, cv::COLOR_GRAY2BGR);
    } else {
        base.copyTo(out);
    }

    // Draw polygon edges.
    for (size_t i = 0; i + 1 < st.polygon.size(); ++i) {
        cv::line(out, st.polygon[i], st.polygon[i + 1], cv::Scalar(0, 255, 0), 1);
    }
    if (st.collecting_polygon && !st.polygon.empty() && st.cursor.x >= 0) {
        cv::line(out, st.polygon.back(), st.cursor, cv::Scalar(0, 200, 0), 1);
    }
    if (!st.collecting_polygon && st.polygon.size() >= 3) {
        cv::line(out, st.polygon.back(), st.polygon.front(), cv::Scalar(0, 255, 0), 1);
    }
    for (const auto& p : st.polygon) {
        cv::circle(out, p, 3, cv::Scalar(0, 255, 255), -1);
    }
    // Draw seeds.
    for (const auto& s : st.seeds) {
        cv::drawMarker(out, s, cv::Scalar(0, 0, 255), cv::MARKER_CROSS, 14, 2);
    }
}

void onMouse(int event, int x, int y, int /*flags*/, void* userdata) {
    auto* st = static_cast<GuiState*>(userdata);
    st->cursor = cv::Point(x, y);
    if (event == cv::EVENT_LBUTTONDOWN) {
        if (st->collecting_polygon) {
            st->polygon.emplace_back(x, y);
        } else {
            st->seeds.emplace_back(x, y);
        }
    } else if (event == cv::EVENT_RBUTTONDOWN) {
        if (st->collecting_polygon && st->polygon.size() >= 3) {
            st->collecting_polygon = false; // close polygon, move to seed phase
        }
    }
}

} // namespace

MaskSeedResult MaskSeedSetup::runGui(const cv::Mat& reference_image, int num_seeds,
                                     std::string& error_message) {
    MaskSeedResult result;
    if (reference_image.empty()) {
        error_message = "GUI setup: reference image is empty";
        return result;
    }
    if (num_seeds < 1) num_seeds = 1;

    const std::string win = "xdic_stepsABC: MASK (L-click add, R-click close) then Seed";
    GuiState st;
    cv::namedWindow(win, cv::WINDOW_AUTOSIZE);
    cv::setMouseCallback(win, onMouse, &st);

    cv::Mat display;
    LOG_INFO << "[gui] Draw mask polygon: left-click vertices, right-click to close. "
             << "Then click " << num_seeds
             << " seed point(s). Keys: u=undo, r=reset, ENTER=accept, ESC=cancel.";

    while (true) {
        renderOverlay(reference_image, st, display);
        cv::imshow(win, display);
        int key = cv::waitKey(20) & 0xFF;

        if (key == 27) { // ESC -> cancel
            error_message = "GUI setup cancelled by user";
            cv::destroyWindow(win);
            return result;
        } else if (key == 'u') { // undo last point of current phase
            if (st.collecting_polygon && !st.polygon.empty())
                st.polygon.pop_back();
            else if (!st.collecting_polygon && !st.seeds.empty())
                st.seeds.pop_back();
        } else if (key == 'r') { // reset everything
            st = GuiState();
        } else if (key == 13 || key == 10) { // ENTER
            if (st.collecting_polygon) {
                if (st.polygon.size() >= 3) st.collecting_polygon = false; // close polygon
            } else if (static_cast<int>(st.seeds.size()) >= num_seeds) {
                break; // done
            }
        }
    }

    cv::destroyWindow(win);

    result.polygon = st.polygon;
    result.mask = polygonToMask(st.polygon, reference_image.size());
    result.seeds.clear();
    for (int i = 0; i < num_seeds && i < static_cast<int>(st.seeds.size()); ++i) {
        result.seeds.emplace_back(st.seeds[i].x, st.seeds[i].y);
    }
    result.valid = (result.polygon.size() >= 3) && !result.seeds.empty();
    if (!result.valid)
        error_message = "GUI setup incomplete (need polygon >=3 vertices and >=1 seed)";
    return result;
}

#else // !XDIC_STEPSABC_GUI

MaskSeedResult MaskSeedSetup::runGui(const cv::Mat& /*reference_image*/, int /*num_seeds*/,
                                     std::string& error_message) {
    MaskSeedResult result;
    error_message =
        "GUI mode is not available: this binary was built with XDIC_STEPSABC_GUI=OFF "
        "(headless). Re-run with --no-gui and provide --mask/--seed files, or rebuild "
        "with -DXDIC_STEPSABC_GUI=ON on a machine with a display.";
    return result;
}

#endif // XDIC_STEPSABC_GUI

} // namespace stepsABC
} // namespace cppxdic
