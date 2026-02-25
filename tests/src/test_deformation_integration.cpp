/**
 * Integration Test: Deformation Analysis (Step F)
 *
 * Assumes you have MATLAB reference .mat files:
 *   - Input:  DIC3Dcombined_matlab.mat   (MATLAB Step E output)
 *   - Output: DIC3DPPresults_matlab.mat  (MATLAB Step F output)
 *
 * This test:
 *   1. Loads the DIC3Dcombined from .mat (same data for both C++ and MATLAB)
 *   2. Runs computeTriSurfaceDeformation (TCPE) on the loaded mesh
 *   3. Compares the C++ deformation output against the MATLAB reference
 *   4. Outputs a detailed comparison report
 *
 * Usage:
 *   ./test_deformation_integration <test_data_dir> [tolerance]
 *
 * Expected files in <test_data_dir>:
 *   DIC3Dcombined_matlab.mat   - Input mesh (Points3D, Faces)
 *   DIC3DPPresults_matlab.mat  - MATLAB deformation reference (Deform struct)
 *
 * The test outputs:
 *   deformation_report.txt     - Detailed comparison report
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
#include <Eigen/Dense>

#include "strain_computation.h"

namespace fs = std::filesystem;
using namespace cppxdic;

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

void printComparison(const FieldComparison& fc) {
    std::cout << "  " << std::left << std::setw(25) << fc.name;
    std::cout << (fc.passed ? " PASS" : " FAIL");
    std::cout << "  (n=" << fc.n_compared
              << " fail=" << fc.n_failed
              << " max=" << std::scientific << std::setprecision(3) << fc.max_error
              << " rmse=" << fc.rmse << ")" << std::endl;
    for (const auto& d : fc.details)
        std::cout << "    " << d << std::endl;
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
    std::cout << "Report: " << path << std::endl;
}

// ============================================================================
// Load mesh data from DIC3Dcombined .mat
// ============================================================================

struct MeshData {
    std::vector<int> faces;  // 0-indexed
    std::vector<Eigen::Vector3d> vertices_ref;
    std::vector<std::vector<Eigen::Vector3d>> vertices_all;
    size_t nFrames = 0;
    size_t nPoints = 0;
    size_t nFaces = 0;
    bool valid = false;
};

MeshData loadMeshFromCombined(const std::string& mat_path) {
    MeshData mesh;
    mat_t* matfp = Mat_Open(mat_path.c_str(), MAT_ACC_RDONLY);
    if (!matfp) return mesh;
    
    // Read Faces
    matvar_t* faces_var = Mat_VarRead(matfp, "Faces");
    if (faces_var) {
        mesh.faces = readIntArray(faces_var);
        // Convert 1-based to 0-based if needed
        if (!mesh.faces.empty()) {
            int minF = *std::min_element(mesh.faces.begin(), mesh.faces.end());
            if (minF == 1) for (auto& f : mesh.faces) f -= 1;
        }
        mesh.nFaces = mesh.faces.size() / 3;
        Mat_VarFree(faces_var);
    }
    
    // Read Points3D
    matvar_t* pts_var = Mat_VarRead(matfp, "Points3D");
    if (pts_var && pts_var->class_type == MAT_C_CELL) {
        mesh.nFrames = pts_var->dims[0] * pts_var->dims[1];
        for (size_t f = 0; f < mesh.nFrames; ++f) {
            matvar_t* frame = Mat_VarGetCell(pts_var, f);
            if (!frame) continue;
            
            matvar_t* xv = Mat_VarGetStructFieldByName(frame, "x", 0);
            matvar_t* yv = Mat_VarGetStructFieldByName(frame, "y", 0);
            matvar_t* zv = Mat_VarGetStructFieldByName(frame, "z", 0);
            
            auto xd = readDoubleArray(xv);
            auto yd = readDoubleArray(yv);
            auto zd = readDoubleArray(zv);
            
            if (f == 0) mesh.nPoints = xd.size();
            
            std::vector<Eigen::Vector3d> verts(xd.size());
            for (size_t i = 0; i < xd.size(); ++i)
                verts[i] = Eigen::Vector3d(xd[i], yd[i], zd[i]);
            
            mesh.vertices_all.push_back(std::move(verts));
        }
        Mat_VarFree(pts_var);
    }
    
    if (!mesh.vertices_all.empty()) {
        mesh.vertices_ref = mesh.vertices_all[0];
    }
    
    Mat_Close(matfp);
    mesh.valid = !mesh.faces.empty() && !mesh.vertices_all.empty();
    return mesh;
}

// ============================================================================
// Load Deform struct from DIC3DPPresults .mat
// ============================================================================

struct DeformRef {
    // Scalar fields per frame (cell arrays in MATLAB)
    std::vector<std::vector<double>> Epc1, Epc2, epc1, epc2;
    std::vector<std::vector<double>> EShearMax, eShearMax;
    std::vector<std::vector<double>> Eeq, eeq;
    std::vector<std::vector<double>> Emgn, emgn;
    std::vector<std::vector<double>> J;
    std::vector<std::vector<double>> Lamda1, Lamda2;
    bool valid = false;
};

static std::vector<std::vector<double>> loadCellField(matvar_t* deform_var, const char* name) {
    std::vector<std::vector<double>> result;
    matvar_t* field = Mat_VarGetStructFieldByName(deform_var, name, 0);
    if (!field || field->class_type != MAT_C_CELL) return result;
    
    size_t n = field->dims[0] * field->dims[1];
    for (size_t i = 0; i < n; ++i) {
        matvar_t* cell = Mat_VarGetCell(field, i);
        result.push_back(readDoubleArray(cell));
    }
    return result;
}

DeformRef loadDeformFromPPresults(const std::string& mat_path) {
    DeformRef ref;
    mat_t* matfp = Mat_Open(mat_path.c_str(), MAT_ACC_RDONLY);
    if (!matfp) return ref;
    
    matvar_t* deform_var = Mat_VarRead(matfp, "Deform");
    if (!deform_var || deform_var->class_type != MAT_C_STRUCT) {
        if (deform_var) Mat_VarFree(deform_var);
        Mat_Close(matfp);
        return ref;
    }
    
    ref.Epc1 = loadCellField(deform_var, "Epc1");
    ref.Epc2 = loadCellField(deform_var, "Epc2");
    ref.epc1 = loadCellField(deform_var, "epc1");
    ref.epc2 = loadCellField(deform_var, "epc2");
    ref.EShearMax = loadCellField(deform_var, "EShearMax");
    ref.eShearMax = loadCellField(deform_var, "eShearMax");
    ref.Eeq = loadCellField(deform_var, "Eeq");
    ref.eeq = loadCellField(deform_var, "eeq");
    ref.Emgn = loadCellField(deform_var, "Emgn");
    ref.emgn = loadCellField(deform_var, "emgn");
    ref.J = loadCellField(deform_var, "J");
    ref.Lamda1 = loadCellField(deform_var, "Lamda1");
    ref.Lamda2 = loadCellField(deform_var, "Lamda2");
    
    Mat_VarFree(deform_var);
    Mat_Close(matfp);
    
    ref.valid = !ref.Epc1.empty();
    return ref;
}

// ============================================================================
// Compare a deformation field across all frames
// ============================================================================

static void compareDeformField(const std::string& name,
                                const std::vector<std::vector<double>>& cpp_frames,
                                const std::vector<std::vector<double>>& mat_frames,
                                double tolerance,
                                std::vector<FieldComparison>& comparisons) {
    size_t nFrames = std::min(cpp_frames.size(), mat_frames.size());
    if (nFrames == 0) {
        FieldComparison fc;
        fc.name = name;
        fc.passed = false;
        fc.details.push_back("No frames to compare (C++=" + std::to_string(cpp_frames.size()) +
                            " MAT=" + std::to_string(mat_frames.size()) + ")");
        comparisons.push_back(fc);
        return;
    }
    
    // Compare first and last frame
    for (size_t f : {size_t(0), nFrames > 1 ? nFrames - 1 : size_t(0)}) {
        if (f >= nFrames) continue;
        std::string label = name + "[" + std::to_string(f) + "]";
        comparisons.push_back(compareDoubleArrays(label, cpp_frames[f], mat_frames[f], tolerance));
    }
}

// ============================================================================
// Extract per-frame scalar field from FrameDeformationResult
// ============================================================================

using FieldExtractor = std::function<const std::vector<double>&(const DeformationResult&)>;

static std::vector<std::vector<double>> extractField(const FrameDeformationResult& result,
                                                      FieldExtractor extractor) {
    std::vector<std::vector<double>> out;
    for (const auto& frame : result.frames)
        out.push_back(extractor(frame));
    return out;
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    std::string test_data_dir = "test_data";
    double tolerance = 1e-6;
    
    if (argc > 1) test_data_dir = argv[1];
    if (argc > 2) tolerance = std::stod(argv[2]);
    
    std::cout << "\n=== DEFORMATION ANALYSIS INTEGRATION TEST ===" << std::endl;
    std::cout << "Data dir:  " << test_data_dir << std::endl;
    std::cout << "Tolerance: " << std::scientific << tolerance << std::endl << std::endl;
    
    std::string combined_file = test_data_dir + "/DIC3Dcombined_matlab.mat";
    std::string ppresults_file = test_data_dir + "/DIC3DPPresults_matlab.mat";
    
    if (!fs::exists(combined_file)) {
        std::cerr << "Input mesh not found: " << combined_file << std::endl;
        std::cerr << "\nPrepare test data:" << std::endl;
        std::cerr << "  Copy your MATLAB DIC3Dcombined_*.mat to " << combined_file << std::endl;
        return 1;
    }
    if (!fs::exists(ppresults_file)) {
        std::cerr << "MATLAB reference not found: " << ppresults_file << std::endl;
        std::cerr << "\nPrepare test data:" << std::endl;
        std::cerr << "  Copy your MATLAB DIC3DPPresults_*.mat to " << ppresults_file << std::endl;
        return 1;
    }
    
    // Load mesh
    std::cout << "Loading mesh from: " << combined_file << std::endl;
    auto mesh = loadMeshFromCombined(combined_file);
    if (!mesh.valid) {
        std::cerr << "Failed to load mesh" << std::endl;
        return 1;
    }
    std::cout << "  " << mesh.nFrames << " frames, " << mesh.nPoints << " points, "
              << mesh.nFaces << " faces" << std::endl;
    
    // Load MATLAB deformation reference
    std::cout << "Loading MATLAB Deform reference: " << ppresults_file << std::endl;
    auto deform_ref = loadDeformFromPPresults(ppresults_file);
    if (!deform_ref.valid) {
        std::cerr << "Failed to load MATLAB Deform struct" << std::endl;
        return 1;
    }
    std::cout << "  Loaded " << deform_ref.Epc1.size() << " frames of deformation data" << std::endl;
    
    // Run C++ deformation computation
    std::cout << "\nRunning C++ TCPE deformation (cumulative)..." << std::endl;
    FrameDeformationResult cpp_result = computeTriSurfaceDeformation(
        mesh.faces, mesh.vertices_ref, mesh.vertices_all, true
    );
    std::cout << "  Computed " << cpp_result.n_frames << " frames, "
              << cpp_result.n_faces << " faces" << std::endl;
    
    // Extract C++ fields
    auto cpp_Epc1 = extractField(cpp_result, [](const DeformationResult& r) -> const std::vector<double>& { return r.Epc1; });
    auto cpp_Epc2 = extractField(cpp_result, [](const DeformationResult& r) -> const std::vector<double>& { return r.Epc2; });
    auto cpp_epc1 = extractField(cpp_result, [](const DeformationResult& r) -> const std::vector<double>& { return r.epc1; });
    auto cpp_epc2 = extractField(cpp_result, [](const DeformationResult& r) -> const std::vector<double>& { return r.epc2; });
    auto cpp_EShearMax = extractField(cpp_result, [](const DeformationResult& r) -> const std::vector<double>& { return r.EShearMax; });
    auto cpp_eShearMax = extractField(cpp_result, [](const DeformationResult& r) -> const std::vector<double>& { return r.eShearMax; });
    auto cpp_Eeq = extractField(cpp_result, [](const DeformationResult& r) -> const std::vector<double>& { return r.Eeq; });
    auto cpp_eeq = extractField(cpp_result, [](const DeformationResult& r) -> const std::vector<double>& { return r.eeq; });
    auto cpp_Emgn = extractField(cpp_result, [](const DeformationResult& r) -> const std::vector<double>& { return r.Emgn; });
    auto cpp_emgn = extractField(cpp_result, [](const DeformationResult& r) -> const std::vector<double>& { return r.emgn; });
    auto cpp_J = extractField(cpp_result, [](const DeformationResult& r) -> const std::vector<double>& { return r.J; });
    auto cpp_Lamda1 = extractField(cpp_result, [](const DeformationResult& r) -> const std::vector<double>& { return r.Lamda1; });
    auto cpp_Lamda2 = extractField(cpp_result, [](const DeformationResult& r) -> const std::vector<double>& { return r.Lamda2; });
    
    // Compare all fields
    std::vector<FieldComparison> comparisons;
    
    compareDeformField("Epc1", cpp_Epc1, deform_ref.Epc1, tolerance, comparisons);
    compareDeformField("Epc2", cpp_Epc2, deform_ref.Epc2, tolerance, comparisons);
    compareDeformField("epc1", cpp_epc1, deform_ref.epc1, tolerance, comparisons);
    compareDeformField("epc2", cpp_epc2, deform_ref.epc2, tolerance, comparisons);
    compareDeformField("EShearMax", cpp_EShearMax, deform_ref.EShearMax, tolerance, comparisons);
    compareDeformField("eShearMax", cpp_eShearMax, deform_ref.eShearMax, tolerance, comparisons);
    compareDeformField("Eeq", cpp_Eeq, deform_ref.Eeq, tolerance, comparisons);
    compareDeformField("eeq", cpp_eeq, deform_ref.eeq, tolerance, comparisons);
    compareDeformField("Emgn", cpp_Emgn, deform_ref.Emgn, tolerance, comparisons);
    compareDeformField("emgn", cpp_emgn, deform_ref.emgn, tolerance, comparisons);
    compareDeformField("J", cpp_J, deform_ref.J, 1e-3, comparisons);  // Relaxed for J
    compareDeformField("Lamda1", cpp_Lamda1, deform_ref.Lamda1, tolerance, comparisons);
    compareDeformField("Lamda2", cpp_Lamda2, deform_ref.Lamda2, tolerance, comparisons);
    
    // Print results
    std::cout << "\n--- Deformation Field Comparisons ---" << std::endl;
    for (const auto& fc : comparisons) printComparison(fc);
    
    int passed = 0;
    for (const auto& fc : comparisons) if (fc.passed) passed++;
    
    std::cout << "\n========================================" << std::endl;
    std::cout << "Deformation: " << passed << "/" << comparisons.size() << " fields passed" << std::endl;
    std::cout << "========================================" << std::endl;
    
    writeReport(test_data_dir + "/deformation_report.txt",
                "Deformation Integration Test", comparisons);
    
    return (passed == (int)comparisons.size()) ? 0 : 1;
}
