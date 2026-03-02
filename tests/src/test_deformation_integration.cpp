/**
 * Integration Test: Deformation Analysis (Step F)
 *
 * This test calls DicAnalysis::runStepF() directly to execute the C++ TCPE
 * deformation pipeline, then compares the generated .mat output against
 * a MATLAB reference.
 *
 * Usage:
 *   ./test_deformation_integration <test_root_dir> [tolerance]
 *
 * The <test_root_dir> must mirror the real project directory layout:
 *
 *   <test_root_dir>/
 *     dic_output/<subject>/<material>/
 *       DIC3Dcombined_<N>Pairs_stitched.bin  <- Step E binary (or generated from .mat)
 *     matlab_reference/
 *       DIC3Dcombined_matlab.mat             (MATLAB Step E output, used as input)
 *       DIC3DPPresults_matlab.mat            (MATLAB Step F reference output)
 *     test_config.txt                        (Config overrides)
 *
 * The test:
 *   1. If the .bin file doesn't exist, loads DIC3Dcombined_matlab.mat and
 *      saves it as .bin at the path Step F expects
 *   2. Configures DicAnalysis and calls runStepF()
 *   3. Loads the C++ generated DIC3DPPresults .mat
 *   4. Loads the MATLAB reference DIC3DPPresults .mat
 *   5. Compares all deformation fields and produces a report
 */

#include "dic_analysis.h"
#include "config.h"
#include "mat_reader.h"
#include "dic_structures.h"
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
}

// ============================================================================
// Load Deform struct from DIC3DPPresults .mat
// ============================================================================

struct DeformRef {
    std::vector<std::vector<double>> Epc1, Epc2, epc1, epc2;
    std::vector<std::vector<double>> EShearMax, eShearMax;
    std::vector<std::vector<double>> Eeq, eeq;
    std::vector<std::vector<double>> Emgn, emgn;
    std::vector<std::vector<double>> J;
    std::vector<std::vector<double>> Lamda1, Lamda2;
    bool valid = false;
};

static std::vector<std::vector<double>> loadCellField(matvar_t* parent, const char* name) {
    std::vector<std::vector<double>> result;
    matvar_t* field = Mat_VarGetStructFieldByName(parent, name, 0);
    if (!field || field->class_type != MAT_C_CELL) return result;
    size_t n = field->dims[0] * field->dims[1];
    for (size_t i = 0; i < n; ++i)
        result.push_back(readDoubleArray(Mat_VarGetCell(field, i)));
    return result;
}

DeformRef loadDeformFromPPresults(const std::string& mat_path) {
    DeformRef ref;
    mat_t* matfp = Mat_Open(mat_path.c_str(), MAT_ACC_RDONLY);
    if (!matfp) return ref;
    
    matvar_t* dv = Mat_VarRead(matfp, "Deform");
    if (!dv || dv->class_type != MAT_C_STRUCT) {
        if (dv) Mat_VarFree(dv);
        Mat_Close(matfp);
        return ref;
    }
    
    ref.Epc1      = loadCellField(dv, "Epc1");
    ref.Epc2      = loadCellField(dv, "Epc2");
    ref.epc1      = loadCellField(dv, "epc1");
    ref.epc2      = loadCellField(dv, "epc2");
    ref.EShearMax = loadCellField(dv, "EShearMax");
    ref.eShearMax = loadCellField(dv, "eShearMax");
    ref.Eeq       = loadCellField(dv, "Eeq");
    ref.eeq       = loadCellField(dv, "eeq");
    ref.Emgn      = loadCellField(dv, "Emgn");
    ref.emgn      = loadCellField(dv, "emgn");
    ref.J         = loadCellField(dv, "J");
    ref.Lamda1    = loadCellField(dv, "Lamda1");
    ref.Lamda2    = loadCellField(dv, "Lamda2");
    
    Mat_VarFree(dv);
    Mat_Close(matfp);
    ref.valid = !ref.Epc1.empty();
    return ref;
}

// Load Deform from a C++ generated DIC3DPPresults .mat (same format)
DeformRef loadDeformFromCppOutput(const std::string& mat_path) {
    return loadDeformFromPPresults(mat_path);
}

// ============================================================================
// Compare a deformation field (first + last frame)
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
        fc.details.push_back("No frames (C++=" + std::to_string(cpp_frames.size()) +
                            " MAT=" + std::to_string(mat_frames.size()) + ")");
        comparisons.push_back(fc);
        return;
    }
    for (size_t f : {size_t(0), nFrames > 1 ? nFrames - 1 : size_t(0)}) {
        if (f >= nFrames) continue;
        std::string label = name + "[" + std::to_string(f) + "]";
        comparisons.push_back(compareDoubleArrays(label, cpp_frames[f], mat_frames[f], tolerance));
    }
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    std::string test_root = "test_data";
    double tolerance = 1e-6;
    
    if (argc > 1) test_root = argv[1];
    if (argc > 2) tolerance = std::stod(argv[2]);
    
    std::cout << "\n=== DEFORMATION ANALYSIS INTEGRATION TEST ===" << std::endl;
    std::cout << "Test root: " << test_root << std::endl;
    std::cout << "Tolerance: " << std::scientific << tolerance << std::endl;
    
    // =====================================================================
    // 1. Configure DicAnalysis
    // =====================================================================
    Config config;
    std::string config_file = test_root + "/test_config.txt";
    if (fs::exists(config_file)) {
        config.loadFromDicParamsFile(config_file);
        std::cout << "Loaded config from: " << config_file << std::endl;
    } else {
        std::cerr << "Config file not found: " << config_file << std::endl;
        std::cerr << "\nCreate " << config_file << " with at least:" << std::endl;
        std::cerr << "  subject_id = S09" << std::endl;
        std::cerr << "  material = glass" << std::endl;
        std::cerr << "  num_pair = 2" << std::endl;
        std::cerr << "  dic_path = " << test_root << "/dic_output" << std::endl;
        std::cerr << "\nSee PREPARE_TEST_DATA.md for full directory layout." << std::endl;
        return 1;
    }
    
    config.data_format = "mat";
    config.mapLogic = false;
    config.debug_mode = true;
    // Disable temporal filtering for clean comparison against MATLAB TCPE output
    config.smoothTimeLogic = false;
    
    std::cout << "\nConfig:" << std::endl;
    std::cout << "  subject_id: " << config.subject_id << std::endl;
    std::cout << "  material:   " << config.material << std::endl;
    std::cout << "  num_pair:   " << config.num_pair << std::endl;
    std::cout << "  dic_path:   " << config.dic_path << std::endl;
    
    std::vector<int> trial_target = {config.ref_trial_id};
    std::cout << "  trial:      " << trial_target[0] << std::endl;
    
    // =====================================================================
    // 2. Ensure DIC3Dcombined .bin exists (Step F reads binary, not .mat)
    // =====================================================================
    std::string output_dir = config.dic_path + "/" + config.subject_id + "/" + config.material;
    std::string bin_path = output_dir + "/DIC3Dcombined_" +
                           std::to_string(config.num_pair) + "Pairs_stitched.bin";
    
    std::string matlab_combined = test_root + "/matlab_reference/DIC3Dcombined_matlab.mat";
    
    if (!fs::exists(bin_path)) {
        // Convert MATLAB .mat → binary .bin so Step F can read it
        std::cout << "\nNo .bin found at: " << bin_path << std::endl;
        std::cout << "Converting from MATLAB .mat: " << matlab_combined << std::endl;
        
        if (!fs::exists(matlab_combined)) {
            std::cerr << "MATLAB DIC3Dcombined not found: " << matlab_combined << std::endl;
            std::cerr << "Place your MATLAB DIC3Dcombined_*.mat there." << std::endl;
            return 1;
        }
        
        cppxdic::DIC3Dcombined combined;
        if (!cppxdic::MatReader::readDIC3Dcombined(matlab_combined, combined)) {
            std::cerr << "Failed to read DIC3Dcombined from .mat" << std::endl;
            return 1;
        }
        
        fs::create_directories(output_dir);
        try {
            combined.saveBinary(bin_path);
            std::cout << "  Saved binary: " << bin_path << std::endl;
            std::cout << "  " << combined.Points3D.size() << " frames, "
                      << (combined.Points3D.empty() ? 0 : combined.Points3D[0].x.size()) << " points, "
                      << combined.Faces.size() / 3 << " faces" << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "Failed to save binary: " << e.what() << std::endl;
            return 1;
        }
    } else {
        std::cout << "\nUsing existing .bin: " << bin_path << std::endl;
    }
    
    // Remove existing Step F output to force re-computation
    std::string cpp_pp_mat = output_dir + "/DIC3DPPresults_" +
                              std::to_string(config.num_pair) + "Pairs_cum_v1.mat";
    std::string cpp_pp_bin = output_dir + "/DIC3DPPresults_" +
                              std::to_string(config.num_pair) + "Pairs_cum_" + config.fileversion + ".bin";
    
    if (fs::exists(cpp_pp_mat)) { fs::remove(cpp_pp_mat); std::cout << "  Removed: " << cpp_pp_mat << std::endl; }
    if (fs::exists(cpp_pp_bin)) { fs::remove(cpp_pp_bin); std::cout << "  Removed: " << cpp_pp_bin << std::endl; }
    
    // =====================================================================
    // 3. Run Step F via DicAnalysis::runStepF()
    // =====================================================================
    std::cout << "\n--- Running C++ Deformation Analysis (Step F) ---" << std::endl;
    
    auto t0 = std::chrono::high_resolution_clock::now();
    DicAnalysis analysis(config);
    bool step_f_ok = analysis.runStepF(trial_target);
    auto t1 = std::chrono::high_resolution_clock::now();
    double elapsed = std::chrono::duration<double>(t1 - t0).count();
    
    if (!step_f_ok) {
        std::cerr << "\nStep F FAILED (" << elapsed << " s)" << std::endl;
        return 1;
    }
    std::cout << "\nStep F completed in " << std::fixed << std::setprecision(2) << elapsed << " s" << std::endl;
    
    // =====================================================================
    // 4. Load C++ output and MATLAB reference
    // =====================================================================
    if (!fs::exists(cpp_pp_mat)) {
        std::cerr << "C++ output not generated: " << cpp_pp_mat << std::endl;
        return 1;
    }
    
    std::string matlab_pp_ref = test_root + "/matlab_reference/DIC3DPPresults_matlab.mat";
    if (!fs::exists(matlab_pp_ref)) {
        std::cerr << "MATLAB reference not found: " << matlab_pp_ref << std::endl;
        std::cerr << "Step F ran successfully but no MATLAB reference to compare." << std::endl;
        std::cerr << "Place MATLAB DIC3DPPresults_*.mat at: " << matlab_pp_ref << std::endl;
        std::cout << "\nStep F: PASS (no comparison, reference missing)" << std::endl;
        return 0;
    }
    
    std::cout << "\nLoading C++ output:       " << cpp_pp_mat << std::endl;
    auto cpp_deform = loadDeformFromCppOutput(cpp_pp_mat);
    
    std::cout << "Loading MATLAB reference: " << matlab_pp_ref << std::endl;
    auto mat_deform = loadDeformFromPPresults(matlab_pp_ref);
    
    if (!cpp_deform.valid) { std::cerr << "Failed to load C++ Deform" << std::endl; return 1; }
    if (!mat_deform.valid) { std::cerr << "Failed to load MATLAB Deform" << std::endl; return 1; }
    
    std::cout << "  C++ frames:    " << cpp_deform.Epc1.size() << std::endl;
    std::cout << "  MATLAB frames: " << mat_deform.Epc1.size() << std::endl;
    
    // =====================================================================
    // 5. Compare all deformation fields
    // =====================================================================
    std::vector<FieldComparison> comparisons;
    
    compareDeformField("Epc1",      cpp_deform.Epc1,      mat_deform.Epc1,      tolerance, comparisons);
    compareDeformField("Epc2",      cpp_deform.Epc2,      mat_deform.Epc2,      tolerance, comparisons);
    compareDeformField("epc1",      cpp_deform.epc1,      mat_deform.epc1,      tolerance, comparisons);
    compareDeformField("epc2",      cpp_deform.epc2,      mat_deform.epc2,      tolerance, comparisons);
    compareDeformField("EShearMax", cpp_deform.EShearMax, mat_deform.EShearMax, tolerance, comparisons);
    compareDeformField("eShearMax", cpp_deform.eShearMax, mat_deform.eShearMax, tolerance, comparisons);
    compareDeformField("Eeq",       cpp_deform.Eeq,       mat_deform.Eeq,       tolerance, comparisons);
    compareDeformField("eeq",       cpp_deform.eeq,       mat_deform.eeq,       tolerance, comparisons);
    compareDeformField("Emgn",      cpp_deform.Emgn,      mat_deform.Emgn,      tolerance, comparisons);
    compareDeformField("emgn",      cpp_deform.emgn,      mat_deform.emgn,      tolerance, comparisons);
    compareDeformField("J",         cpp_deform.J,         mat_deform.J,         1e-3, comparisons);
    compareDeformField("Lamda1",    cpp_deform.Lamda1,    mat_deform.Lamda1,    tolerance, comparisons);
    compareDeformField("Lamda2",    cpp_deform.Lamda2,    mat_deform.Lamda2,    tolerance, comparisons);
    
    // =====================================================================
    // 6. Results
    // =====================================================================
    std::cout << "\n--- Deformation Field Comparisons ---" << std::endl;
    for (const auto& fc : comparisons) printComparison(fc);
    
    int passed = 0;
    for (const auto& fc : comparisons) if (fc.passed) passed++;
    
    std::cout << "\n========================================" << std::endl;
    std::cout << "Deformation: " << passed << "/" << comparisons.size()
              << " fields passed (" << elapsed << " s)" << std::endl;
    std::cout << "========================================" << std::endl;
    
    writeReport(test_root + "/deformation_report.txt",
                "Deformation Integration Test", comparisons);
    std::cout << "Report: " << test_root << "/deformation_report.txt" << std::endl;
    
    return (passed == (int)comparisons.size()) ? 0 : 1;
}
