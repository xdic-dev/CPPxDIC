/**
 * Parity Test for StepD Workflow vs MATLAB var_test2.mat
 * 
 * This test replicates the StepD workflow logic for trial 007, stereopair 1
 * and compares intermediate outputs against the MATLAB reference file var_test2.mat.
 * 
 * Variables compared:
 * - cfr1: cam_first_raw[0] (raw image)
 * - cfs1: cam_first_satur[0] (saturated image)
 * - cf1: cam_first[0] (filtered image)
 * - refmask_REF: reference ROI mask
 * - refmask_trial: trial ROI mask
 * - refmask_trial_matched: matched trial mask from performMatching
 * - initial_seed_point_set1: seed point for camera 1
 * - initial_seed_point_set2: seed point for camera 2
 * - gsboundaries: grayscale boundaries from filtering
 */

#include <iostream>
#include <string>
#include <vector>
#include <iomanip>
#include <filesystem>
#include <cmath>

#include <matio.h>
#include <opencv2/opencv.hpp>

// Include CPPxDIC headers
#include "config.h"
#include "parameters.h"
#include "image_processor.h"
#include "roi_manager.h"
#include "utils.h"
#include <ncorr.h>

namespace fs = std::filesystem;
using namespace cppxdic;

// ============================================================================
// MAT File Reading Utilities
// ============================================================================

cv::Mat readMatFromVar(matvar_t* var) {
    if (!var || !var->data || var->rank != 2) {
        return cv::Mat();
    }
    
    int rows = static_cast<int>(var->dims[0]);
    int cols = static_cast<int>(var->dims[1]);
    
    cv::Mat result;
    
    if (var->class_type == MAT_C_UINT8) {
        result = cv::Mat(rows, cols, CV_8UC1);
        uint8_t* src = static_cast<uint8_t*>(var->data);
        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                result.at<uint8_t>(r, c) = src[c * rows + r];
            }
        }
    } else if (var->class_type == MAT_C_DOUBLE) {
        result = cv::Mat(rows, cols, CV_64FC1);
        double* src = static_cast<double*>(var->data);
        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                result.at<double>(r, c) = src[c * rows + r];
            }
        }
    }
    
    return result;
}

template<typename T>
std::vector<T> readStructFieldArray(matvar_t* struct_var, const char* field_name) {
    std::vector<T> result;
    
    matvar_t* field = Mat_VarGetStructFieldByName(struct_var, field_name, 0);
    if (!field || !field->data) {
        return result;
    }
    
    size_t n_elements = 1;
    for (int i = 0; i < field->rank; ++i) {
        n_elements *= field->dims[i];
    }
    
    result.resize(n_elements);
    
    if (field->class_type == MAT_C_UINT8) {
        uint8_t* src = static_cast<uint8_t*>(field->data);
        for (size_t i = 0; i < n_elements; ++i) {
            result[i] = static_cast<T>(src[i]);
        }
    } else if (field->class_type == MAT_C_UINT16) {
        uint16_t* src = static_cast<uint16_t*>(field->data);
        for (size_t i = 0; i < n_elements; ++i) {
            result[i] = static_cast<T>(src[i]);
        }
    } else if (field->class_type == MAT_C_DOUBLE) {
        double* src = static_cast<double*>(field->data);
        for (size_t i = 0; i < n_elements; ++i) {
            result[i] = static_cast<T>(src[i]);
        }
    }
    
    return result;
}

std::pair<double, double> readGsBoundaries(mat_t* matfp) {
    matvar_t* gsb = Mat_VarRead(matfp, "gsboundaries");
    if (!gsb || !gsb->data) {
        return {0.0, 0.0};
    }
    
    double* data = static_cast<double*>(gsb->data);
    std::pair<double, double> result = {data[0], data[1]};
    Mat_VarFree(gsb);
    return result;
}

// ============================================================================
// Comparison Utilities
// ============================================================================

struct ComparisonResult {
    bool passed = true;
    int total_tests = 0;
    int passed_tests = 0;
    std::vector<std::string> errors;
    std::vector<std::string> info;
    
    void addError(const std::string& msg) {
        errors.push_back(msg);
        passed = false;
    }
    
    void addInfo(const std::string& msg) {
        info.push_back(msg);
    }
    
    void recordTest(bool success, const std::string& name) {
        total_tests++;
        if (success) {
            passed_tests++;
            addInfo(name + ": PASSED");
        } else {
            addError(name + ": FAILED");
        }
    }
    
    void print() const {
        std::cout << "\n========================================\n";
        std::cout << "PARITY TEST RESULT: " << (passed ? "PASSED" : "FAILED") << "\n";
        std::cout << "Tests: " << passed_tests << "/" << total_tests << " passed\n";
        std::cout << "========================================\n";
        
        if (!info.empty()) {
            std::cout << "\nDETAILS:\n";
            for (const auto& msg : info) {
                std::cout << "  " << msg << "\n";
            }
        }
        
        if (!errors.empty()) {
            std::cout << "\nERRORS:\n";
            for (const auto& msg : errors) {
                std::cout << "  [ERROR] " << msg << "\n";
            }
        }
        
        std::cout << "========================================\n";
    }
};

// Compare two cv::Mat images
bool compareMat(const cv::Mat& cpp_mat, const cv::Mat& matlab_mat, 
                const std::string& name, ComparisonResult& result,
                double tolerance = 1e-6, bool is_mask = false) {
    if (cpp_mat.empty() && matlab_mat.empty()) {
        result.addInfo(name + ": Both empty (OK)");
        return true;
    }
    
    if (cpp_mat.empty()) {
        result.addError(name + ": C++ mat is empty");
        return false;
    }
    
    if (matlab_mat.empty()) {
        result.addError(name + ": MATLAB mat is empty");
        return false;
    }
    
    if (cpp_mat.rows != matlab_mat.rows || cpp_mat.cols != matlab_mat.cols) {
        result.addError(name + ": Size mismatch - C++: " + 
                       std::to_string(cpp_mat.rows) + "x" + std::to_string(cpp_mat.cols) +
                       ", MATLAB: " + std::to_string(matlab_mat.rows) + "x" + 
                       std::to_string(matlab_mat.cols));
        return false;
    }
    
    // Convert both to double for comparison
    cv::Mat cpp_double, matlab_double;
    cpp_mat.convertTo(cpp_double, CV_64F);
    matlab_mat.convertTo(matlab_double, CV_64F);
    
    // For masks, normalize both to 0/1 range for comparison
    if (is_mask) {
        double cpp_max, matlab_max;
        cv::minMaxLoc(cpp_double, nullptr, &cpp_max);
        cv::minMaxLoc(matlab_double, nullptr, &matlab_max);
        if (cpp_max > 1.0) cpp_double /= cpp_max;
        if (matlab_max > 1.0) matlab_double /= matlab_max;
    }
    
    cv::Mat diff;
    cv::absdiff(cpp_double, matlab_double, diff);
    
    double minVal, maxVal;
    cv::minMaxLoc(diff, &minVal, &maxVal);
    
    cv::Scalar mean_diff = cv::mean(diff);
    int nonzero_diff = cv::countNonZero(diff > tolerance);
    double pct_diff = 100.0 * nonzero_diff / (diff.rows * diff.cols);
    
    std::ostringstream oss;
    oss << name << ": max_diff=" << std::fixed << std::setprecision(4) << maxVal
        << ", mean_diff=" << mean_diff[0]
        << ", diff_pixels=" << nonzero_diff << " (" << std::setprecision(2) << pct_diff << "%)";
    
    bool success = (maxVal <= tolerance) || (pct_diff < 1.0);
    
    if (success) {
        result.addInfo(oss.str() + " [OK]");
    } else {
        result.addError(oss.str());
    }
    
    return success;
}

// Compare seed points
bool compareSeedPoint(const SeedPoint& cpp_seed, 
                      const std::vector<int>& matlab_pw,
                      const std::vector<int>& matlab_sw,
                      const std::string& name, ComparisonResult& result,
                      int tolerance = 2) {
    bool pw_match = (std::abs(static_cast<int>(cpp_seed.pw[0]) - matlab_pw[0]) <= tolerance) &&
                    (std::abs(static_cast<int>(cpp_seed.pw[1]) - matlab_pw[1]) <= tolerance);
    
    bool sw_match = (std::abs(static_cast<int>(cpp_seed.sw[0]) - matlab_sw[0]) <= tolerance) &&
                    (std::abs(static_cast<int>(cpp_seed.sw[1]) - matlab_sw[1]) <= tolerance);
    
    std::ostringstream oss;
    oss << name << ".pw: C++=[" << cpp_seed.pw[0] << "," << cpp_seed.pw[1] << "]"
        << " MATLAB=[" << matlab_pw[0] << "," << matlab_pw[1] << "]";
    
    if (pw_match) {
        result.addInfo(oss.str() + " [OK]");
    } else {
        result.addError(oss.str());
    }
    
    oss.str("");
    oss << name << ".sw: C++=[" << cpp_seed.sw[0] << "," << cpp_seed.sw[1] << "]"
        << " MATLAB=[" << matlab_sw[0] << "," << matlab_sw[1] << "]";
    
    if (sw_match) {
        result.addInfo(oss.str() + " [OK]");
    } else {
        result.addError(oss.str());
    }
    
    return pw_match && sw_match;
}

// ============================================================================
// Main Test
// ============================================================================

int main(int argc, char** argv) {
    std::string mat_path = "test_output/var_test2.mat";
    
    if (argc > 1) {
        mat_path = argv[1];
    }
    
    std::cout << "=== StepD Workflow Parity Test ===\n";
    std::cout << "Reference MAT file: " << mat_path << "\n\n";
    
    if (!fs::exists(mat_path)) {
        std::cerr << "Reference MAT file not found: " << mat_path << "\n";
        return 1;
    }
    
    // Open reference MAT file
    mat_t* matfp = Mat_Open(mat_path.c_str(), MAT_ACC_RDONLY);
    if (!matfp) {
        std::cerr << "Failed to open MAT file\n";
        return 1;
    }
    
    ComparisonResult result;
    
    // ========================================================================
    // Setup Config (matching trial 007, stereopair 1)
    // ========================================================================
    Config config;
    config.base_path = "/Users/jaoga/devlab/MultiDIC";
    config.data_path = config.base_path + "/example_data";
    // Use the main analysis path where cache files exist
    config.dic_path = config.base_path + "/analysis";
    config.subject_id = "S09";
    config.material_id = 1;  // coating (not coating_oil)
    config.material = config.frictional_conditions[config.material_id];
    config.phase_id = "loading";
    config.ref_trial_id = 5;
    config.im_filter_mode = true;
    config.debug_mode = true;
    
    std::string trial = "007";
    int stereopair = 1;
    
    // Setup base parameters
    BaseParameters base_params;
    base_params.baseDataPath = config.data_path;
    base_params.baseResultPath = config.dic_path;
    base_params.subject = config.subject_id;
    base_params.material = config.material;
    base_params.trial = trial;
    base_params.stereopair = stereopair;
    base_params.phase = config.phase_id;
    
    // Reference trial
    std::ostringstream reftrial_oss;
    reftrial_oss << std::setw(3) << std::setfill('0') << config.ref_trial_id;
    base_params.reftrial = reftrial_oss.str();
    
    // Set grayscale limit (S09 >= 8, so use higher limit)
    base_params.limit_grayscale = DICConstants::LIMIT_GRAYSCALE_S8_PLUS;
    
    // Camera numbers
    int cam_1 = (stereopair - 1) * 2 + 1;
    int cam_2 = (stereopair - 1) * 2 + 2;
    base_params.cam_1 = cam_1;
    base_params.cam_2 = cam_2;
    
    // Output path
    base_params.outputPath = Utils::buildOutputPath(base_params);
    
    
    // File paths
    base_params.roifile = Utils::buildRoiFilePath(base_params, base_params.reftrial, stereopair);
    base_params.seedfile = Utils::buildSeedFilePath(base_params, base_params.reftrial, stereopair);
    base_params.matchingfile = Utils::buildMatchingFilePath(base_params, base_params.reftrial, stereopair);
    
    std::cout << "Config:\n";
    std::cout << "  Trial: " << trial << "\n";
    std::cout << "  Stereopair: " << stereopair << " (cam " << cam_1 << ", " << cam_2 << ")\n";
    std::cout << "  Reference trial: " << base_params.reftrial << "\n";
    std::cout << "  ROI file: " << base_params.roifile << "\n";
    std::cout << "  Seed file: " << base_params.seedfile << "\n";
    std::cout << "\n";
    
    // ========================================================================
    // Step 1: Import video frames
    // ========================================================================
    std::cout << "--- Step 1: Import Video Frames ---\n";
    
    std::vector<std::string> cam1_paths, cam2_paths;
    int trial_num = std::stoi(trial);
    
    if (!Utils::importVid(config, trial_num, stereopair, cam1_paths, cam2_paths)) {
        std::cerr << "Failed to import video frames\n";
        Mat_Close(matfp);
        return 1;
    }
    
    if (cam1_paths.empty()) {
        std::cerr << "No frames found\n";
        Mat_Close(matfp);
        return 1;
    }
    
    // Load first frame only for parity test
    cv::Mat cam_first_raw_0 = cv::imread(cam1_paths[0], cv::IMREAD_GRAYSCALE);
    if (cam_first_raw_0.empty()) {
        std::cerr << "Failed to load first frame\n";
        Mat_Close(matfp);
        return 1;
    }
    
    std::cout << "Loaded frame: " << cam_first_raw_0.rows << "x" << cam_first_raw_0.cols << "\n";
    
    // Compare cfr1 (raw image)
    // Note: Video decoders (FFmpeg vs MATLAB VideoReader) produce slightly different results
    // Typical difference is 1-2 pixel values due to H.264 decoding differences
    matvar_t* cfr1_var = Mat_VarRead(matfp, "cfr1");
    if (cfr1_var) {
        cv::Mat cfr1_matlab = readMatFromVar(cfr1_var);
        
        // Debug: show some pixel values to understand the difference
        std::cout << "Debug - First 5 pixels at (0,0) to (0,4):\n";
        std::cout << "  C++:    ";
        for (int i = 0; i < 5; ++i) {
            std::cout << static_cast<int>(cam_first_raw_0.at<uchar>(0, i)) << " ";
        }
        std::cout << "\n  MATLAB: ";
        for (int i = 0; i < 5; ++i) {
            std::cout << static_cast<int>(cfr1_matlab.at<uchar>(0, i)) << " ";
        }
        std::cout << "\n";
        
        // Use tolerance of 3 for video decoder differences
        result.recordTest(compareMat(cam_first_raw_0, cfr1_matlab, "cfr1 (raw)", result, 3.0), "cfr1");
        Mat_VarFree(cfr1_var);
    } else {
        result.addError("cfr1 not found in MAT file");
    }
    
    // ========================================================================
    // Step 2: Saturation
    // ========================================================================
    std::cout << "\n--- Step 2: Saturation ---\n";
    
    std::vector<cv::Mat> raw_frames = {cam_first_raw_0};
    std::vector<cv::Mat> satur_frames = ImageProcessor::saturate(raw_frames, base_params.limit_grayscale);
    cv::Mat cam_first_satur_0 = satur_frames[0];
    
    std::cout << "Saturation limit: " << base_params.limit_grayscale << "\n";
    
    // Compare cfs1 (saturated image)
    // Note: Saturation differences propagate from raw image decoding differences
    // The saturation amplifies the ~2 pixel difference from raw to larger differences
    matvar_t* cfs1_var = Mat_VarRead(matfp, "cfs1");
    if (cfs1_var) {
        cv::Mat cfs1_matlab = readMatFromVar(cfs1_var);
        
        // Debug: show some pixel values
        std::cout << "Debug - Saturated first 5 pixels at (0,0) to (0,4):\n";
        std::cout << "  C++:    ";
        for (int i = 0; i < 5; ++i) {
            std::cout << static_cast<int>(cam_first_satur_0.at<uchar>(0, i)) << " ";
        }
        std::cout << "\n  MATLAB: ";
        for (int i = 0; i < 5; ++i) {
            std::cout << static_cast<int>(cfs1_matlab.at<uchar>(0, i)) << " ";
        }
        std::cout << "\n";
        
        // Use tolerance of 5 for video decoding differences (amplified by saturation)
        result.recordTest(compareMat(cam_first_satur_0, cfs1_matlab, "cfs1 (saturated)", result, 5.0), "cfs1");
        Mat_VarFree(cfs1_var);
    } else {
        result.addError("cfs1 not found in MAT file");
    }
    
    // ========================================================================
    // Step 3: Load ROI
    // ========================================================================
    std::cout << "\n--- Step 3: Load ROI ---\n";
    
    cv::Mat refmask_REF = ROIManager::loadOrCreateROI(base_params, cam_first_satur_0);
    
    if (refmask_REF.empty()) {
        std::cerr << "Failed to load ROI\n";
        Mat_Close(matfp);
        return 1;
    }
    
    std::cout << "ROI loaded: " << refmask_REF.rows << "x" << refmask_REF.cols << "\n";
    
    // Compare refmask_REF (use is_mask=true to normalize 0/255 vs 0/1)
    matvar_t* refmask_REF_var = Mat_VarRead(matfp, "refmask_REF");
    if (refmask_REF_var) {
        cv::Mat refmask_REF_matlab = readMatFromVar(refmask_REF_var);
        result.recordTest(compareMat(refmask_REF, refmask_REF_matlab, "refmask_REF", result, 0.0, true), "refmask_REF");
        Mat_VarFree(refmask_REF_var);
    } else {
        result.addError("refmask_REF not found in MAT file");
    }
    
    // ========================================================================
    // Step 4: Load Seed
    // ========================================================================
    std::cout << "\n--- Step 4: Load Seed ---\n";
    
    SeedPoint ref_seed_point = ROIManager::loadOrCreateSeed(base_params, refmask_REF);
    
    std::cout << "Seed loaded: pw=[" << ref_seed_point.pw[0] << "," << ref_seed_point.pw[1] << "]\n";
    
    // ========================================================================
    // Step 5: REF to Trial Matching (get initial_seed_point_set1)
    // ========================================================================
    std::cout << "\n--- Step 5: REF to Trial Matching ---\n";
    
    // For this test, we need to load the matching results to get initial_seed_point_set1
    // The matching transforms the seed from REF trial to current trial
    
    cv::Mat refmask_trial;
    SeedPoint initial_seed_point_set1;
    
    int spacing = DICConstants::SUBSET_SPACING;
    bool matching_loaded = false;
    
    // Load matching from ncorr binary file
    if (std::filesystem::exists(base_params.matchingfile)) {
        std::cout << "Loading matching from: " << base_params.matchingfile << "\n";
        
        auto dic_output = ncorr::DIC_analysis_output::load(base_params.matchingfile);
        
        if (!dic_output.disps.empty()) {
            refmask_trial = refmask_REF.clone();
            
            const auto& disp = dic_output.disps[0];
            const auto& u_array = disp.get_u().get_array();
            const auto& v_array = disp.get_v().get_array();
            
            cv::Mat U_mapped(disp.get_u().data_height(), disp.get_u().data_width(), CV_64F);
            cv::Mat V_mapped(disp.get_v().data_height(), disp.get_v().data_width(), CV_64F);
            
            for (size_t y = 0; y < static_cast<size_t>(disp.get_u().data_height()); ++y) {
                for (size_t x = 0; x < static_cast<size_t>(disp.get_u().data_width()); ++x) {
                    U_mapped.at<double>(y, x) = u_array(y, x);
                    V_mapped.at<double>(y, x) = v_array(y, x);
                }
            }
            
            ref_seed_point.sw = ROIManager::mapPixel2Subset(ref_seed_point.pw, spacing);
            initial_seed_point_set1.sw = ROIManager::mapPointCoordinate(ref_seed_point.sw, U_mapped, V_mapped);
            initial_seed_point_set1.pw = ROIManager::mapSubset2Pixel(initial_seed_point_set1.sw, spacing);
            matching_loaded = true;
            std::cout << "Matching loaded\n";
        }
    }
    
    if (!matching_loaded) {
        // No matching - use seed as-is
        refmask_trial = refmask_REF.clone();
        initial_seed_point_set1 = ref_seed_point;
        initial_seed_point_set1.sw = ROIManager::mapPixel2Subset(ref_seed_point.pw, spacing);
        std::cout << "No matching file, using seed as-is\n";
    }
    
    std::cout << "initial_seed_point_set1: pw=[" << initial_seed_point_set1.pw[0] << "," 
              << initial_seed_point_set1.pw[1] << "], sw=[" << initial_seed_point_set1.sw[0] 
              << "," << initial_seed_point_set1.sw[1] << "]\n";
    
    // Compare refmask_trial (use is_mask=true to normalize 0/255 vs 0/1)
    matvar_t* refmask_trial_var = Mat_VarRead(matfp, "refmask_trial");
    if (refmask_trial_var) {
        cv::Mat refmask_trial_matlab = readMatFromVar(refmask_trial_var);
        result.recordTest(compareMat(refmask_trial, refmask_trial_matlab, "refmask_trial", result, 0.0, true), "refmask_trial");
        Mat_VarFree(refmask_trial_var);
    } else {
        result.addError("refmask_trial not found in MAT file");
    }
    
    // Compare initial_seed_point_set1
    matvar_t* seed1_var = Mat_VarRead(matfp, "initial_seed_point_set1");
    if (seed1_var) {
        auto pw1 = readStructFieldArray<int>(seed1_var, "pw");
        auto sw1 = readStructFieldArray<int>(seed1_var, "sw");
        result.recordTest(compareSeedPoint(initial_seed_point_set1, pw1, sw1, 
                                          "initial_seed_point_set1", result), "seed1");
        Mat_VarFree(seed1_var);
    } else {
        result.addError("initial_seed_point_set1 not found in MAT file");
    }
    
    // ========================================================================
    // Step 6: Inter-camera Matching (get initial_seed_point_set2 and refmask_trial_matched)
    // ========================================================================
    std::cout << "\n--- Step 6: Inter-camera Matching ---\n";
    
    std::string ncorr_matching_path = base_params.outputPath + "/ncorr" + 
        std::to_string(cam_1) + std::to_string(cam_2) + ".bin";
    
    cv::Mat refmask_trial_matched;
    SeedPoint initial_seed_point_set2;
    
    if (std::filesystem::exists(ncorr_matching_path)) {
        std::cout << "Loading matching from: " << ncorr_matching_path << "\n";
        
        auto dic_output = ncorr::DIC_analysis_output::load(ncorr_matching_path);
        
        if (!dic_output.disps.empty()) {
            refmask_trial_matched = refmask_trial.clone();
            
            const auto& disp = dic_output.disps[0];
            const auto& u_array = disp.get_u().get_array();
            const auto& v_array = disp.get_v().get_array();
            
            cv::Mat U_mapped(disp.get_u().data_height(), disp.get_u().data_width(), CV_64F);
            cv::Mat V_mapped(disp.get_v().data_height(), disp.get_v().data_width(), CV_64F);
            
            for (size_t y = 0; y < static_cast<size_t>(disp.get_u().data_height()); ++y) {
                for (size_t x = 0; x < static_cast<size_t>(disp.get_u().data_width()); ++x) {
                    U_mapped.at<double>(y, x) = u_array(y, x);
                    V_mapped.at<double>(y, x) = v_array(y, x);
                }
            }
            
            initial_seed_point_set2.sw = ROIManager::mapPointCoordinate(
                initial_seed_point_set1.sw, U_mapped, V_mapped);
            initial_seed_point_set2.pw = ROIManager::mapSubset2Pixel(
                initial_seed_point_set2.sw, spacing);
        }
    } else {
        // No matching file - use seed as-is
        refmask_trial_matched = refmask_trial.clone();
        initial_seed_point_set2 = initial_seed_point_set1;
        std::cout << "No matching file, using seed as-is\n";
    }
    
    std::cout << "initial_seed_point_set2: pw=[" << initial_seed_point_set2.pw[0] << "," 
              << initial_seed_point_set2.pw[1] << "], sw=[" << initial_seed_point_set2.sw[0] 
              << "," << initial_seed_point_set2.sw[1] << "]\n";
    
    // Compare refmask_trial_matched (use is_mask=true to normalize 0/255 vs 0/1)
    matvar_t* refmask_matched_var = Mat_VarRead(matfp, "refmask_trial_matched");
    if (refmask_matched_var) {
        cv::Mat refmask_matched_matlab = readMatFromVar(refmask_matched_var);
        result.recordTest(compareMat(refmask_trial_matched, refmask_matched_matlab, 
                                    "refmask_trial_matched", result, 0.0, true), "refmask_matched");
        Mat_VarFree(refmask_matched_var);
    } else {
        result.addError("refmask_trial_matched not found in MAT file");
    }
    
    // Compare initial_seed_point_set2
    matvar_t* seed2_var = Mat_VarRead(matfp, "initial_seed_point_set2");
    if (seed2_var) {
        auto pw2 = readStructFieldArray<int>(seed2_var, "pw");
        auto sw2 = readStructFieldArray<int>(seed2_var, "sw");
        result.recordTest(compareSeedPoint(initial_seed_point_set2, pw2, sw2, 
                                          "initial_seed_point_set2", result), "seed2");
        Mat_VarFree(seed2_var);
    } else {
        result.addError("initial_seed_point_set2 not found in MAT file");
    }
    
    // ========================================================================
    // Step 7: Image Filtering
    // ========================================================================
    std::cout << "\n--- Step 7: Image Filtering ---\n";
    
    std::vector<int> param_filt = {25, 300};
    auto [filtered_frames, gs_bounds] = ImageProcessor::filterLikeBen(
        satur_frames, refmask_trial, param_filt, nullptr);
    
    cv::Mat cam_first_0 = filtered_frames[0];
    
    std::cout << "Filtering done. GS boundaries: [" << gs_bounds.first << ", " << gs_bounds.second << "]\n";
    
    // Compare cf1 (filtered image)
    matvar_t* cf1_var = Mat_VarRead(matfp, "cf1");
    if (cf1_var) {
        cv::Mat cf1_matlab = readMatFromVar(cf1_var);
        
        // cf1 is float64 normalized to [0,1], so use appropriate tolerance
        // Convert C++ result to same format
        cv::Mat cam_first_double;
        cam_first_0.convertTo(cam_first_double, CV_64F);
        if (cam_first_0.type() == CV_8UC1) {
            cam_first_double /= 255.0;
        }
        
        // Tolerance of 0.1 accounts for ~2 pixel raw input difference propagating through filtering
        result.recordTest(compareMat(cam_first_double, cf1_matlab, "cf1 (filtered)", result, 0.1), "cf1");
        Mat_VarFree(cf1_var);
    } else {
        result.addError("cf1 not found in MAT file");
    }
    
    // Compare gsboundaries
    auto matlab_gs_bounds = readGsBoundaries(matfp);
    
    std::cout << "GS boundaries comparison:\n";
    std::cout << "  C++:    [" << gs_bounds.first << ", " << gs_bounds.second << "]\n";
    std::cout << "  MATLAB: [" << matlab_gs_bounds.first << ", " << matlab_gs_bounds.second << "]\n";
    
    double gs_diff_low = std::abs(gs_bounds.first - matlab_gs_bounds.first);
    double gs_diff_high = std::abs(gs_bounds.second - matlab_gs_bounds.second);
    bool gs_match = (gs_diff_low < 1.0) && (gs_diff_high < 1.0);
    result.recordTest(gs_match, "gsboundaries");
    
    // ========================================================================
    // Summary
    // ========================================================================
    Mat_Close(matfp);
    
    result.print();
    
    return result.passed ? 0 : 1;
}
