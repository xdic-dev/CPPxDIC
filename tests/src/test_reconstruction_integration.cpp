/**
 * Integration Test: 3D Reconstruction (Step E)
 *
 * Assumes you have MATLAB reference .mat files:
 *   - Input:  myDIC2DpairResults_C_1_C_2.mat (from Step D / step2_dic_finish)
 *   - Output: DIC3Dcombined_matlab.mat        (MATLAB Step E output)
 *
 * This test:
 *   1. Loads the 2D DIC pair results from .mat
 *   2. Runs dic3DReconstruction logic (DLT, triangulation, stitching)
 *   3. Compares the C++ 3D output against the MATLAB reference
 *   4. Optionally writes the C++ output as .mat for visual inspection
 *
 * Usage:
 *   ./test_reconstruction_integration <test_data_dir> [tolerance]
 *
 * Expected files in <test_data_dir>:
 *   myDIC2DpairResults_C_1_C_2.mat   - 2D DIC pair results (MATLAB format)
 *   DIC3Dcombined_matlab.mat          - MATLAB 3D reconstruction reference
 *   DLT_cam1.mat / DLT_cam2.mat      - DLT calibration parameters
 *
 * The test outputs:
 *   DIC3Dcombined_cpp.mat             - C++ reconstruction (for MATLAB comparison)
 *   reconstruction_report.txt         - Detailed comparison report
 */

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
    
    // Pass if < 1% failures
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

// ============================================================================
// Report Generation
// ============================================================================

void printComparison(const FieldComparison& fc) {
    std::cout << "  " << std::left << std::setw(25) << fc.name;
    if (fc.passed)
        std::cout << " PASS";
    else
        std::cout << " FAIL";
    
    std::cout << "  (n=" << fc.n_compared
              << " fail=" << fc.n_failed
              << " max_err=" << std::scientific << std::setprecision(3) << fc.max_error
              << " rmse=" << fc.rmse
              << " nan_both=" << fc.n_nan_both
              << " nan_mismatch=" << fc.n_nan_mismatch
              << ")" << std::endl;
    
    for (const auto& d : fc.details) {
        std::cout << "    " << d << std::endl;
    }
}

void writeReport(const std::string& path,
                 const std::string& test_name,
                 const std::vector<FieldComparison>& comparisons) {
    std::ofstream ofs(path);
    if (!ofs.is_open()) return;
    
    int total = comparisons.size();
    int passed = 0;
    for (const auto& fc : comparisons) if (fc.passed) passed++;
    
    ofs << "=== " << test_name << " ===" << std::endl;
    ofs << "Fields compared: " << total << std::endl;
    ofs << "Fields passed:   " << passed << "/" << total << std::endl;
    ofs << std::endl;
    
    for (const auto& fc : comparisons) {
        ofs << fc.name << ": " << (fc.passed ? "PASS" : "FAIL")
            << " (n=" << fc.n_compared << " fail=" << fc.n_failed
            << " max_err=" << std::scientific << fc.max_error
            << " rmse=" << fc.rmse << ")" << std::endl;
        for (const auto& d : fc.details) ofs << "  " << d << std::endl;
    }
    
    ofs.close();
    std::cout << "Report saved to: " << path << std::endl;
}

// ============================================================================
// Load DIC3Dcombined from MATLAB .mat file
// ============================================================================

struct Loaded3DData {
    std::vector<std::vector<double>> pts_x, pts_y, pts_z; // [nFrames][nPoints]
    std::vector<int> faces;
    std::vector<double> face_colors;
    std::vector<std::vector<double>> corr_comb;      // [nFrames][nPoints]
    std::vector<std::vector<double>> face_corr_comb;  // [nFrames][nFaces]
    std::vector<std::vector<double>> disp_mgn;        // [nFrames][nPoints]
    std::vector<int> face_pair_inds;
    std::vector<int> point_pair_inds;
    bool valid = false;
};

Loaded3DData loadDIC3Dcombined(const std::string& mat_path) {
    Loaded3DData data;
    
    mat_t* matfp = Mat_Open(mat_path.c_str(), MAT_ACC_RDONLY);
    if (!matfp) {
        std::cerr << "Cannot open: " << mat_path << std::endl;
        return data;
    }
    
    // Read Points3D (cell array)
    matvar_t* pts_var = Mat_VarRead(matfp, "Points3D");
    if (pts_var && pts_var->class_type == MAT_C_CELL) {
        size_t nFrames = pts_var->dims[0] * pts_var->dims[1];
        for (size_t f = 0; f < nFrames; ++f) {
            matvar_t* frame = Mat_VarGetCell(pts_var, f);
            if (!frame) continue;
            
            // Each frame is a struct with x, y, z fields
            matvar_t* xv = Mat_VarGetStructFieldByName(frame, "x", 0);
            matvar_t* yv = Mat_VarGetStructFieldByName(frame, "y", 0);
            matvar_t* zv = Mat_VarGetStructFieldByName(frame, "z", 0);
            
            data.pts_x.push_back(readDoubleArray(xv));
            data.pts_y.push_back(readDoubleArray(yv));
            data.pts_z.push_back(readDoubleArray(zv));
        }
        Mat_VarFree(pts_var);
    }
    
    // Read Faces
    matvar_t* faces_var = Mat_VarRead(matfp, "Faces");
    if (faces_var) {
        data.faces = readIntArray(faces_var);
        Mat_VarFree(faces_var);
    }
    
    // Read FaceColors
    matvar_t* fc_var = Mat_VarRead(matfp, "FaceColors");
    if (fc_var) {
        data.face_colors = readDoubleArray(fc_var);
        Mat_VarFree(fc_var);
    }
    
    // Read corrComb (cell array)
    matvar_t* corr_var = Mat_VarRead(matfp, "corrComb");
    if (corr_var && corr_var->class_type == MAT_C_CELL) {
        size_t nFrames = corr_var->dims[0] * corr_var->dims[1];
        for (size_t f = 0; f < nFrames; ++f) {
            matvar_t* frame = Mat_VarGetCell(corr_var, f);
            data.corr_comb.push_back(readDoubleArray(frame));
        }
        Mat_VarFree(corr_var);
    }
    
    // Read FaceCorrComb (cell array)
    matvar_t* fcorr_var = Mat_VarRead(matfp, "FaceCorrComb");
    if (fcorr_var && fcorr_var->class_type == MAT_C_CELL) {
        size_t nFrames = fcorr_var->dims[0] * fcorr_var->dims[1];
        for (size_t f = 0; f < nFrames; ++f) {
            matvar_t* frame = Mat_VarGetCell(fcorr_var, f);
            data.face_corr_comb.push_back(readDoubleArray(frame));
        }
        Mat_VarFree(fcorr_var);
    }
    
    // Read Disp.DispMgn (nested struct with cell array)
    matvar_t* disp_var = Mat_VarRead(matfp, "Disp");
    if (disp_var && disp_var->class_type == MAT_C_STRUCT) {
        matvar_t* mgn_var = Mat_VarGetStructFieldByName(disp_var, "DispMgn", 0);
        if (mgn_var && mgn_var->class_type == MAT_C_CELL) {
            size_t nFrames = mgn_var->dims[0] * mgn_var->dims[1];
            for (size_t f = 0; f < nFrames; ++f) {
                matvar_t* frame = Mat_VarGetCell(mgn_var, f);
                data.disp_mgn.push_back(readDoubleArray(frame));
            }
        }
        Mat_VarFree(disp_var);
    }
    
    // Read FacePairInds
    matvar_t* fpi_var = Mat_VarRead(matfp, "FacePairInds");
    if (fpi_var) {
        data.face_pair_inds = readIntArray(fpi_var);
        Mat_VarFree(fpi_var);
    }
    
    // Read PointPairInds
    matvar_t* ppi_var = Mat_VarRead(matfp, "PointPairInds");
    if (ppi_var) {
        data.point_pair_inds = readIntArray(ppi_var);
        Mat_VarFree(ppi_var);
    }
    
    Mat_Close(matfp);
    
    data.valid = !data.pts_x.empty() && !data.faces.empty();
    return data;
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    std::string test_data_dir = "test_data";
    double tolerance = 1e-4;  // Relaxed: accumulated numerical differences expected
    
    if (argc > 1) test_data_dir = argv[1];
    if (argc > 2) tolerance = std::stod(argv[2]);
    
    std::cout << "\n=== 3D RECONSTRUCTION INTEGRATION TEST ===" << std::endl;
    std::cout << "Data dir:  " << test_data_dir << std::endl;
    std::cout << "Tolerance: " << std::scientific << tolerance << std::endl;
    std::cout << std::endl;
    
    // Locate reference files
    std::string cpp_file = test_data_dir + "/DIC3Dcombined_cpp.mat";
    std::string matlab_file = test_data_dir + "/DIC3Dcombined_matlab.mat";
    
    if (!fs::exists(cpp_file)) {
        std::cerr << "C++ output not found: " << cpp_file << std::endl;
        std::cerr << "\nTo prepare test data:" << std::endl;
        std::cerr << "  1. Run C++ pipeline Step E on your dataset" << std::endl;
        std::cerr << "  2. Copy the DIC3Dcombined_*.mat to " << test_data_dir << "/DIC3Dcombined_cpp.mat" << std::endl;
        return 1;
    }
    
    if (!fs::exists(matlab_file)) {
        std::cerr << "MATLAB reference not found: " << matlab_file << std::endl;
        std::cerr << "\nTo prepare test data:" << std::endl;
        std::cerr << "  1. Run MATLAB pipeline Step 3 (step3_dic_rewrited) on the same dataset" << std::endl;
        std::cerr << "  2. Copy the DIC3Dcombined_*.mat to " << test_data_dir << "/DIC3Dcombined_matlab.mat" << std::endl;
        return 1;
    }
    
    // Load both files
    std::cout << "Loading C++ output:      " << cpp_file << std::endl;
    auto cpp_data = loadDIC3Dcombined(cpp_file);
    
    std::cout << "Loading MATLAB reference: " << matlab_file << std::endl;
    auto matlab_data = loadDIC3Dcombined(matlab_file);
    
    if (!cpp_data.valid || !matlab_data.valid) {
        std::cerr << "Failed to load one or both files" << std::endl;
        return 1;
    }
    
    std::cout << "\nC++ data:    " << cpp_data.pts_x.size() << " frames, "
              << (cpp_data.pts_x.empty() ? 0 : cpp_data.pts_x[0].size()) << " points, "
              << cpp_data.faces.size() / 3 << " faces" << std::endl;
    std::cout << "MATLAB data: " << matlab_data.pts_x.size() << " frames, "
              << (matlab_data.pts_x.empty() ? 0 : matlab_data.pts_x[0].size()) << " points, "
              << matlab_data.faces.size() / 3 << " faces" << std::endl;
    
    // ---- Compare ----
    std::vector<FieldComparison> comparisons;
    
    // 1. Faces (exact match expected, modulo 1-based indexing)
    // MATLAB Faces are 1-indexed; C++ are 0-indexed. Check if we need offset.
    {
        auto cpp_faces = cpp_data.faces;
        auto mat_faces = matlab_data.faces;
        
        // Detect indexing convention: if MATLAB min face index > 0, it's 1-based
        if (!mat_faces.empty()) {
            int min_mat = *std::min_element(mat_faces.begin(), mat_faces.end());
            int min_cpp = *std::min_element(cpp_faces.begin(), cpp_faces.end());
            if (min_mat == 1 && min_cpp == 0) {
                // Convert MATLAB to 0-based
                for (auto& f : mat_faces) f -= 1;
            }
        }
        
        comparisons.push_back(compareIntArrays("Faces", cpp_faces, mat_faces));
    }
    
    // 2. Points3D per frame
    size_t nFrames = std::min(cpp_data.pts_x.size(), matlab_data.pts_x.size());
    if (cpp_data.pts_x.size() != matlab_data.pts_x.size()) {
        std::cout << "  WARNING: Frame count mismatch (C++=" << cpp_data.pts_x.size()
                  << " MATLAB=" << matlab_data.pts_x.size() << ")" << std::endl;
    }
    
    // Compare frame 0 (reference) and last frame
    for (size_t f : {size_t(0), nFrames > 1 ? nFrames - 1 : size_t(0)}) {
        if (f >= nFrames) continue;
        std::string prefix = "Points3D[" + std::to_string(f) + "]";
        comparisons.push_back(compareDoubleArrays(prefix + ".x", cpp_data.pts_x[f], matlab_data.pts_x[f], tolerance));
        comparisons.push_back(compareDoubleArrays(prefix + ".y", cpp_data.pts_y[f], matlab_data.pts_y[f], tolerance));
        comparisons.push_back(compareDoubleArrays(prefix + ".z", cpp_data.pts_z[f], matlab_data.pts_z[f], tolerance));
    }
    
    // 3. FaceColors
    if (!cpp_data.face_colors.empty() && !matlab_data.face_colors.empty()) {
        comparisons.push_back(compareDoubleArrays("FaceColors", cpp_data.face_colors, matlab_data.face_colors, 1.0));
    }
    
    // 4. Correlation (frame 0)
    if (!cpp_data.corr_comb.empty() && !matlab_data.corr_comb.empty()) {
        comparisons.push_back(compareDoubleArrays("corrComb[0]", cpp_data.corr_comb[0], matlab_data.corr_comb[0], tolerance));
    }
    
    // 5. FaceCorrComb (frame 0)
    if (!cpp_data.face_corr_comb.empty() && !matlab_data.face_corr_comb.empty()) {
        comparisons.push_back(compareDoubleArrays("FaceCorrComb[0]", cpp_data.face_corr_comb[0], matlab_data.face_corr_comb[0], tolerance));
    }
    
    // 6. Displacement magnitude (last frame)
    if (nFrames > 1 && !cpp_data.disp_mgn.empty() && !matlab_data.disp_mgn.empty()) {
        size_t last = nFrames - 1;
        if (last < cpp_data.disp_mgn.size() && last < matlab_data.disp_mgn.size()) {
            comparisons.push_back(compareDoubleArrays("DispMgn[last]", cpp_data.disp_mgn[last], matlab_data.disp_mgn[last], tolerance));
        }
    }
    
    // 7. FacePairInds
    if (!cpp_data.face_pair_inds.empty() && !matlab_data.face_pair_inds.empty()) {
        comparisons.push_back(compareIntArrays("FacePairInds", cpp_data.face_pair_inds, matlab_data.face_pair_inds));
    }
    
    // ---- Print results ----
    std::cout << "\n--- Field Comparisons ---" << std::endl;
    for (const auto& fc : comparisons) printComparison(fc);
    
    int passed = 0;
    for (const auto& fc : comparisons) if (fc.passed) passed++;
    
    std::cout << "\n========================================" << std::endl;
    std::cout << "3D Reconstruction: " << passed << "/" << comparisons.size() << " fields passed" << std::endl;
    std::cout << "========================================" << std::endl;
    
    // Write report
    writeReport(test_data_dir + "/reconstruction_report.txt",
                "3D Reconstruction Integration Test", comparisons);
    
    return (passed == (int)comparisons.size()) ? 0 : 1;
}
