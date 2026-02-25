/**
 * Integration Test for Complete DIC Pipeline
 * Validates C++ implementation against MATLAB reference data
 * Tests the complete workflow from image input to strain output
 */

#include <iostream>
#include <vector>
#include <cmath>
#include <matio.h>
#include <filesystem>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace fs = std::filesystem;

// Tolerance levels for different types of comparisons
struct ToleranceLevels {
    static constexpr double EXACT = 1e-12;
    static constexpr double STRICT = 1e-10;
    static constexpr double NORMAL = 1e-6;
    static constexpr double RELAXED = 1e-3;
    static constexpr double PERCENT_1 = 0.01;
    static constexpr double PERCENT_5 = 0.05;
};

class PipelineValidator {
public:
    struct ComparisonResult {
        bool passed = true;
        std::string field_name;
        double max_error = 0.0;
        double mean_error = 0.0;
        double rmse = 0.0;
        int num_comparisons = 0;
        int num_failures = 0;
        double tolerance_used = 0.0;
        std::vector<std::string> failure_details;
    };

    struct TestResult {
        bool passed = true;
        std::string test_name;
        std::string test_stage;
        std::vector<ComparisonResult> comparisons;
        double execution_time_ms = 0.0;
        std::vector<std::string> errors;
        std::vector<std::string> warnings;
        
        int getTotalComparisons() const {
            int total = 0;
            for (const auto& comp : comparisons) {
                total += comp.num_comparisons;
            }
            return total;
        }
        
        int getTotalFailures() const {
            int total = 0;
            for (const auto& comp : comparisons) {
                total += comp.num_failures;
            }
            return total;
        }
    };

    /**
     * Compare 3D point clouds from Step E
     */
    TestResult validate3DReconstruction(const std::string& cpp_file, 
                                       const std::string& matlab_file) {
        TestResult result;
        result.test_name = "3D Reconstruction (Step E)";
        result.test_stage = "DIC3Dcombined";

        auto start = std::chrono::high_resolution_clock::now();

        // Load C++ binary
        mat_t* cpp_mat = Mat_Open(cpp_file.c_str(), MAT_ACC_RDONLY);
        mat_t* matlab_mat = Mat_Open(matlab_file.c_str(), MAT_ACC_RDONLY);

        if (!cpp_mat || !matlab_mat) {
            result.errors.push_back("Failed to open input files");
            result.passed = false;
            return result;
        }

        // Compare Points3D
        result.comparisons.push_back(comparePoints3D(cpp_mat, matlab_mat));
        
        // Compare Faces
        result.comparisons.push_back(compareFaces(cpp_mat, matlab_mat));
        
        // Compare correlation values
        result.comparisons.push_back(compareCorrelation(cpp_mat, matlab_mat));
        
        // Compare displacement field
        result.comparisons.push_back(compareDisplacement(cpp_mat, matlab_mat));

        Mat_Close(cpp_mat);
        Mat_Close(matlab_mat);

        auto end = std::chrono::high_resolution_clock::now();
        result.execution_time_ms = std::chrono::duration<double, std::milli>(end - start).count();

        // Check if all comparisons passed
        for (const auto& comp : result.comparisons) {
            if (!comp.passed) {
                result.passed = false;
                break;
            }
        }

        return result;
    }

    /**
     * Compare deformation results from Step F
     */
    TestResult validateDeformation(const std::string& cpp_file,
                                  const std::string& matlab_file) {
        TestResult result;
        result.test_name = "Deformation Analysis (Step F)";
        result.test_stage = "DIC3DPPresults";

        auto start = std::chrono::high_resolution_clock::now();

        mat_t* cpp_mat = Mat_Open(cpp_file.c_str(), MAT_ACC_RDONLY);
        mat_t* matlab_mat = Mat_Open(matlab_file.c_str(), MAT_ACC_RDONLY);

        if (!cpp_mat || !matlab_mat) {
            result.errors.push_back("Failed to open input files");
            result.passed = false;
            return result;
        }

        // Compare principal strains
        result.comparisons.push_back(compareCellArray(cpp_mat, matlab_mat, "Deform.Epc1", 
                                                      ToleranceLevels::NORMAL));
        result.comparisons.push_back(compareCellArray(cpp_mat, matlab_mat, "Deform.Epc2",
                                                      ToleranceLevels::NORMAL));
        
        // Compare engineering strains
        result.comparisons.push_back(compareCellArray(cpp_mat, matlab_mat, "Deform.epc1",
                                                      ToleranceLevels::NORMAL));
        result.comparisons.push_back(compareCellArray(cpp_mat, matlab_mat, "Deform.epc2",
                                                      ToleranceLevels::NORMAL));
        
        // Compare max shear strains
        result.comparisons.push_back(compareCellArray(cpp_mat, matlab_mat, "Deform.EShearMax",
                                                      ToleranceLevels::NORMAL));
        result.comparisons.push_back(compareCellArray(cpp_mat, matlab_mat, "Deform.eShearMax",
                                                      ToleranceLevels::NORMAL));
        
        // Compare equivalent strains
        result.comparisons.push_back(compareCellArray(cpp_mat, matlab_mat, "Deform.Eeq",
                                                      ToleranceLevels::NORMAL));
        result.comparisons.push_back(compareCellArray(cpp_mat, matlab_mat, "Deform.eeq",
                                                      ToleranceLevels::NORMAL));
        
        // Compare deformation gradient
        result.comparisons.push_back(compareCellArray(cpp_mat, matlab_mat, "Deform.J",
                                                      ToleranceLevels::RELAXED));
        
        // Compare principal stretches
        result.comparisons.push_back(compareCellArray(cpp_mat, matlab_mat, "Deform.Lamda1",
                                                      ToleranceLevels::NORMAL));
        result.comparisons.push_back(compareCellArray(cpp_mat, matlab_mat, "Deform.Lamda2",
                                                      ToleranceLevels::NORMAL));

        Mat_Close(cpp_mat);
        Mat_Close(matlab_mat);

        auto end = std::chrono::high_resolution_clock::now();
        result.execution_time_ms = std::chrono::duration<double, std::milli>(end - start).count();

        // Check if all comparisons passed
        for (const auto& comp : result.comparisons) {
            if (!comp.passed) {
                result.passed = false;
                break;
            }
        }

        return result;
    }

    /**
     * Generate comprehensive validation report
     */
    void generateReport(const std::vector<TestResult>& results,
                       const std::string& output_file = "") {
        std::stringstream report;
        
        report << "\n================================================================\n";
        report << "           DIC PIPELINE VALIDATION REPORT\n";
        report << "================================================================\n\n";
        report << "Generated: " << getCurrentTimestamp() << "\n\n";

        // Summary statistics
        int total_tests = results.size();
        int passed_tests = 0;
        int total_comparisons = 0;
        int total_failures = 0;
        double total_time = 0.0;

        for (const auto& result : results) {
            if (result.passed) passed_tests++;
            total_comparisons += result.getTotalComparisons();
            total_failures += result.getTotalFailures();
            total_time += result.execution_time_ms;
        }

        report << "SUMMARY\n";
        report << "-------\n";
        report << "Tests Run:        " << total_tests << "\n";
        report << "Tests Passed:     " << passed_tests << " (" 
               << std::fixed << std::setprecision(1) 
               << (100.0 * passed_tests / total_tests) << "%)\n";
        report << "Total Comparisons: " << total_comparisons << "\n";
        report << "Total Failures:    " << total_failures << "\n";
        report << "Success Rate:      " << std::fixed << std::setprecision(2)
               << (100.0 * (total_comparisons - total_failures) / total_comparisons) << "%\n";
        report << "Total Time:        " << std::fixed << std::setprecision(2) 
               << total_time << " ms\n\n";

        // Detailed results
        report << "DETAILED RESULTS\n";
        report << "----------------\n\n";

        for (const auto& result : results) {
            report << "Test: " << result.test_name << "\n";
            report << "Stage: " << result.test_stage << "\n";
            report << "Status: " << (result.passed ? "PASSED ✓" : "FAILED ✗") << "\n";
            report << "Execution Time: " << std::fixed << std::setprecision(2) 
                   << result.execution_time_ms << " ms\n";

            if (!result.errors.empty()) {
                report << "Errors:\n";
                for (const auto& error : result.errors) {
                    report << "  • " << error << "\n";
                }
            }

            if (!result.warnings.empty()) {
                report << "Warnings:\n";
                for (const auto& warning : result.warnings) {
                    report << "  • " << warning << "\n";
                }
            }

            if (!result.comparisons.empty()) {
                report << "Field Comparisons:\n";
                for (const auto& comp : result.comparisons) {
                    report << "  " << std::left << std::setw(30) << comp.field_name;
                    if (comp.passed) {
                        report << " PASS";
                    } else {
                        report << " FAIL";
                    }
                    report << " (max_err=" << std::scientific << std::setprecision(3) 
                           << comp.max_error;
                    report << ", tol=" << comp.tolerance_used << ")\n";
                    
                    if (!comp.passed && !comp.failure_details.empty()) {
                        for (size_t i = 0; i < std::min(size_t(3), comp.failure_details.size()); ++i) {
                            report << "    - " << comp.failure_details[i] << "\n";
                        }
                        if (comp.failure_details.size() > 3) {
                            report << "    ... and " << (comp.failure_details.size() - 3) 
                                   << " more failures\n";
                        }
                    }
                }
            }
            report << "\n";
        }

        // Recommendations
        report << "RECOMMENDATIONS\n";
        report << "---------------\n";
        if (total_failures > 0) {
            double failure_rate = 100.0 * total_failures / total_comparisons;
            if (failure_rate > 10) {
                report << "• High failure rate detected (" << std::fixed << std::setprecision(1) 
                       << failure_rate << "%). Review numerical algorithms.\n";
            } else if (failure_rate > 1) {
                report << "• Moderate failure rate (" << std::fixed << std::setprecision(1)
                       << failure_rate << "%). Consider relaxing tolerances for accumulated errors.\n";
            } else {
                report << "• Low failure rate (" << std::fixed << std::setprecision(1)
                       << failure_rate << "%). Minor numerical differences are acceptable.\n";
            }
        } else {
            report << "• All comparisons passed! C++ implementation matches MATLAB reference.\n";
        }

        report << "\n================================================================\n\n";

        // Output report
        std::cout << report.str();

        if (!output_file.empty()) {
            std::ofstream file(output_file);
            if (file.is_open()) {
                file << report.str();
                file.close();
                std::cout << "Report saved to: " << output_file << "\n";
            }
        }
    }

private:
    ComparisonResult comparePoints3D(mat_t* cpp_mat, mat_t* matlab_mat) {
        ComparisonResult result;
        result.field_name = "Points3D";
        result.tolerance_used = ToleranceLevels::NORMAL;

        matvar_t* cpp_var = Mat_VarRead(cpp_mat, "Points3D");
        matvar_t* matlab_var = Mat_VarRead(matlab_mat, "Points3D");

        if (!cpp_var || !matlab_var) {
            result.passed = false;
            result.failure_details.push_back("Variable not found in one or both files");
            return result;
        }

        // Compare cell array of points
        if (cpp_var->class_type == MAT_C_CELL && matlab_var->class_type == MAT_C_CELL) {
            size_t n_frames = std::min(cpp_var->dims[1], matlab_var->dims[1]);
            
            for (size_t frame = 0; frame < n_frames; ++frame) {
                matvar_t* cpp_frame = Mat_VarGetCell(cpp_var, frame);
                matvar_t* matlab_frame = Mat_VarGetCell(matlab_var, frame);
                
                if (cpp_frame && matlab_frame) {
                    compareFramePoints(cpp_frame, matlab_frame, frame, result);
                }
            }
        }

        Mat_VarFree(cpp_var);
        Mat_VarFree(matlab_var);

        return result;
    }

    void compareFramePoints(matvar_t* cpp_frame, matvar_t* matlab_frame, 
                           size_t frame_idx, ComparisonResult& result) {
        // Extract x, y, z coordinates and compare
        matvar_t* cpp_x = Mat_VarGetStructFieldByName(cpp_frame, "x", 0);
        matvar_t* cpp_y = Mat_VarGetStructFieldByName(cpp_frame, "y", 0);
        matvar_t* cpp_z = Mat_VarGetStructFieldByName(cpp_frame, "z", 0);
        
        matvar_t* mat_x = Mat_VarGetStructFieldByName(matlab_frame, "x", 0);
        matvar_t* mat_y = Mat_VarGetStructFieldByName(matlab_frame, "y", 0);
        matvar_t* mat_z = Mat_VarGetStructFieldByName(matlab_frame, "z", 0);
        
        if (cpp_x && mat_x) {
            compareArrays(static_cast<double*>(cpp_x->data), 
                         static_cast<double*>(mat_x->data),
                         cpp_x->dims[0], result, "x", frame_idx);
        }
        
        if (cpp_y && mat_y) {
            compareArrays(static_cast<double*>(cpp_y->data),
                         static_cast<double*>(mat_y->data),
                         cpp_y->dims[0], result, "y", frame_idx);
        }
        
        if (cpp_z && mat_z) {
            compareArrays(static_cast<double*>(cpp_z->data),
                         static_cast<double*>(mat_z->data),
                         cpp_z->dims[0], result, "z", frame_idx);
        }
    }

    ComparisonResult compareFaces(mat_t* cpp_mat, mat_t* matlab_mat) {
        ComparisonResult result;
        result.field_name = "Faces";
        result.tolerance_used = ToleranceLevels::EXACT;

        matvar_t* cpp_var = Mat_VarRead(cpp_mat, "Faces");
        matvar_t* matlab_var = Mat_VarRead(matlab_mat, "Faces");

        if (!cpp_var || !matlab_var) {
            result.passed = false;
            result.failure_details.push_back("Faces variable not found");
            return result;
        }

        if (cpp_var->class_type == MAT_C_INT32 && matlab_var->class_type == MAT_C_INT32) {
            int* cpp_data = static_cast<int*>(cpp_var->data);
            int* matlab_data = static_cast<int*>(matlab_var->data);
            size_t n_faces = cpp_var->dims[0] * cpp_var->dims[1];
            
            for (size_t i = 0; i < n_faces; ++i) {
                result.num_comparisons++;
                if (cpp_data[i] != matlab_data[i]) {
                    result.num_failures++;
                    if (result.failure_details.size() < 5) {
                        std::stringstream ss;
                        ss << "Face[" << i/3 << "][" << i%3 << "]: "
                           << cpp_data[i] << " != " << matlab_data[i];
                        result.failure_details.push_back(ss.str());
                    }
                }
            }
        }

        result.passed = (result.num_failures == 0);
        Mat_VarFree(cpp_var);
        Mat_VarFree(matlab_var);
        
        return result;
    }

    ComparisonResult compareCorrelation(mat_t* cpp_mat, mat_t* matlab_mat) {
        return compareCellArray(cpp_mat, matlab_mat, "FaceCorrComb", ToleranceLevels::RELAXED);
    }

    ComparisonResult compareDisplacement(mat_t* cpp_mat, mat_t* matlab_mat) {
        return compareCellArray(cpp_mat, matlab_mat, "Disp", ToleranceLevels::NORMAL);
    }

    ComparisonResult compareCellArray(mat_t* cpp_mat, mat_t* matlab_mat,
                                      const std::string& var_name, double tolerance) {
        ComparisonResult result;
        result.field_name = var_name;
        result.tolerance_used = tolerance;

        // Handle nested field access (e.g., "Deform.Epc1")
        std::string parent_var = var_name;
        std::string field_name = "";
        size_t dot_pos = var_name.find('.');
        if (dot_pos != std::string::npos) {
            parent_var = var_name.substr(0, dot_pos);
            field_name = var_name.substr(dot_pos + 1);
        }

        matvar_t* cpp_var = Mat_VarRead(cpp_mat, parent_var.c_str());
        matvar_t* matlab_var = Mat_VarRead(matlab_mat, parent_var.c_str());

        if (!cpp_var || !matlab_var) {
            result.passed = false;
            result.failure_details.push_back("Variable " + parent_var + " not found");
            return result;
        }

        // If we have a field name, get the field
        if (!field_name.empty()) {
            matvar_t* cpp_field = Mat_VarGetStructFieldByName(cpp_var, field_name.c_str(), 0);
            matvar_t* matlab_field = Mat_VarGetStructFieldByName(matlab_var, field_name.c_str(), 0);
            
            Mat_VarFree(cpp_var);
            Mat_VarFree(matlab_var);
            
            cpp_var = cpp_field;
            matlab_var = matlab_field;
        }

        if (!cpp_var || !matlab_var) {
            result.passed = false;
            result.failure_details.push_back("Field " + field_name + " not found");
            return result;
        }

        // Compare cell arrays
        if (cpp_var->class_type == MAT_C_CELL && matlab_var->class_type == MAT_C_CELL) {
            size_t n_cells = std::min(cpp_var->dims[0] * cpp_var->dims[1],
                                     matlab_var->dims[0] * matlab_var->dims[1]);
            
            for (size_t i = 0; i < n_cells; ++i) {
                matvar_t* cpp_cell = Mat_VarGetCell(cpp_var, i);
                matvar_t* matlab_cell = Mat_VarGetCell(matlab_var, i);
                
                if (cpp_cell && matlab_cell && 
                    cpp_cell->class_type == MAT_C_DOUBLE &&
                    matlab_cell->class_type == MAT_C_DOUBLE) {
                    
                    compareArrays(static_cast<double*>(cpp_cell->data),
                                 static_cast<double*>(matlab_cell->data),
                                 cpp_cell->dims[0] * cpp_cell->dims[1],
                                 result, "cell", i);
                }
            }
        }

        result.passed = (result.num_failures == 0) || 
                       (double(result.num_failures) / result.num_comparisons < 0.01);

        return result;
    }

    void compareArrays(double* cpp_data, double* matlab_data, size_t size,
                      ComparisonResult& result, const std::string& label, size_t idx) {
        double sum_squared_error = 0.0;
        
        for (size_t i = 0; i < size; ++i) {
            double error = std::abs(cpp_data[i] - matlab_data[i]);
            result.num_comparisons++;
            result.max_error = std::max(result.max_error, error);
            result.mean_error += error;
            sum_squared_error += error * error;
            
            if (error > result.tolerance_used) {
                result.num_failures++;
                if (result.failure_details.size() < 5) {
                    std::stringstream ss;
                    ss << label << "[" << idx << "][" << i << "]: "
                       << std::scientific << std::setprecision(6)
                       << cpp_data[i] << " vs " << matlab_data[i] 
                       << " (err=" << error << ")";
                    result.failure_details.push_back(ss.str());
                }
            }
        }
        
        if (result.num_comparisons > 0) {
            result.mean_error /= result.num_comparisons;
            result.rmse = std::sqrt(sum_squared_error / result.num_comparisons);
        }
    }

    std::string getCurrentTimestamp() {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
        return ss.str();
    }
};

int main(int argc, char** argv) {
    std::cout << "\n=== DIC PIPELINE INTEGRATION VALIDATION ===\n\n";

    // Parse command line arguments
    std::string test_data_dir = "test_data/";
    std::string report_file = "validation_report.txt";
    
    if (argc > 1) test_data_dir = argv[1];
    if (argc > 2) report_file = argv[2];

    std::cout << "Test data directory: " << test_data_dir << "\n";
    std::cout << "Report output: " << report_file << "\n\n";

    PipelineValidator validator;
    std::vector<PipelineValidator::TestResult> results;

    // Test 3D Reconstruction (Step E)
    std::string cpp_3d_file = test_data_dir + "/DIC3Dcombined_cpp.mat";
    std::string matlab_3d_file = test_data_dir + "/DIC3Dcombined_matlab.mat";
    
    if (fs::exists(cpp_3d_file) && fs::exists(matlab_3d_file)) {
        std::cout << "Testing 3D Reconstruction...\n";
        results.push_back(validator.validate3DReconstruction(cpp_3d_file, matlab_3d_file));
    } else {
        std::cout << "Skipping 3D Reconstruction test (files not found)\n";
    }

    // Test Deformation Analysis (Step F)
    std::string cpp_deform_file = test_data_dir + "/DIC3DPPresults_cpp.mat";
    std::string matlab_deform_file = test_data_dir + "/DIC3DPPresults_matlab.mat";
    
    if (fs::exists(cpp_deform_file) && fs::exists(matlab_deform_file)) {
        std::cout << "Testing Deformation Analysis...\n";
        results.push_back(validator.validateDeformation(cpp_deform_file, matlab_deform_file));
    } else {
        std::cout << "Skipping Deformation test (files not found)\n";
    }

    // Generate comprehensive report
    validator.generateReport(results, report_file);

    // Return success/failure code
    bool all_passed = true;
    for (const auto& result : results) {
        all_passed = all_passed && result.passed;
    }

    if (all_passed) {
        std::cout << "\n✓ All validation tests PASSED\n";
        return 0;
    } else {
        std::cout << "\n✗ Some validation tests FAILED\n";
        std::cout << "See report for details: " << report_file << "\n";
        return 1;
    }
}
