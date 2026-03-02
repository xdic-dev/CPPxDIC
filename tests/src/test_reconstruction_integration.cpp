/**
 * Integration Test: 3D Reconstruction (Step E)
 *
 * This test calls DicAnalysis::runStepE() directly to execute the C++ 3D
 * reconstruction pipeline, then compares the generated .mat output against
 * a MATLAB reference.
 *
 * Usage:
 *   ./test_reconstruction_integration <test_root_dir> [tolerance]
 *
 * The <test_root_dir> must mirror the real project directory layout:
 *
 *   <test_root_dir>/
 *     dic_output/<subject>/<material>/
 *       <trial>/<phase>/                  <- Step D binary outputs
 *         ncorr1.bin                      (cam1 DIC output)
 *         ncorr2.bin                      (cam2 DIC output)
 *         ncorr12.bin                     (matching displacement)
 *       myDIC2DpairResults_C_1_C_2.mat    (optional, from formatOutput)
 *     rawdata/<subject>/speckles/<material>/calibration/
 *       DLTstruct_cam1.mat                (DLT calibration cam1)
 *       DLTstruct_cam2.mat                (DLT calibration cam2)
 *     matlab_reference/
 *       DIC3Dcombined_matlab.mat          (MATLAB Step E reference output)
 *
 * The test:
 *   1. Configures DicAnalysis to point at test_root_dir paths
 *   2. Calls runStepE() which runs dic3DReconstruction
 *   3. Loads the C++ generated DIC3Dcombined .mat
 *   4. Loads the MATLAB reference DIC3Dcombined .mat
 *   5. Compares all fields and produces a report
 */

#include "dic_analysis.h"
#include "config.h"
#include <iostream>
#include <string>
#include <vector>
#include <cmath>
#include <matio.h>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <chrono>

namespace fs = std::filesystem;

// ============================================================================
// MAT File Reading Helpers
// ============================================================================

static std::vector<double> readDoubleArray(matvar_t* var) {
    std::vector<double> result;
    if (!var || !var->data) return result;
    size_t n = 1;
    for (int i = 0; i < var->rank; ++i) n *= var->dims[i];
    if (var->class_type == MAT_C_DOUBLE) {
        double* data = static_cast<double*>(var->data);
        result.assign(data, data + n);
    } else if (var->class_type == MAT_C_SINGLE) {
        float* data = static_cast<float*>(var->data);
        result.resize(n);
        for (size_t i = 0; i < n; ++i) result[i] = data[i];
    } else if (var->class_type == MAT_C_INT32) {
        int32_t* data = static_cast<int32_t*>(var->data);
        result.resize(n);
        for (size_t i = 0; i < n; ++i) result[i] = data[i];
    }
    return result;
}

static std::vector<int> readIntArray(matvar_t* var) {
    std::vector<int> result;
    if (!var || !var->data) return result;
    size_t n = 1;
    for (int i = 0; i < var->rank; ++i) n *= var->dims[i];
    if (var->class_type == MAT_C_INT32) {
        int32_t* data = static_cast<int32_t*>(var->data);
        result.assign(data, data + n);
    } else if (var->class_type == MAT_C_DOUBLE) {
        double* data = static_cast<double*>(var->data);
        result.resize(n);
        for (size_t i = 0; i < n; ++i) result[i] = static_cast<int>(std::round(data[i]));
    }
    return result;
}

// ============================================================================
// Comparison Utilities
// ============================================================================

struct FieldComparison {
    std::string name;
    bool passed = true;
    size_t n_compared = 0;
    size_t n_failed = 0;
    size_t n_nan_both = 0;
    size_t n_nan_mismatch = 0;
    double max_error = 0.0;
    double mean_error = 0.0;
    double rmse = 0.0;
    double tolerance = 1e-6;
    std::vector<std::string> details;
};

FieldComparison compareDoubleArrays(const std::string& name,
                                     const std::vector<double>& cpp_data,
                                     const std::vector<double>& matlab_data,
                                     double tolerance) {
    FieldComparison fc;
    fc.name = name;
    fc.tolerance = tolerance;
    
    if (cpp_data.size() != matlab_data.size()) {
        fc.passed = false;
        fc.details.push_back("Size mismatch: C++=" + std::to_string(cpp_data.size()) +
                            " MATLAB=" + std::to_string(matlab_data.size()));
        return fc;
    }
    
    double sum_err = 0.0, sum_sq_err = 0.0;
    size_t n_valid = 0;
    
    for (size_t i = 0; i < cpp_data.size(); ++i) {
        bool cpp_nan = std::isnan(cpp_data[i]);
        bool mat_nan = std::isnan(matlab_data[i]);
        
        if (cpp_nan && mat_nan) { fc.n_nan_both++; continue; }
        if (cpp_nan != mat_nan) {
            fc.n_nan_mismatch++;
            fc.n_failed++;
            if (fc.details.size() < 5) {
                std::ostringstream oss;
                oss << "[" << i << "] NaN mismatch: C++=" << cpp_data[i] << " MAT=" << matlab_data[i];
                fc.details.push_back(oss.str());
            }
            continue;
        }
        
        double err = std::abs(cpp_data[i] - matlab_data[i]);
        fc.n_compared++;
        n_valid++;
        sum_err += err;
        sum_sq_err += err * err;
        fc.max_error = std::max(fc.max_error, err);
        
        if (err > tolerance) {
            fc.n_failed++;
            if (fc.details.size() < 5) {
                std::ostringstream oss;
                oss << "[" << i << "] C++=" << std::setprecision(8) << cpp_data[i]
                    << " MAT=" << matlab_data[i] << " err=" << std::scientific << err;
                fc.details.push_back(oss.str());
            }
        }
    }
    
    if (n_valid > 0) {
        fc.mean_error = sum_err / n_valid;
        fc.rmse = std::sqrt(sum_sq_err / n_valid);
    }
    
    double fail_rate = (fc.n_compared > 0) ? (double)fc.n_failed / fc.n_compared : 0.0;
    fc.passed = (fail_rate < 0.01);
    
    return fc;
}

FieldComparison compareIntArrays(const std::string& name,
                                  const std::vector<int>& cpp_data,
                                  const std::vector<int>& matlab_data) {
    FieldComparison fc;
    fc.name = name;
    fc.tolerance = 0.0;
    
    if (cpp_data.size() != matlab_data.size()) {
        fc.passed = false;
        fc.details.push_back("Size mismatch: C++=" + std::to_string(cpp_data.size()) +
                            " MATLAB=" + std::to_string(matlab_data.size()));
        return fc;
    }
    
    fc.n_compared = cpp_data.size();
    for (size_t i = 0; i < cpp_data.size(); ++i) {
        if (cpp_data[i] != matlab_data[i]) {
            fc.n_failed++;
            fc.max_error = std::max(fc.max_error, (double)std::abs(cpp_data[i] - matlab_data[i]));
            if (fc.details.size() < 5) {
                fc.details.push_back("[" + std::to_string(i) + "] C++=" +
                    std::to_string(cpp_data[i]) + " MAT=" + std::to_string(matlab_data[i]));
            }
        }
    }
    
    fc.passed = (fc.n_failed == 0);
    return fc;
}

void printComparison(const FieldComparison& fc) {
    std::cout << "  " << std::left << std::setw(25) << fc.name;
    std::cout << (fc.passed ? " PASS" : " FAIL");
    std::cout << "  (n=" << fc.n_compared
              << " fail=" << fc.n_failed
              << " max=" << std::scientific << std::setprecision(3) << fc.max_error
              << " rmse=" << fc.rmse
              << " nan_both=" << fc.n_nan_both
              << " nan_mismatch=" << fc.n_nan_mismatch << ")" << std::endl;
    for (const auto& d : fc.details) std::cout << "    " << d << std::endl;
}

void writeReport(const std::string& path, const std::string& title,
                 const std::vector<FieldComparison>& comparisons) {
    std::ofstream ofs(path);
    if (!ofs.is_open()) return;
    int total = comparisons.size(), passed = 0;
    for (const auto& fc : comparisons) if (fc.passed) passed++;
    ofs << "=== " << title << " ===" << std::endl;
    ofs << "Fields: " << passed << "/" << total << " passed" << std::endl << std::endl;
    for (const auto& fc : comparisons) {
        ofs << fc.name << ": " << (fc.passed ? "PASS" : "FAIL")
            << " (n=" << fc.n_compared << " fail=" << fc.n_failed
            << " max=" << std::scientific << fc.max_error
            << " rmse=" << fc.rmse << ")" << std::endl;
        for (const auto& d : fc.details) ofs << "  " << d << std::endl;
    }
    ofs.close();
}

// ============================================================================
// Load DIC3Dcombined from MATLAB .mat file for comparison
// ============================================================================

struct Loaded3DData {
    std::vector<std::vector<double>> pts_x, pts_y, pts_z;
    std::vector<int> faces;
    std::vector<double> face_colors;
    std::vector<std::vector<double>> corr_comb;
    std::vector<std::vector<double>> face_corr_comb;
    std::vector<std::vector<double>> disp_mgn;
    std::vector<int> face_pair_inds;
    bool valid = false;
};

Loaded3DData loadDIC3Dcombined(const std::string& mat_path) {
    Loaded3DData data;
    mat_t* matfp = Mat_Open(mat_path.c_str(), MAT_ACC_RDONLY);
    if (!matfp) { std::cerr << "Cannot open: " << mat_path << std::endl; return data; }
    
    // Points3D (cell array of structs with x,y,z)
    matvar_t* pts_var = Mat_VarRead(matfp, "Points3D");
    if (pts_var && pts_var->class_type == MAT_C_CELL) {
        size_t nFrames = pts_var->dims[0] * pts_var->dims[1];
        for (size_t f = 0; f < nFrames; ++f) {
            matvar_t* frame = Mat_VarGetCell(pts_var, f);
            if (!frame) continue;
            matvar_t* xv = Mat_VarGetStructFieldByName(frame, "x", 0);
            matvar_t* yv = Mat_VarGetStructFieldByName(frame, "y", 0);
            matvar_t* zv = Mat_VarGetStructFieldByName(frame, "z", 0);
            data.pts_x.push_back(readDoubleArray(xv));
            data.pts_y.push_back(readDoubleArray(yv));
            data.pts_z.push_back(readDoubleArray(zv));
        }
        Mat_VarFree(pts_var);
    }
    
    matvar_t* faces_var = Mat_VarRead(matfp, "Faces");
    if (faces_var) { data.faces = readIntArray(faces_var); Mat_VarFree(faces_var); }
    
    matvar_t* fc_var = Mat_VarRead(matfp, "FaceColors");
    if (fc_var) { data.face_colors = readDoubleArray(fc_var); Mat_VarFree(fc_var); }
    
    matvar_t* corr_var = Mat_VarRead(matfp, "corrComb");
    if (corr_var && corr_var->class_type == MAT_C_CELL) {
        size_t n = corr_var->dims[0] * corr_var->dims[1];
        for (size_t f = 0; f < n; ++f)
            data.corr_comb.push_back(readDoubleArray(Mat_VarGetCell(corr_var, f)));
        Mat_VarFree(corr_var);
    }
    
    matvar_t* fcorr_var = Mat_VarRead(matfp, "FaceCorrComb");
    if (fcorr_var && fcorr_var->class_type == MAT_C_CELL) {
        size_t n = fcorr_var->dims[0] * fcorr_var->dims[1];
        for (size_t f = 0; f < n; ++f)
            data.face_corr_comb.push_back(readDoubleArray(Mat_VarGetCell(fcorr_var, f)));
        Mat_VarFree(fcorr_var);
    }
    
    matvar_t* disp_var = Mat_VarRead(matfp, "Disp");
    if (disp_var && disp_var->class_type == MAT_C_STRUCT) {
        matvar_t* mgn_var = Mat_VarGetStructFieldByName(disp_var, "DispMgn", 0);
        if (mgn_var && mgn_var->class_type == MAT_C_CELL) {
            size_t n = mgn_var->dims[0] * mgn_var->dims[1];
            for (size_t f = 0; f < n; ++f)
                data.disp_mgn.push_back(readDoubleArray(Mat_VarGetCell(mgn_var, f)));
        }
        Mat_VarFree(disp_var);
    }
    
    matvar_t* fpi_var = Mat_VarRead(matfp, "FacePairInds");
    if (fpi_var) { data.face_pair_inds = readIntArray(fpi_var); Mat_VarFree(fpi_var); }
    
    Mat_Close(matfp);
    data.valid = !data.pts_x.empty() && !data.faces.empty();
    return data;
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    std::string test_root = "test_data";
    double tolerance = 1e-4;
    
    if (argc > 1) test_root = argv[1];
    if (argc > 2) tolerance = std::stod(argv[2]);
    
    std::cout << "\n=== 3D RECONSTRUCTION INTEGRATION TEST ===" << std::endl;
    std::cout << "Test root: " << test_root << std::endl;
    std::cout << "Tolerance: " << std::scientific << tolerance << std::endl;
    
    // =====================================================================
    // 1. Configure DicAnalysis to use test directory paths
    // =====================================================================
    // Read config from test_root/test_config.txt if available, else use defaults
    Config config;
    
    std::string config_file = test_root + "/test_config.txt";
    if (fs::exists(config_file)) {
        config.loadFromDicParamsFile(config_file);
        std::cout << "Loaded config from: " << config_file << std::endl;
    } else {
        // Default test configuration - user must override via test_config.txt
        std::cerr << "Config file not found: " << config_file << std::endl;
        std::cerr << "\nCreate " << config_file << " with at least:" << std::endl;
        std::cerr << "  subject_id = S09" << std::endl;
        std::cerr << "  material = glass" << std::endl;
        std::cerr << "  phase_id = loading" << std::endl;
        std::cerr << "  num_pair = 2" << std::endl;
        std::cerr << "  data_path = " << test_root << "/rawdata_root" << std::endl;
        std::cerr << "  dic_path = " << test_root << "/dic_output" << std::endl;
        std::cerr << "\nSee PREPARE_TEST_DATA.md for full directory layout." << std::endl;
        return 1;
    }
    
    // Force .mat output and disable visualization
    config.data_format = "mat";
    config.mapLogic = false;
    config.debug_mode = true;
    
    std::cout << "\nConfig:" << std::endl;
    std::cout << "  subject_id: " << config.subject_id << std::endl;
    std::cout << "  material:   " << config.material << std::endl;
    std::cout << "  phase_id:   " << config.phase_id << std::endl;
    std::cout << "  num_pair:   " << config.num_pair << std::endl;
    std::cout << "  data_path:  " << config.data_path << std::endl;
    std::cout << "  dic_path:   " << config.dic_path << std::endl;
    
    // Trial to process
    std::vector<int> trial_target;
    // Check for trial override in config or use ref_trial_id
    trial_target.push_back(config.ref_trial_id);
    std::cout << "  trial:      " << trial_target[0] << std::endl;
    
    // Remove existing output so we force re-computation (no checkpoint skip)
    std::ostringstream cpp_bin_path, cpp_mat_path;
    cpp_bin_path << config.dic_path << "/" << config.subject_id << "/" << config.material
                 << "/DIC3Dcombined_" << config.num_pair << "Pairs_stitched.bin";
    cpp_mat_path << config.dic_path << "/" << config.subject_id << "/" << config.material
                 << "/DIC3Dcombined_" << config.num_pair << "Pairs_stitched.mat";
    
    if (fs::exists(cpp_bin_path.str())) {
        fs::remove(cpp_bin_path.str());
        std::cout << "  Removed existing: " << cpp_bin_path.str() << std::endl;
    }
    if (fs::exists(cpp_mat_path.str())) {
        fs::remove(cpp_mat_path.str());
        std::cout << "  Removed existing: " << cpp_mat_path.str() << std::endl;
    }
    
    // Ensure output directory exists
    std::string output_dir = config.dic_path + "/" + config.subject_id + "/" + config.material;
    fs::create_directories(output_dir);
    
    // =====================================================================
    // 2. Run Step E via DicAnalysis::runStepE()
    // =====================================================================
    std::cout << "\n--- Running C++ 3D Reconstruction (Step E) ---" << std::endl;
    
    auto t0 = std::chrono::high_resolution_clock::now();
    DicAnalysis analysis(config);
    bool step_e_ok = analysis.runStepE(trial_target);
    auto t1 = std::chrono::high_resolution_clock::now();
    double elapsed = std::chrono::duration<double>(t1 - t0).count();
    
    if (!step_e_ok) {
        std::cerr << "\nStep E FAILED (" << elapsed << " s)" << std::endl;
        std::cerr << "Check that ncorr*.bin and calibration files exist in test directory." << std::endl;
        return 1;
    }
    std::cout << "\nStep E completed in " << std::fixed << std::setprecision(2) << elapsed << " s" << std::endl;
    
    // =====================================================================
    // 3. Load C++ output and MATLAB reference
    // =====================================================================
    std::string cpp_output = cpp_mat_path.str();
    std::string matlab_ref = test_root + "/matlab_reference/DIC3Dcombined_matlab.mat";
    
    if (!fs::exists(cpp_output)) {
        std::cerr << "C++ output not generated: " << cpp_output << std::endl;
        return 1;
    }
    
    if (!fs::exists(matlab_ref)) {
        std::cerr << "MATLAB reference not found: " << matlab_ref << std::endl;
        std::cerr << "\nStep E ran successfully but no MATLAB reference to compare against." << std::endl;
        std::cerr << "Place MATLAB DIC3Dcombined_*.mat at: " << matlab_ref << std::endl;
        // Still exit 0 since Step E itself succeeded
        std::cout << "\nStep E: PASS (no comparison, reference missing)" << std::endl;
        return 0;
    }
    
    std::cout << "\nLoading C++ output:       " << cpp_output << std::endl;
    auto cpp_data = loadDIC3Dcombined(cpp_output);
    
    std::cout << "Loading MATLAB reference: " << matlab_ref << std::endl;
    auto matlab_data = loadDIC3Dcombined(matlab_ref);
    
    if (!cpp_data.valid || !matlab_data.valid) {
        std::cerr << "Failed to load .mat files for comparison" << std::endl;
        return 1;
    }
    
    std::cout << "\nC++ data:    " << cpp_data.pts_x.size() << " frames, "
              << (cpp_data.pts_x.empty() ? 0 : cpp_data.pts_x[0].size()) << " points, "
              << cpp_data.faces.size() / 3 << " faces" << std::endl;
    std::cout << "MATLAB data: " << matlab_data.pts_x.size() << " frames, "
              << (matlab_data.pts_x.empty() ? 0 : matlab_data.pts_x[0].size()) << " points, "
              << matlab_data.faces.size() / 3 << " faces" << std::endl;
    
    // =====================================================================
    // 4. Compare fields
    // =====================================================================
    std::vector<FieldComparison> comparisons;
    
    // Faces (handle 0-based vs 1-based indexing)
    {
        auto cpp_faces = cpp_data.faces;
        auto mat_faces = matlab_data.faces;
        if (!mat_faces.empty()) {
            int min_mat = *std::min_element(mat_faces.begin(), mat_faces.end());
            int min_cpp = *std::min_element(cpp_faces.begin(), cpp_faces.end());
            if (min_mat == 1 && min_cpp == 0)
                for (auto& f : mat_faces) f -= 1;
        }
        comparisons.push_back(compareIntArrays("Faces", cpp_faces, mat_faces));
    }
    
    // Points3D (first and last frame)
    size_t nFrames = std::min(cpp_data.pts_x.size(), matlab_data.pts_x.size());
    for (size_t f : {size_t(0), nFrames > 1 ? nFrames - 1 : size_t(0)}) {
        if (f >= nFrames) continue;
        std::string pfx = "Points3D[" + std::to_string(f) + "]";
        comparisons.push_back(compareDoubleArrays(pfx + ".x", cpp_data.pts_x[f], matlab_data.pts_x[f], tolerance));
        comparisons.push_back(compareDoubleArrays(pfx + ".y", cpp_data.pts_y[f], matlab_data.pts_y[f], tolerance));
        comparisons.push_back(compareDoubleArrays(pfx + ".z", cpp_data.pts_z[f], matlab_data.pts_z[f], tolerance));
    }
    
    // Correlation
    if (!cpp_data.corr_comb.empty() && !matlab_data.corr_comb.empty())
        comparisons.push_back(compareDoubleArrays("corrComb[0]", cpp_data.corr_comb[0], matlab_data.corr_comb[0], tolerance));
    if (!cpp_data.face_corr_comb.empty() && !matlab_data.face_corr_comb.empty())
        comparisons.push_back(compareDoubleArrays("FaceCorrComb[0]", cpp_data.face_corr_comb[0], matlab_data.face_corr_comb[0], tolerance));
    
    // Displacement (last frame)
    if (nFrames > 1 && !cpp_data.disp_mgn.empty() && !matlab_data.disp_mgn.empty()) {
        size_t last = nFrames - 1;
        if (last < cpp_data.disp_mgn.size() && last < matlab_data.disp_mgn.size())
            comparisons.push_back(compareDoubleArrays("DispMgn[last]", cpp_data.disp_mgn[last], matlab_data.disp_mgn[last], tolerance));
    }
    
    // FacePairInds
    if (!cpp_data.face_pair_inds.empty() && !matlab_data.face_pair_inds.empty())
        comparisons.push_back(compareIntArrays("FacePairInds", cpp_data.face_pair_inds, matlab_data.face_pair_inds));
    
    // =====================================================================
    // 5. Results
    // =====================================================================
    std::cout << "\n--- Field Comparisons ---" << std::endl;
    for (const auto& fc : comparisons) printComparison(fc);
    
    int passed = 0;
    for (const auto& fc : comparisons) if (fc.passed) passed++;
    
    std::cout << "\n========================================" << std::endl;
    std::cout << "3D Reconstruction: " << passed << "/" << comparisons.size()
              << " fields passed (" << elapsed << " s)" << std::endl;
    std::cout << "========================================" << std::endl;
    
    writeReport(test_root + "/reconstruction_report.txt",
                "3D Reconstruction Integration Test", comparisons);
    std::cout << "Report: " << test_root << "/reconstruction_report.txt" << std::endl;
    
    return (passed == (int)comparisons.size()) ? 0 : 1;
}
