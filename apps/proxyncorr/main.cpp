/**
 * @file main.cpp
 * @brief proxyncorr - thin pass-through driver around the vendored CppNCorr 2D DIC engine.
 *
 * proxyncorr lets you run CppNCorr's DIC + strain pipeline directly from CPPXDIC for
 * quick validation, without going through the full xDIC stereo/3D-reconstruction stack.
 * It discovers image frames in a folder, loads an ROI, runs DIC analysis (sequential or
 * parallel, with optional user seeds), converts to the Eulerian perspective, runs strain
 * analysis, and serialises the results (binary .bin, JSON, and overlay videos).
 *
 * This source is promoted from `Tools/CppNCorr/test/src/proxyncorr.cpp` (the best of the
 * in-tree experimental drivers) into a first-class, optional CPPXDIC target. The submodule
 * copy is left untouched; this is the maintained version going forward.
 *
 * Build: configure with `-DBUILD_PROXYNCORR=ON` (default OFF). The target is NOT built as
 * part of the default `cppxdic` build.
 *
 * @par In-memory hand-off (IMPLEMENTED)
 *  The `--in-memory` flag selects an in-memory pass-through path that does NOT round-trip
 *  frames through the file-based `Image2D`/`DIC_analysis` pipeline. Instead it loads each
 *  frame with OpenCV into an owning `cv::Mat`, wraps it as an `ncorr::ImageBuffer` (a thin,
 *  non-owning view over the raw pixel bytes), and drives `ncorr::NcorrSession`
 *  (`Tools/CppNCorr/include/ncorr/session.h`): `set_reference()` once, optional `set_roi()`,
 *  then `process_frame()` per deformed frame. Each call returns a `ncorr::DICResult` holding
 *  the native Lagrangian displacement fields (u/v in pixels, plus per-point corrcoef) on the
 *  reduced analysis grid, NaN outside the ROI. The fields are consumed directly as plain
 *  `std::vector<double>` and dumped per-frame as lightweight JSON (no `.bin`/video/strain
 *  serialisation in this path). This removes the disk round-trip when proxyncorr is used as
 *  a library-style call. The default (no flag) behaviour is unchanged: the full file-based
 *  DIC + strain + video pipeline below.
 *
 * @par TODO - remaining follow-ups
 *  - Expose a callable C++ API (e.g.
 *    `proxyncorr::run(const ProxyConfig&, const std::vector<Image2D>&) -> ProxyResult`)
 *    so xDIC modes can invoke the DIC engine programmatically. The `--in-memory` path here
 *    is the building block: it already drives the engine from in-memory buffers.
 *  - Map CPPXDIC's `ncorr_params.txt` / Config fields onto ProxyConfig so a single config
 *    source drives both the full pipeline and the proxy.
 *  - Replace the `system("mkdir -p ...")` calls with std::filesystem::create_directories.
 */

#include "ncorr.h"
#include "ncorr/session.h"
#include "logging.h"
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <fstream>
#include <sstream>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <sys/stat.h>
#include <dirent.h>
#include <algorithm>
#include <getopt.h>
#include <matio.h>
#include <opencv2/core.hpp>

using namespace ncorr;
using json = nlohmann::json;

// ============================================================================
// Configuration structure holding all parameters
// ============================================================================
struct ProxyConfig {
    // Paths
    std::string folder = "images";
    std::string roi_path = ""; // Empty means use folder/roi.png
    std::string ref_path = ""; // Empty means use first frame
    std::string output_dir = "output";

    // DIC parameters
    int scalefactor = 3;
    std::string interp_type = "QUINTIC_BSPLINE_PRECOMPUTE";
    std::string subregion_type = "CIRCLE";
    int subregion_radius = 20;
    int num_threads = 4;
    std::string dic_config = "NO_UPDATE";
    bool debug = false;

    // Perspective change
    std::string perspective_interp = "CUBIC_KEYS";

    // Units
    std::string units = "mm";
    double units_per_pixel = 0.2;

    // Strain parameters
    std::string strain_subregion_type = "CIRCLE";
    int strain_radius = 5;

    // Algorithm mode: "auto", "sequential", "parallel"
    std::string algorithm_mode = "auto";

    // Seeds configuration (one per region)
    std::vector<SeedParams> seeds_by_region;
    std::string seeds_file = ""; // Path to seeds JSON file
    bool seeds_are_optimized = false;

    // Video parameters
    double alpha = 0.5;
    double fps = 15.0;

    // Flags
    bool save_json = true;
    bool save_binary = true;
    bool save_videos = true;

    // In-memory pass-through path: load frames with OpenCV and drive
    // ncorr::NcorrSession instead of the file-based DIC/strain/video pipeline.
    bool in_memory = false;
};

// ============================================================================
// Helper functions for enum conversion
// ============================================================================
INTERP parse_interp(const std::string& s) {
    if (s == "NEAREST") return INTERP::NEAREST;
    if (s == "LINEAR") return INTERP::LINEAR;
    if (s == "CUBIC_KEYS") return INTERP::CUBIC_KEYS;
    if (s == "CUBIC_KEYS_PRECOMPUTE") return INTERP::CUBIC_KEYS_PRECOMPUTE;
    if (s == "QUINTIC_BSPLINE") return INTERP::QUINTIC_BSPLINE;
    if (s == "QUINTIC_BSPLINE_PRECOMPUTE") return INTERP::QUINTIC_BSPLINE_PRECOMPUTE;
    throw std::runtime_error("Unknown interpolation type: " + s);
}

SUBREGION parse_subregion(const std::string& s) {
    if (s == "CIRCLE") return SUBREGION::CIRCLE;
    if (s == "SQUARE") return SUBREGION::SQUARE;
    throw std::runtime_error("Unknown subregion type: " + s);
}

DIC_analysis_config parse_dic_config(const std::string& s) {
    if (s == "NO_UPDATE") return DIC_analysis_config::NO_UPDATE;
    if (s == "KEEP_MOST_POINTS") return DIC_analysis_config::KEEP_MOST_POINTS;
    if (s == "REMOVE_BAD_POINTS") return DIC_analysis_config::REMOVE_BAD_POINTS;
    throw std::runtime_error("Unknown DIC config: " + s);
}

// ============================================================================
// Helper functions for JSON serialization (from ncorr_test_cubic.cpp)
// ============================================================================
json array2d_to_json(const Array2D<double>& array) {
    json j;
    j["rows"] = array.height();
    j["cols"] = array.width();
    std::vector<double> data;
    for (int i = 0; i < array.height(); ++i) {
        for (int jj = 0; jj < array.width(); ++jj) {
            data.push_back(array(i, jj));
        }
    }
    j["data"] = data;
    return j;
}

json array2d_to_json(const Array2D<bool>& array) {
    json j;
    j["rows"] = array.height();
    j["cols"] = array.width();
    std::vector<bool> data;
    for (int i = 0; i < array.height(); ++i) {
        for (int jj = 0; jj < array.width(); ++jj) {
            data.push_back(array(i, jj));
        }
    }
    j["data"] = data;
    return j;
}

json roi2d_to_json(const ROI2D& roi) {
    json j;
    j["num_regions"] = roi.size_regions();
    Array2D<bool> mask = roi.get_mask();
    j["mask"] = array2d_to_json(mask);
    return j;
}

json disp2d_to_json(const Disp2D& disp) {
    json j;
    j["v"] = array2d_to_json(disp.get_v().get_array());
    j["u"] = array2d_to_json(disp.get_u().get_array());
    j["roi"] = roi2d_to_json(disp.get_roi());
    j["scalefactor"] = disp.get_scalefactor();
    return j;
}

json strain2d_to_json(const Strain2D& strain) {
    json j;
    j["eyy"] = array2d_to_json(strain.get_eyy().get_array());
    j["exy"] = array2d_to_json(strain.get_exy().get_array());
    j["exx"] = array2d_to_json(strain.get_exx().get_array());
    j["roi"] = roi2d_to_json(strain.get_roi());
    j["scalefactor"] = strain.get_scalefactor();
    return j;
}

json dic_input_to_json(const DIC_analysis_input& dic_input) {
    json j;
    j["roi"] = roi2d_to_json(dic_input.roi);
    j["scalefactor"] = dic_input.scalefactor;
    j["interp_type"] = static_cast<int>(dic_input.interp_type);
    j["subregion_type"] = static_cast<int>(dic_input.subregion_type);
    j["radius"] = dic_input.r;
    j["num_threads"] = dic_input.num_threads;
    j["cutoff_corrcoef"] = dic_input.cutoff_corrcoef;
    j["update_corrcoef"] = dic_input.update_corrcoef;
    j["prctile_corrcoef"] = dic_input.prctile_corrcoef;
    j["debug"] = dic_input.debug;
    std::vector<std::string> img_paths;
    for (const auto& img : dic_input.imgs) {
        img_paths.push_back(img.get_filename());
    }
    j["img_paths"] = img_paths;
    return j;
}

json dic_output_to_json(const DIC_analysis_output& dic_output) {
    json j;
    json disps_json = json::array();
    for (const auto& disp : dic_output.disps) {
        disps_json.push_back(disp2d_to_json(disp));
    }
    j["disps"] = disps_json;
    j["perspective_type"] = static_cast<int>(dic_output.perspective_type);
    j["units"] = dic_output.units;
    j["units_per_pixel"] = dic_output.units_per_pixel;
    return j;
}

json strain_input_to_json(const strain_analysis_input& strain_input) {
    json j;
    j["dic_input"] = dic_input_to_json(strain_input.DIC_input);
    j["dic_output"] = dic_output_to_json(strain_input.DIC_output);
    j["subregion_type"] = static_cast<int>(strain_input.subregion_type);
    j["radius"] = strain_input.r;
    return j;
}

json strain_output_to_json(const strain_analysis_output& strain_output) {
    json j;
    json strains_json = json::array();
    for (const auto& strain : strain_output.strains) {
        strains_json.push_back(strain2d_to_json(strain));
    }
    j["strains"] = strains_json;
    return j;
}

void save_as_json(const DIC_analysis_input& dic_input, const DIC_analysis_output& dic_output,
                  const strain_analysis_input& strain_input,
                  const strain_analysis_output& strain_output, const std::string& directory) {
    json dic_input_json = dic_input_to_json(dic_input);
    json dic_output_json = dic_output_to_json(dic_output);
    json strain_input_json = strain_input_to_json(strain_input);
    json strain_output_json = strain_output_to_json(strain_output);

    system(("mkdir -p " + directory).c_str());

    std::ofstream dic_input_file(directory + "/DIC_input.json");
    dic_input_file << std::setw(4) << dic_input_json << std::endl;

    std::ofstream dic_output_file(directory + "/DIC_output.json");
    dic_output_file << std::setw(4) << dic_output_json << std::endl;

    std::ofstream strain_input_file(directory + "/strain_input.json");
    strain_input_file << std::setw(4) << strain_input_json << std::endl;

    std::ofstream strain_output_file(directory + "/strain_output.json");
    strain_output_file << std::setw(4) << strain_output_json << std::endl;
}

// ============================================================================
// File discovery functions
// ============================================================================
std::vector<std::string> discover_frames(const std::string& folder, const std::string& ref_path,
                                         const std::string& roi_path) {
    std::vector<std::string> frames;
    DIR* dir = opendir(folder.c_str());
    if (!dir) {
        throw std::runtime_error("Cannot open folder: " + folder);
    }

    // Get basenames to exclude
    std::string roi_basename = "";
    std::string ref_basename = "";
    if (!roi_path.empty()) {
        size_t pos = roi_path.find_last_of("/\\");
        roi_basename = (pos != std::string::npos) ? roi_path.substr(pos + 1) : roi_path;
    }
    if (!ref_path.empty()) {
        size_t pos = ref_path.find_last_of("/\\");
        ref_basename = (pos != std::string::npos) ? ref_path.substr(pos + 1) : ref_path;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name = entry->d_name;

        // Skip hidden files and directories
        if (name[0] == '.') continue;

        // Check for image extensions
        std::string lower_name = name;
        std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);

        bool is_image =
            (lower_name.length() > 4 && (lower_name.substr(lower_name.length() - 4) == ".png" ||
                                         lower_name.substr(lower_name.length() - 4) == ".jpg" ||
                                         lower_name.substr(lower_name.length() - 4) == ".bmp" ||
                                         lower_name.substr(lower_name.length() - 5) == ".jpeg" ||
                                         lower_name.substr(lower_name.length() - 5) == ".tiff" ||
                                         lower_name.substr(lower_name.length() - 4) == ".tif"));

        if (!is_image) continue;

        // Skip roi.png by default
        if (lower_name == "roi.png") continue;

        // Skip ref.png by default
        if (lower_name == "ref.png") continue;

        // Skip explicitly specified roi and ref files
        if (!roi_basename.empty() && name == roi_basename) continue;
        if (!ref_basename.empty() && name == ref_basename) continue;

        frames.push_back(folder + "/" + name);
    }
    closedir(dir);

    // Sort frames naturally (handles numbered files)
    std::sort(frames.begin(), frames.end());

    return frames;
}

bool file_exists(const std::string& path) {
    struct stat buffer;
    return (stat(path.c_str(), &buffer) == 0);
}

// Lowercase file extension including the dot, e.g. "/a/B.MAT" -> ".mat".
std::string file_ext_lower(const std::string& path) {
    size_t slash = path.find_last_of("/\\");
    size_t dot = path.find_last_of('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return "";
    std::string ext = path.substr(dot);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext;
}

// ============================================================================
// MATLAB .mat ROI / seed loading (xDIC-compatible)
// ============================================================================
//
// xDIC stores the reference ROI mask and seed as .mat sidecars next to each
// trial (REF_MASK_*.mat / REF_SEED_*.mat). To let proxyncorr consume those same
// artefacts, we read them here with matio, mirroring cppxdic::ROIManager::
// loadROIFromMat / loadSeedFromMat (kept self-contained so proxyncorr stays a
// thin, lib-free driver).

// Load an ROI mask from a .mat file holding a 2D uint8/logical variable named
// "refmask". Returns a CV_8UC1 image (0 / 255), row-major, matching the PNG ROI
// path. Throws on any failure so the caller can report a clear error.
cv::Mat load_roi_mask_from_mat(const std::string& mat_path) {
    mat_t* matfp = Mat_Open(mat_path.c_str(), MAT_ACC_RDONLY);
    if (!matfp) throw std::runtime_error("Cannot open ROI .mat file: " + mat_path);

    matvar_t* var = Mat_VarRead(matfp, "refmask");
    if (!var) {
        Mat_Close(matfp);
        throw std::runtime_error("Variable 'refmask' not found in " + mat_path);
    }
    if (var->rank != 2 || !var->data) {
        Mat_VarFree(var);
        Mat_Close(matfp);
        throw std::runtime_error("'refmask' must be a 2D array in " + mat_path);
    }

    const size_t height = var->dims[0];
    const size_t width = var->dims[1];
    const bool is_logical = (var->isLogical != 0);
    cv::Mat mask(static_cast<int>(height), static_cast<int>(width), CV_8UC1);

    // MATLAB is column-major: element (y, x) lives at index x*height + y.
    // Support the common storage classes for a mask variable.
    auto fill = [&](auto* data) {
        for (size_t y = 0; y < height; ++y) {
            for (size_t x = 0; x < width; ++x) {
                double v = static_cast<double>(data[x * height + y]);
                mask.at<uint8_t>(static_cast<int>(y), static_cast<int>(x)) =
                    (is_logical || v <= 1.0) ? (v > 0.0 ? 255 : 0)
                                             : static_cast<uint8_t>(std::min(255.0, v));
            }
        }
    };
    switch (var->class_type) {
        case MAT_C_UINT8:  fill(static_cast<const uint8_t*>(var->data));  break;
        case MAT_C_INT8:   fill(static_cast<const int8_t*>(var->data));   break;
        case MAT_C_DOUBLE: fill(static_cast<const double*>(var->data));   break;
        case MAT_C_SINGLE: fill(static_cast<const float*>(var->data));    break;
        case MAT_C_UINT16: fill(static_cast<const uint16_t*>(var->data)); break;
        default:
            Mat_VarFree(var);
            Mat_Close(matfp);
            throw std::runtime_error("Unsupported 'refmask' class in " + mat_path);
    }

    Mat_VarFree(var);
    Mat_Close(matfp);
    return mask;
}

// Build an ROI2D from either an image file (PNG/BMP/...) or an xDIC .mat mask,
// chosen by file extension. Mirrors the existing PNG path's >0.5 threshold.
ROI2D build_roi(const std::string& roi_path) {
    if (file_ext_lower(roi_path) == ".mat") {
        return ROI2D(Image2D(load_roi_mask_from_mat(roi_path)).get_gs() > 0.5);
    }
    return ROI2D(Image2D(roi_path).get_gs() > 0.5);
}

// Load a seed from a .mat file holding a 2-element variable named "seed_point"
// ([x, y] in pixels), matching xDIC's REF_SEED_*.mat. Returns a single-region
// seed vector (deformation params zeroed), consistent with load_seeds_from_json.
std::vector<SeedParams> load_seeds_from_mat(const std::string& mat_path) {
    mat_t* matfp = Mat_Open(mat_path.c_str(), MAT_ACC_RDONLY);
    if (!matfp) throw std::runtime_error("Cannot open seed .mat file: " + mat_path);

    matvar_t* var = Mat_VarRead(matfp, "seed_point");
    if (!var) {
        Mat_Close(matfp);
        throw std::runtime_error("Variable 'seed_point' not found in " + mat_path);
    }

    size_t n = (var->rank >= 1 && var->data) ? 1 : 0;
    for (int i = 0; i < var->rank; ++i) n *= var->dims[i];
    if (n < 2 || !var->data) {
        Mat_VarFree(var);
        Mat_Close(matfp);
        throw std::runtime_error("'seed_point' must hold at least [x, y] in " + mat_path);
    }

    auto at = [&](size_t i) -> double {
        switch (var->class_type) {
            case MAT_C_DOUBLE: return static_cast<const double*>(var->data)[i];
            case MAT_C_SINGLE: return static_cast<const float*>(var->data)[i];
            case MAT_C_UINT16: return static_cast<const uint16_t*>(var->data)[i];
            case MAT_C_INT16:  return static_cast<const int16_t*>(var->data)[i];
            case MAT_C_UINT32: return static_cast<const uint32_t*>(var->data)[i];
            case MAT_C_INT32:  return static_cast<const int32_t*>(var->data)[i];
            case MAT_C_UINT8:  return static_cast<const uint8_t*>(var->data)[i];
            default:           return static_cast<const double*>(var->data)[i];
        }
    };
    int x = static_cast<int>(std::lround(at(0)));
    int y = static_cast<int>(std::lround(at(1)));

    Mat_VarFree(var);
    Mat_Close(matfp);
    return {SeedParams(x, y)};
}

// Load seeds from either a JSON array or an xDIC .mat seed file, by extension.
std::vector<SeedParams> load_seeds_from_json(const std::string& seeds_path); // fwd decl
std::vector<SeedParams> load_seeds_any(const std::string& seeds_path) {
    if (file_ext_lower(seeds_path) == ".mat") return load_seeds_from_mat(seeds_path);
    return load_seeds_from_json(seeds_path);
}

// ============================================================================
// Seeds file loading
// ============================================================================
std::vector<SeedParams> load_seeds_from_json(const std::string& seeds_path) {
    std::vector<SeedParams> seeds;
    std::ifstream file(seeds_path);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open seeds file: " + seeds_path);
    }

    json j;
    file >> j;

    if (j.is_array()) {
        for (const auto& seed_json : j) {
            SeedParams seed;
            seed.x = seed_json.value("x", 0);
            seed.y = seed_json.value("y", 0);
            seed.u = seed_json.value("u", 0.0);
            seed.v = seed_json.value("v", 0.0);
            seed.du_dx = seed_json.value("du_dx", 0.0);
            seed.du_dy = seed_json.value("du_dy", 0.0);
            seed.dv_dx = seed_json.value("dv_dx", 0.0);
            seed.dv_dy = seed_json.value("dv_dy", 0.0);
            seed.corrcoef = seed_json.value("corrcoef", 0.0);
            seeds.push_back(seed);
        }
    }

    return seeds;
}

// ============================================================================
// Configuration file parsing
// ============================================================================
ProxyConfig parse_config_file(const std::string& config_path) {
    ProxyConfig config;
    std::ifstream file(config_path);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open config file: " + config_path);
    }

    std::string line;
    while (std::getline(file, line)) {
        // Skip empty lines and comments
        if (line.empty() || line[0] == '#') continue;

        // Remove leading/trailing whitespace
        size_t start = line.find_first_not_of(" \t");
        size_t end = line.find_last_not_of(" \t");
        if (start == std::string::npos) continue;
        line = line.substr(start, end - start + 1);

        // Parse key=value
        size_t eq_pos = line.find('=');
        if (eq_pos == std::string::npos) continue;

        std::string key = line.substr(0, eq_pos);
        std::string value = line.substr(eq_pos + 1);

        // Trim key and value
        key.erase(key.find_last_not_of(" \t") + 1);
        value.erase(0, value.find_first_not_of(" \t"));

        // Parse each parameter
        if (key == "folder")
            config.folder = value;
        else if (key == "roi")
            config.roi_path = value;
        else if (key == "ref")
            config.ref_path = value;
        else if (key == "output")
            config.output_dir = value;
        else if (key == "scalefactor")
            config.scalefactor = std::stoi(value);
        else if (key == "interp")
            config.interp_type = value;
        else if (key == "subregion")
            config.subregion_type = value;
        else if (key == "radius")
            config.subregion_radius = std::stoi(value);
        else if (key == "threads")
            config.num_threads = std::stoi(value);
        else if (key == "dic_config")
            config.dic_config = value;
        else if (key == "debug")
            config.debug = (value == "true" || value == "1");
        else if (key == "perspective_interp")
            config.perspective_interp = value;
        else if (key == "units")
            config.units = value;
        else if (key == "units_per_pixel")
            config.units_per_pixel = std::stod(value);
        else if (key == "strain_subregion")
            config.strain_subregion_type = value;
        else if (key == "strain_radius")
            config.strain_radius = std::stoi(value);
        else if (key == "alpha")
            config.alpha = std::stod(value);
        else if (key == "fps")
            config.fps = std::stod(value);
        else if (key == "save_json")
            config.save_json = (value == "true" || value == "1");
        else if (key == "save_binary")
            config.save_binary = (value == "true" || value == "1");
        else if (key == "save_videos")
            config.save_videos = (value == "true" || value == "1");
        else if (key == "algorithm_mode")
            config.algorithm_mode = value;
        else if (key == "seeds_file")
            config.seeds_file = value;
        else if (key == "seeds_are_optimized")
            config.seeds_are_optimized = (value == "true" || value == "1");
    }

    return config;
}

// ============================================================================
// In-memory pass-through path (ncorr::NcorrSession)
// ============================================================================
//
// Loads frames with OpenCV into owning cv::Mats, wraps each as a thin
// ncorr::ImageBuffer view, and drives ncorr::NcorrSession directly. No frames
// are written to or re-read from disk for the DIC itself; only the resulting
// displacement fields are dumped per-frame as lightweight JSON.

// Load an image from disk into an owning, contiguous 8-bit cv::Mat.
// Returns BGR (3-channel) for colour inputs and single-channel for grayscale,
// matching the interleaved layout ncorr::ImageBuffer expects.
static cv::Mat load_mat(const std::string& path) {
    cv::Mat img = cv::imread(path, cv::IMREAD_UNCHANGED);
    if (img.empty()) {
        throw std::runtime_error("Cannot load image: " + path);
    }
    // Normalise to 8-bit depth; ncorr::ImageBuffer assumes 8-bit samples.
    if (img.depth() != CV_8U) {
        cv::Mat tmp;
        img.convertTo(tmp, CV_8U);
        img = tmp;
    }
    // Drop alpha (4-channel) down to BGR so channels match the documented
    // 1 = grayscale / 3 = BGR contract.
    if (img.channels() == 4) {
        cv::Mat tmp;
        cv::cvtColor(img, tmp, cv::COLOR_BGRA2BGR);
        img = tmp;
    }
    // Guarantee contiguous storage so ImageBuffer's flat-row-major view is valid.
    if (!img.isContinuous()) {
        img = img.clone();
    }
    return img;
}

// Wrap an owning cv::Mat as a non-owning ncorr::ImageBuffer view. The Mat must
// outlive every use of the returned buffer.
static ncorr::ImageBuffer as_buffer(const cv::Mat& m) {
    return ncorr::ImageBuffer(m.ptr<std::uint8_t>(0), m.cols, m.rows, m.channels());
}

// Serialise a single DICResult to lightweight per-frame JSON. The displacement
// fields are emitted as flat row-major arrays (length width*height), matching
// the DICResult contract; NaN (out-of-ROI) is encoded as JSON null.
static json dic_result_to_json(const ncorr::DICResult& r, int frame_index,
                               const std::string& frame_path) {
    auto field_to_json = [](const std::vector<double>& v) {
        json arr = json::array();
        for (double x : v) {
            if (std::isnan(x))
                arr.push_back(nullptr);
            else
                arr.push_back(x);
        }
        return arr;
    };

    json j;
    j["frame_index"] = frame_index;
    j["frame_path"] = frame_path;
    j["valid"] = r.valid;
    j["message"] = r.message;
    j["width"] = r.width;
    j["height"] = r.height;
    j["u"] = field_to_json(r.u);
    j["v"] = field_to_json(r.v);
    j["corrcoef"] = field_to_json(r.corrcoef);
    return j;
}

// Drive the in-memory NcorrSession path end to end and dump per-frame JSON.
// Returns 0 on success, non-zero if any frame failed to process.
static int run_in_memory(const ProxyConfig& config, const std::string& roi_path,
                         const std::string& ref_path, const std::vector<std::string>& frame_paths) {
    LOG_INFO << "[IN-MEMORY MODE] Driving ncorr::NcorrSession (no disk round-trip)";

    ncorr::SessionConfig scfg;
    scfg.scalefactor = config.scalefactor;
    scfg.subregion_radius = config.subregion_radius;
    scfg.strain_radius = config.strain_radius;
    scfg.num_threads = config.num_threads;
    scfg.debug = config.debug;

    ncorr::NcorrSession session(scfg);

    // Reference frame. Keep the owning Mat alive for the whole session.
    cv::Mat ref_mat = load_mat(ref_path);
    session.set_reference(as_buffer(ref_mat));
    LOG_INFO << "Reference set: " << ref_path << " (" << ref_mat.cols << "x" << ref_mat.rows
             << ", " << ref_mat.channels() << "ch)";

    // Optional ROI mask (same geometry as the reference).
    cv::Mat roi_mat; // declared here so it outlives set_roi()
    if (!roi_path.empty() && file_exists(roi_path)) {
        roi_mat = (file_ext_lower(roi_path) == ".mat") ? load_roi_mask_from_mat(roi_path)
                                                       : load_mat(roi_path);
        session.set_roi(as_buffer(roi_mat));
        LOG_INFO << "ROI mask set: " << roi_path;
    } else {
        LOG_INFO << "No ROI mask; analysing full frame.";
    }

    system(("mkdir -p " + config.output_dir + "/in_memory").c_str());

    int failures = 0;
    int frame_index = 0;
    for (const auto& path : frame_paths) {
        if (path == ref_path) continue; // skip self if ref is also a frame

        cv::Mat def_mat = load_mat(path);
        ncorr::DICResult result = session.process_frame(as_buffer(def_mat));

        if (result.valid) {
            LOG_INFO << "  [frame " << frame_index << "] " << path << " -> " << result.width << "x"
                     << result.height << " disp field";
        } else {
            LOG_ERROR << "  [frame " << frame_index << "] " << path << " FAILED: " << result.message;
            ++failures;
        }

        if (config.save_json) {
            json j = dic_result_to_json(result, frame_index, path);
            std::ostringstream fname;
            fname << config.output_dir << "/in_memory/frame_" << std::setw(4) << std::setfill('0')
                  << frame_index << ".json";
            std::ofstream out(fname.str());
            out << std::setw(2) << j << std::endl;
        }
        ++frame_index;
    }

    LOG_INFO << "[IN-MEMORY MODE] Processed " << frame_index << " frame(s), " << failures
             << " failure(s). Displacement JSON in: " << config.output_dir << "/in_memory";

    return failures == 0 ? 0 : 1;
}

// ============================================================================
// Usage and help
// ============================================================================
void print_usage(const char* prog_name) {
    std::cout << "Usage: " << prog_name << " [OPTIONS]\n\n"
              << "A flexible DIC analysis tool that discovers frames from a folder.\n\n"
              << "OPTIONS:\n"
              << "  -f, --folder <path>        Image folder (default: images)\n"
              << "  -c, --config <path>        Config file path (overrides defaults)\n"
              << "  -r, --roi <path>           ROI mask: image (PNG/BMP/...) or xDIC .mat\n"
              << "                             ('refmask' var). Default: <folder>/roi.mat then roi.png\n"
              << "  -R, --ref <path>           Reference image path (default: first frame)\n"
              << "  -o, --output <path>        Output directory (default: output)\n"
              << "  -s, --scalefactor <int>    Scale factor (default: 3)\n"
              << "  -i, --interp <type>        Interpolation: NEAREST, LINEAR, CUBIC_KEYS,\n"
              << "                             CUBIC_KEYS_PRECOMPUTE, QUINTIC_BSPLINE,\n"
              << "                             QUINTIC_BSPLINE_PRECOMPUTE (default)\n"
              << "  -S, --subregion <type>     Subregion: CIRCLE (default), SQUARE\n"
              << "  -d, --radius <int>         Subregion radius (default: 20)\n"
              << "  -t, --threads <int>        Number of threads (default: 4)\n"
              << "  -u, --units <str>          Units string (default: mm)\n"
              << "  -p, --units-per-pixel <f>  Units per pixel (default: 0.2)\n"
              << "  --strain-subregion <type>  Strain subregion type (default: CIRCLE)\n"
              << "  --strain-radius <int>      Strain radius (default: 5)\n"
              << "  -m, --mode <mode>          Algorithm mode (default: auto):\n"
              << "                               auto                    pick parallel/sequential\n"
              << "                               sequential              auto/seeded sequential RG-DIC\n"
              << "                               parallel                auto/seeded threaded RG-DIC\n"
              << "                               matlab-parallel         MATLAB-ABR seeded parallel\n"
              << "                               matlab-sequential       MATLAB-ABR seeded sequential\n"
              << "                               exact-matlab-parallel   exact MATLAB-ncorr parallel\n"
              << "                               exact-matlab-sequential exact MATLAB-ncorr sequential\n"
              << "  --seeds <path>             Seeds: JSON array OR xDIC .mat ('seed_point' [x,y])\n"
              << "  --seeds-optimized          Seeds are already optimized (skip optimization)\n"
              << "  -a, --alpha <float>        Video overlay alpha (default: 0.5)\n"
              << "  -F, --fps <float>          Video FPS (default: 15)\n"
              << "  --no-json                  Disable JSON output\n"
              << "  --no-binary                Disable binary output\n"
              << "  --no-videos                Disable video output\n"
              << "  --in-memory                Use the in-memory ncorr::NcorrSession path\n"
              << "                             (loads frames with OpenCV, no disk round-trip;\n"
              << "                             dumps per-frame u/v/corrcoef JSON)\n"
              << "  --debug                    Enable debug mode\n"
              << "  -h, --help                 Show this help message\n\n"
              << "CONFIG FILE FORMAT (config.txt):\n"
              << "  # Comment lines start with #\n"
              << "  folder = images\n"
              << "  roi = path/to/roi.png\n"
              << "  ref = path/to/ref.png\n"
              << "  scalefactor = 3\n"
              << "  interp = QUINTIC_BSPLINE_PRECOMPUTE\n"
              << "  subregion = CIRCLE\n"
              << "  radius = 20\n"
              << "  threads = 4\n"
              << "  dic_config = NO_UPDATE\n"
              << "  units = mm\n"
              << "  units_per_pixel = 0.2\n"
              << "  strain_subregion = CIRCLE\n"
              << "  strain_radius = 5\n"
              << "  algorithm_mode = auto          # auto|sequential|parallel|matlab-parallel|\n"
              << "                                 # matlab-sequential|exact-matlab-parallel|...\n"
              << "  seeds_file = path/to/seeds.json   # or REF_SEED_*.mat\n"
              << "  seeds_are_optimized = false\n"
              << "  alpha = 0.5\n"
              << "  fps = 15\n\n"
              << "SEEDS FILE FORMAT (seeds.json):\n"
              << "  [\n"
              << "    {\"x\": 100, \"y\": 200, \"u\": 0.0, \"v\": 0.0},\n"
              << "    {\"x\": 300, \"y\": 400, \"u\": 0.5, \"v\": 0.3}\n"
              << "  ]\n"
              << std::endl;
}

// ============================================================================
// Main
// ============================================================================
int main(int argc, char* argv[]) {
    ProxyConfig config;

    // Long options
    static struct option long_options[] = {{"folder", required_argument, 0, 'f'},
                                           {"config", required_argument, 0, 'c'},
                                           {"roi", required_argument, 0, 'r'},
                                           {"ref", required_argument, 0, 'R'},
                                           {"output", required_argument, 0, 'o'},
                                           {"scalefactor", required_argument, 0, 's'},
                                           {"interp", required_argument, 0, 'i'},
                                           {"subregion", required_argument, 0, 'S'},
                                           {"radius", required_argument, 0, 'd'},
                                           {"threads", required_argument, 0, 't'},
                                           {"units", required_argument, 0, 'u'},
                                           {"units-per-pixel", required_argument, 0, 'p'},
                                           {"strain-subregion", required_argument, 0, 1001},
                                           {"strain-radius", required_argument, 0, 1002},
                                           {"mode", required_argument, 0, 'm'},
                                           {"seeds", required_argument, 0, 1007},
                                           {"seeds-optimized", no_argument, 0, 1008},
                                           {"alpha", required_argument, 0, 'a'},
                                           {"fps", required_argument, 0, 'F'},
                                           {"no-json", no_argument, 0, 1003},
                                           {"no-binary", no_argument, 0, 1004},
                                           {"no-videos", no_argument, 0, 1005},
                                           {"in-memory", no_argument, 0, 1009},
                                           {"debug", no_argument, 0, 1006},
                                           {"help", no_argument, 0, 'h'},
                                           {0, 0, 0, 0}};

    std::string config_file = "";

    // First pass: check for config file
    int opt;
    int option_index = 0;
    while ((opt = getopt_long(argc, argv, "f:c:r:R:o:s:i:S:d:t:u:p:m:a:F:h", long_options,
                              &option_index)) != -1) {
        if (opt == 'c') {
            config_file = optarg;
            break;
        }
    }

    // Load config file if specified
    if (!config_file.empty()) {
        LOG_INFO << "Loading config from: " << config_file;
        config = parse_config_file(config_file);
    }

    // Reset getopt
    optind = 1;

    // Second pass: override with command line arguments
    while ((opt = getopt_long(argc, argv, "f:c:r:R:o:s:i:S:d:t:u:p:m:a:F:h", long_options,
                              &option_index)) != -1) {
        switch (opt) {
            case 'f':
                config.folder = optarg;
                break;
            case 'c': /* already handled */
                break;
            case 'r':
                config.roi_path = optarg;
                break;
            case 'R':
                config.ref_path = optarg;
                break;
            case 'o':
                config.output_dir = optarg;
                break;
            case 's':
                config.scalefactor = std::stoi(optarg);
                break;
            case 'i':
                config.interp_type = optarg;
                break;
            case 'S':
                config.subregion_type = optarg;
                break;
            case 'd':
                config.subregion_radius = std::stoi(optarg);
                break;
            case 't':
                config.num_threads = std::stoi(optarg);
                break;
            case 'u':
                config.units = optarg;
                break;
            case 'p':
                config.units_per_pixel = std::stod(optarg);
                break;
            case 1001:
                config.strain_subregion_type = optarg;
                break;
            case 1002:
                config.strain_radius = std::stoi(optarg);
                break;
            case 'm':
                config.algorithm_mode = optarg;
                break;
            case 1007:
                config.seeds_file = optarg;
                break;
            case 1008:
                config.seeds_are_optimized = true;
                break;
            case 'a':
                config.alpha = std::stod(optarg);
                break;
            case 'F':
                config.fps = std::stod(optarg);
                break;
            case 1003:
                config.save_json = false;
                break;
            case 1004:
                config.save_binary = false;
                break;
            case 1005:
                config.save_videos = false;
                break;
            case 1009:
                config.in_memory = true;
                break;
            case 1006:
                config.debug = true;
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }

    // Resolve ROI path. When unspecified, prefer an xDIC-style .mat mask, then
    // fall back to the legacy roi.png. An explicit --roi/roi= path (.mat, .png,
    // ...) is honoured verbatim.
    std::string roi_path = config.roi_path;
    if (roi_path.empty()) {
        std::string roi_mat = config.folder + "/roi.mat";
        roi_path = file_exists(roi_mat) ? roi_mat : config.folder + "/roi.png";
    }

    // Check ROI exists
    if (!file_exists(roi_path)) {
        LOG_ERROR << "ROI file not found: " << roi_path;
        return 1;
    }

    // Discover frames
    LOG_INFO << "Discovering frames in: " << config.folder;
    std::vector<std::string> frame_paths;
    try {
        frame_paths = discover_frames(config.folder, config.ref_path, roi_path);
    } catch (const std::exception& e) {
        LOG_ERROR << e.what();
        return 1;
    }

    if (frame_paths.empty()) {
        LOG_ERROR << "No frames found in folder: " << config.folder;
        return 1;
    }

    LOG_INFO << "Found " << frame_paths.size() << " frames";

    // Handle reference image
    std::string ref_path = config.ref_path;
    if (ref_path.empty()) {
        // Check if ref.png exists
        std::string default_ref = config.folder + "/ref.png";
        if (file_exists(default_ref)) {
            ref_path = default_ref;
            LOG_INFO << "Using ref.png as reference image";
        } else {
            // Use first frame as reference
            ref_path = frame_paths[0];
            LOG_INFO << "Using first frame as reference: " << ref_path;
        }
    }

    // Build image list: reference first, then all frames
    std::vector<Image2D> imgs;
    imgs.push_back(Image2D(ref_path));
    for (const auto& path : frame_paths) {
        if (path != ref_path) { // Don't duplicate if ref is also a frame
            imgs.push_back(Image2D(path));
        }
    }

    LOG_INFO << "Total images for analysis: " << imgs.size();

    // Load seeds if specified (JSON array or xDIC .mat REF_SEED, by extension)
    if (!config.seeds_file.empty()) {
        if (file_exists(config.seeds_file)) {
            LOG_INFO << "Loading seeds from: " << config.seeds_file;
            try {
                config.seeds_by_region = load_seeds_any(config.seeds_file);
                LOG_INFO << "Loaded " << config.seeds_by_region.size() << " seed(s)";
            } catch (const std::exception& e) {
                LOG_ERROR << "Failed to load seeds: " << e.what();
                return 1;
            }
        } else {
            LOG_WARN << "Seeds file not found: " << config.seeds_file;
        }
    }

    // Print configuration
    LOG_INFO << "=== Configuration ===";
    LOG_INFO << "ROI: " << roi_path;
    LOG_INFO << "Reference: " << ref_path;
    LOG_INFO << "Scale factor: " << config.scalefactor;
    LOG_INFO << "Interpolation: " << config.interp_type;
    LOG_INFO << "Subregion: " << config.subregion_type << " (r=" << config.subregion_radius << ")";
    LOG_INFO << "Threads: " << config.num_threads;
    LOG_INFO << "Algorithm mode: " << config.algorithm_mode;
    if (!config.seeds_by_region.empty()) {
        LOG_INFO << "Seeds: " << config.seeds_by_region.size() << " region(s)"
                 << (config.seeds_are_optimized ? " (pre-optimized)" : " (will be optimized)");
    }
    LOG_INFO << "Units: " << config.units << " (" << config.units_per_pixel << " per pixel)";
    LOG_INFO << "Strain subregion: " << config.strain_subregion_type
             << " (r=" << config.strain_radius << ")";
    LOG_INFO << "Alpha: " << config.alpha << ", FPS: " << config.fps;
    LOG_INFO << "=====================";

    // In-memory pass-through path: drive ncorr::NcorrSession directly and skip
    // the file-based DIC/strain/video pipeline entirely.
    if (config.in_memory) {
        try {
            return run_in_memory(config, roi_path, ref_path, frame_paths);
        } catch (const std::exception& e) {
            LOG_ERROR << "Error during in-memory analysis: " << e.what();
            return 1;
        }
    }

    // Initialize DIC and strain structures
    DIC_analysis_input DIC_input;
    DIC_analysis_output DIC_output;
    strain_analysis_input strain_input;
    strain_analysis_output strain_output;

    try {
        // Set DIC_input
        DIC_input = DIC_analysis_input(imgs, build_roi(roi_path),
                                       config.scalefactor, parse_interp(config.interp_type),
                                       parse_subregion(config.subregion_type),
                                       config.subregion_radius, config.num_threads,
                                       parse_dic_config(config.dic_config), config.debug);

        // Perform DIC analysis based on mode and seeds.
        bool has_seeds = !config.seeds_by_region.empty();

        // Normalise the mode string: lowercase, '_' -> '-' so "matlab_parallel"
        // and "matlab-parallel" are equivalent.
        std::string effective_mode = config.algorithm_mode;
        std::transform(effective_mode.begin(), effective_mode.end(), effective_mode.begin(),
                       [](unsigned char c) { return c == '_' ? '-' : std::tolower(c); });

        // Determine effective mode for "auto".
        if (effective_mode == "auto") {
            if (has_seeds) {
                effective_mode = "sequential"; // Use sequential with seeds by default
                LOG_INFO << "[AUTO MODE] Seeds provided -> using sequential mode with seeds";
            } else {
                effective_mode = "parallel"; // Use parallel (threaded) by default
                LOG_INFO << "[AUTO MODE] No seeds -> using parallel (threaded) mode";
            }
        }

        const bool is_matlab = effective_mode == "matlab-parallel" ||
                               effective_mode == "matlab-sequential";
        const bool is_exact = effective_mode == "exact-matlab-parallel" ||
                              effective_mode == "exact-matlab-sequential";
        if ((is_matlab || is_exact) && !has_seeds) {
            LOG_WARN << "[" << effective_mode << "] No seeds supplied; the MATLAB-style seed "
                        "analysis runs best with a --seeds .mat/.json. Proceeding without.";
        }

        // Execute based on effective mode.
        if (effective_mode == "sequential") {
            if (has_seeds) {
                LOG_INFO << "[SEQUENTIAL MODE] Performing DIC analysis with "
                         << config.seeds_by_region.size() << " user-provided seed(s)"
                         << (config.seeds_are_optimized
                                 ? " (pre-optimized, skipping optimization step)"
                                 : "")
                         << "...";

                // Use the unambiguous 3-arg overload (the 1-arg form is ambiguous
                // because DIC_analysis_parallel_input converts to DIC_analysis_input).
                DIC_output = DIC_analysis_sequential(DIC_input, config.seeds_by_region,
                                                     config.seeds_are_optimized);
            } else {
                LOG_INFO
                    << "[SEQUENTIAL MODE] Performing DIC analysis with auto-generated seeds...";
                DIC_output = DIC_analysis_sequential(DIC_input, {}, false);
            }
        } else if (effective_mode == "parallel") {
            if (has_seeds) {
                LOG_INFO << "[PARALLEL MODE] Performing parallel DIC analysis with "
                         << config.seeds_by_region.size() << " user-provided seed(s)"
                         << (config.seeds_are_optimized ? " (pre-optimized)" : "") << "...";

                DIC_analysis_parallel_input parallel_input(DIC_input, config.seeds_by_region,
                                                           config.seeds_are_optimized);
                DIC_output = DIC_analysis_parallel(parallel_input);
            } else {
                LOG_INFO << "[PARALLEL MODE] Performing parallel DIC analysis with auto-generated "
                            "seeds...";
                DIC_output = DIC_analysis(DIC_input);
            }
        } else if (effective_mode == "matlab-parallel") {
            // MATLAB-ABR-style fixed-reference seed-analysis + parallel DIC.
            // This is the path the production xDIC stepD pipeline uses.
            LOG_INFO << "[MATLAB-PARALLEL MODE] MATLAB-style seeded parallel DIC ("
                     << config.seeds_by_region.size() << " seed(s))...";
            DIC_analysis_parallel_input parallel_input(DIC_input, config.seeds_by_region,
                                                       config.seeds_are_optimized);
            DIC_output = matlab_DIC_analysis_parallel(parallel_input);
        } else if (effective_mode == "matlab-sequential") {
            LOG_INFO << "[MATLAB-SEQUENTIAL MODE] MATLAB-style seeded sequential DIC ("
                     << config.seeds_by_region.size() << " seed(s))...";
            DIC_output = matlab_DIC_analysis_sequential(DIC_input, config.seeds_by_region,
                                                        config.seeds_are_optimized);
        } else if (effective_mode == "exact-matlab-parallel") {
            // Exact MATLAB-ncorr-mirroring chain composition (see ncorr.h).
            LOG_INFO << "[EXACT-MATLAB-PARALLEL MODE] Exact MATLAB-ncorr seeded parallel DIC ("
                     << config.seeds_by_region.size() << " seed(s))...";
            DIC_analysis_parallel_input parallel_input(DIC_input, config.seeds_by_region,
                                                       config.seeds_are_optimized);
            DIC_output = exact_matlab_DIC_analysis_parallel(parallel_input);
        } else if (effective_mode == "exact-matlab-sequential") {
            LOG_INFO << "[EXACT-MATLAB-SEQUENTIAL MODE] Exact MATLAB-ncorr seeded sequential DIC ("
                     << config.seeds_by_region.size() << " seed(s))...";
            DIC_output = exact_matlab_DIC_analysis_sequential(DIC_input, config.seeds_by_region,
                                                              config.seeds_are_optimized);
        } else {
            throw std::runtime_error(
                "Unknown algorithm mode: " + effective_mode +
                ". Use: auto, sequential, parallel, matlab-parallel, matlab-sequential, "
                "exact-matlab-parallel, or exact-matlab-sequential");
        }

        // Convert to Eulerian perspective
        LOG_INFO << "Converting to Eulerian perspective...";
        DIC_output = change_perspective(DIC_output, parse_interp(config.perspective_interp));

        // Set units
        DIC_output = set_units(DIC_output, config.units, config.units_per_pixel);

        // Set strain input
        strain_input = strain_analysis_input(DIC_input, DIC_output,
                                             parse_subregion(config.strain_subregion_type),
                                             config.strain_radius);

        // Perform strain analysis
        LOG_INFO << "Performing strain analysis...";
        strain_output = strain_analysis(strain_input);

        // Create output directories
        system(("mkdir -p " + config.output_dir + "/save").c_str());
        system(("mkdir -p " + config.output_dir + "/save_json").c_str());
        system(("mkdir -p " + config.output_dir + "/video").c_str());

        // Save outputs
        if (config.save_binary) {
            LOG_INFO << "Saving binary outputs...";
            save(DIC_input, config.output_dir + "/save/DIC_input.bin");
            save(DIC_output, config.output_dir + "/save/DIC_output.bin");
            save(strain_input, config.output_dir + "/save/strain_input.bin");
            save(strain_output, config.output_dir + "/save/strain_output.bin");
        }

        if (config.save_json) {
            LOG_INFO << "Saving JSON outputs...";
            save_as_json(DIC_input, DIC_output, strain_input, strain_output,
                         config.output_dir + "/save_json");
        }

        // Create videos
        if (config.save_videos) {
            LOG_INFO << "Creating videos...";

            save_DIC_video(config.output_dir + "/video/v_eulerian.avi", DIC_input, DIC_output,
                           DISP::V, config.alpha, config.fps);

            save_DIC_video(config.output_dir + "/video/u_eulerian.avi", DIC_input, DIC_output,
                           DISP::U, config.alpha, config.fps);

            save_strain_video(config.output_dir + "/video/eyy_eulerian.avi", strain_input,
                              strain_output, STRAIN::EYY, config.alpha, config.fps);

            save_strain_video(config.output_dir + "/video/exy_eulerian.avi", strain_input,
                              strain_output, STRAIN::EXY, config.alpha, config.fps);

            save_strain_video(config.output_dir + "/video/exx_eulerian.avi", strain_input,
                              strain_output, STRAIN::EXX, config.alpha, config.fps);
        }

        LOG_INFO << "Analysis complete! Results saved to: " << config.output_dir;

    } catch (const std::exception& e) {
        LOG_ERROR << "Error during analysis: " << e.what();
        return 1;
    }

    return 0;
}
