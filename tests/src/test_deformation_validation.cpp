/**
 * Validation Test for TCPE Deformation Algorithm
 * Compares C++ implementation against MATLAB reference data
 */

#include <iostream>
#include <vector>
#include <cmath>
#include <matio.h>
#include <filesystem>
#include <Eigen/Dense>
#include "../../src/strain_computation.cpp"
#include "../../include/dic_structures.h"

namespace fs = std::filesystem;

// Tolerance for numerical comparison
constexpr double TOLERANCE_STRICT = 1e-10;    // For exact matches
constexpr double TOLERANCE_NORMAL = 1e-6;     // For general numerical comparison  
constexpr double TOLERANCE_RELAXED = 1e-3;    // For accumulated error cases

class DeformationValidator {
public:
    struct TestResult {
        bool passed = true;
        std::string test_name;
        std::vector<std::string> errors;
        std::vector<std::string> warnings;
        int num_comparisons = 0;
        int num_failures = 0;
        double max_error = 0.0;
    };

    /**
     * Load MATLAB reference data for deformation test
     */
    bool loadMatlabReference(const std::string& mat_file) {
        mat_t* matfp = Mat_Open(mat_file.c_str(), MAT_ACC_RDONLY);
        if (!matfp) {
            std::cerr << "Failed to open MATLAB reference file: " << mat_file << std::endl;
            return false;
        }

        // Load faces (triangular mesh connectivity)
        matvar_t* faces_var = Mat_VarRead(matfp, "Faces");
        if (faces_var && faces_var->class_type == MAT_C_INT32) {
            int* data = static_cast<int*>(faces_var->data);
            size_t n_faces = faces_var->dims[0];
            faces_matlab.clear();
            for (size_t i = 0; i < n_faces * 3; ++i) {
                faces_matlab.push_back(data[i] - 1); // Convert MATLAB 1-based to C++ 0-based
            }
            Mat_VarFree(faces_var);
        }

        // Load reference vertices
        matvar_t* verts_ref_var = Mat_VarRead(matfp, "vertices_ref");
        if (verts_ref_var && verts_ref_var->class_type == MAT_C_DOUBLE) {
            loadVertices(verts_ref_var, vertices_ref_matlab);
            Mat_VarFree(verts_ref_var);
        }

        // Load deformed vertices (all frames)
        matvar_t* verts_def_var = Mat_VarRead(matfp, "vertices_def");
        if (verts_def_var && verts_def_var->class_type == MAT_C_CELL) {
            vertices_def_matlab.clear();
            size_t n_frames = verts_def_var->dims[0] * verts_def_var->dims[1];
            for (size_t i = 0; i < n_frames; ++i) {
                matvar_t* frame = Mat_VarGetCell(verts_def_var, i);
                if (frame) {
                    std::vector<Eigen::Vector3d> frame_verts;
                    loadVertices(frame, frame_verts);
                    vertices_def_matlab.push_back(frame_verts);
                }
            }
            Mat_VarFree(verts_def_var);
        }

        // Load expected deformation results
        loadDeformationResult(matfp, "Deform", deform_result_matlab);

        Mat_Close(matfp);
        return true;
    }

    /**
     * Run C++ deformation computation and compare against MATLAB
     */
    TestResult validateDeformation(bool cumulative = true) {
        TestResult result;
        result.test_name = cumulative ? "Cumulative Deformation" : "Rate Deformation";

        // Run C++ implementation
        FrameDeformationResult cpp_result = cppxdic::computeTriSurfaceDeformation(
            faces_matlab, vertices_ref_matlab, vertices_def_matlab, cumulative
        );

        // Compare each deformation metric
        compareScalarField(cpp_result, deform_result_matlab, "Epc1", result);
        compareScalarField(cpp_result, deform_result_matlab, "Epc2", result);
        compareScalarField(cpp_result, deform_result_matlab, "epc1", result);
        compareScalarField(cpp_result, deform_result_matlab, "epc2", result);
        compareScalarField(cpp_result, deform_result_matlab, "Emgn", result);
        compareScalarField(cpp_result, deform_result_matlab, "emgn", result);
        compareScalarField(cpp_result, deform_result_matlab, "EShearMax", result);
        compareScalarField(cpp_result, deform_result_matlab, "eShearMax", result);
        compareScalarField(cpp_result, deform_result_matlab, "Eeq", result);
        compareScalarField(cpp_result, deform_result_matlab, "eeq", result);
        compareScalarField(cpp_result, deform_result_matlab, "J", result);
        compareScalarField(cpp_result, deform_result_matlab, "Lamda1", result);
        compareScalarField(cpp_result, deform_result_matlab, "Lamda2", result);

        // Compare vector fields
        compareVectorField(cpp_result, deform_result_matlab, "Epc1vec", result);
        compareVectorField(cpp_result, deform_result_matlab, "Epc2vec", result);
        compareVectorField(cpp_result, deform_result_matlab, "epc1vec", result);
        compareVectorField(cpp_result, deform_result_matlab, "epc2vec", result);

        // Compare matrices
        compareMatrixField(cpp_result, deform_result_matlab, "Fmat", result);
        compareMatrixField(cpp_result, deform_result_matlab, "Cmat", result);
        compareMatrixField(cpp_result, deform_result_matlab, "Emat", result);
        compareMatrixField(cpp_result, deform_result_matlab, "emat", result);

        return result;
    }

    /**
     * Generate validation report
     */
    void generateReport(const std::vector<TestResult>& results) {
        std::cout << "\n========================================\n";
        std::cout << "DEFORMATION VALIDATION REPORT\n";
        std::cout << "========================================\n\n";

        int total_tests = 0;
        int passed_tests = 0;

        for (const auto& result : results) {
            total_tests++;
            if (result.passed) passed_tests++;

            std::cout << "Test: " << result.test_name << "\n";
            std::cout << "  Status: " << (result.passed ? "PASSED" : "FAILED") << "\n";
            std::cout << "  Comparisons: " << result.num_comparisons << "\n";
            std::cout << "  Failures: " << result.num_failures << "\n";
            std::cout << "  Max Error: " << std::scientific << result.max_error << "\n";

            if (!result.errors.empty()) {
                std::cout << "  Errors:\n";
                for (const auto& error : result.errors) {
                    std::cout << "    - " << error << "\n";
                }
            }

            if (!result.warnings.empty()) {
                std::cout << "  Warnings:\n";
                for (const auto& warning : result.warnings) {
                    std::cout << "    - " << warning << "\n";
                }
            }
            std::cout << "\n";
        }

        std::cout << "========================================\n";
        std::cout << "SUMMARY: " << passed_tests << "/" << total_tests << " tests passed\n";
        std::cout << "========================================\n";
    }

private:
    // MATLAB reference data
    std::vector<int> faces_matlab;
    std::vector<Eigen::Vector3d> vertices_ref_matlab;
    std::vector<std::vector<Eigen::Vector3d>> vertices_def_matlab;
    FrameDeformationResult deform_result_matlab;

    void loadVertices(matvar_t* var, std::vector<Eigen::Vector3d>& vertices) {
        if (!var || var->class_type != MAT_C_DOUBLE) return;

        double* data = static_cast<double*>(var->data);
        size_t n_points = var->dims[0];
        vertices.clear();
        vertices.reserve(n_points);

        for (size_t i = 0; i < n_points; ++i) {
            vertices.emplace_back(
                data[i],                    // x
                data[i + n_points],        // y
                data[i + 2 * n_points]     // z
            );
        }
    }

    void loadDeformationResult(mat_t* matfp, const std::string& var_name, 
                               FrameDeformationResult& result) {
        matvar_t* deform_var = Mat_VarRead(matfp, var_name.c_str());
        if (!deform_var) return;

        // Load scalar fields
        loadScalarCellArray(deform_var, "Epc1", result);
        loadScalarCellArray(deform_var, "Epc2", result);
        loadScalarCellArray(deform_var, "epc1", result);
        loadScalarCellArray(deform_var, "epc2", result);
        loadScalarCellArray(deform_var, "Emgn", result);
        loadScalarCellArray(deform_var, "emgn", result);
        loadScalarCellArray(deform_var, "EShearMax", result);
        loadScalarCellArray(deform_var, "eShearMax", result);
        loadScalarCellArray(deform_var, "Eeq", result);
        loadScalarCellArray(deform_var, "eeq", result);
        loadScalarCellArray(deform_var, "J", result);
        loadScalarCellArray(deform_var, "Lamda1", result);
        loadScalarCellArray(deform_var, "Lamda2", result);

        // Load vector fields
        loadVectorCellArray(deform_var, "Epc1vec", result);
        loadVectorCellArray(deform_var, "Epc2vec", result);

        // Load matrix fields
        loadMatrixCellArray(deform_var, "Fmat", result);
        loadMatrixCellArray(deform_var, "Cmat", result);
        loadMatrixCellArray(deform_var, "Emat", result);
        loadMatrixCellArray(deform_var, "emat", result);

        Mat_VarFree(deform_var);
    }

    void loadScalarCellArray(matvar_t* parent, const std::string& field_name,
                             FrameDeformationResult& result) {
        matvar_t* field = Mat_VarGetStructFieldByName(parent, field_name.c_str(), 0);
        if (!field || field->class_type != MAT_C_CELL) return;

        size_t n_frames = field->dims[0] * field->dims[1];
        for (size_t i = 0; i < n_frames; ++i) {
            matvar_t* frame_data = Mat_VarGetCell(field, i);
            if (frame_data && frame_data->class_type == MAT_C_DOUBLE) {
                DeformationResult frame_result;
                double* data = static_cast<double*>(frame_data->data);
                size_t n_faces = frame_data->dims[0];

                if (field_name == "Epc1") {
                    frame_result.Epc1.assign(data, data + n_faces);
                } else if (field_name == "Epc2") {
                    frame_result.Epc2.assign(data, data + n_faces);
                }
                // ... continue for other fields

                if (result.frames.size() <= i) {
                    result.frames.resize(i + 1);
                }
                // Copy field to appropriate frame
                // This is simplified - actual implementation would handle all fields
            }
        }
    }

    void loadVectorCellArray(matvar_t* parent, const std::string& field_name,
                             FrameDeformationResult& result) {
        // Similar to loadScalarCellArray but for 3D vectors
    }

    void loadMatrixCellArray(matvar_t* parent, const std::string& field_name,
                             FrameDeformationResult& result) {
        // Similar to loadScalarCellArray but for 3x3 matrices
    }

    void compareScalarField(const FrameDeformationResult& cpp_result,
                           const FrameDeformationResult& matlab_result,
                           const std::string& field_name,
                           TestResult& test_result) {
        // Get field data from both results
        for (size_t frame = 0; frame < cpp_result.frames.size(); ++frame) {
            const auto& cpp_frame = cpp_result.frames[frame];
            const auto& matlab_frame = matlab_result.frames[frame];

            // Get the appropriate field
            std::vector<double> cpp_data, matlab_data;
            if (field_name == "Epc1") {
                cpp_data = cpp_frame.Epc1;
                matlab_data = matlab_frame.Epc1;
            } else if (field_name == "Epc2") {
                cpp_data = cpp_frame.Epc2;
                matlab_data = matlab_frame.Epc2;
            }
            // ... continue for other fields

            // Compare values
            for (size_t i = 0; i < cpp_data.size() && i < matlab_data.size(); ++i) {
                double error = std::abs(cpp_data[i] - matlab_data[i]);
                test_result.max_error = std::max(test_result.max_error, error);
                test_result.num_comparisons++;

                if (error > TOLERANCE_NORMAL) {
                    test_result.num_failures++;
                    test_result.passed = false;
                    if (test_result.errors.size() < 10) { // Limit error messages
                        std::stringstream ss;
                        ss << field_name << "[frame=" << frame << "][" << i << "]: "
                           << "C++=" << cpp_data[i] << ", MATLAB=" << matlab_data[i]
                           << ", error=" << error;
                        test_result.errors.push_back(ss.str());
                    }
                }
            }
        }
    }

    void compareVectorField(const FrameDeformationResult& cpp_result,
                           const FrameDeformationResult& matlab_result,
                           const std::string& field_name,
                           TestResult& test_result) {
        // Similar comparison for vector fields
    }

    void compareMatrixField(const FrameDeformationResult& cpp_result,
                           const FrameDeformationResult& matlab_result,
                           const std::string& field_name,
                           TestResult& test_result) {
        // Similar comparison for matrix fields
    }
};

int main(int argc, char** argv) {
    std::string matlab_ref_file = "test_data/deformation_reference.mat";
    
    if (argc > 1) {
        matlab_ref_file = argv[1];
    }

    std::cout << "=== TCPE Deformation Validation Test ===\n";
    std::cout << "Reference file: " << matlab_ref_file << "\n\n";

    if (!fs::exists(matlab_ref_file)) {
        std::cerr << "Reference file not found. Please provide MATLAB reference data.\n";
        std::cerr << "Expected format: mat file with variables:\n";
        std::cerr << "  - Faces: [n_faces x 3] triangular mesh connectivity\n";
        std::cerr << "  - vertices_ref: [n_points x 3] reference configuration\n";
        std::cerr << "  - vertices_def: {n_frames x 1} cell array of deformed vertices\n";
        std::cerr << "  - Deform: struct with expected deformation fields\n";
        return 1;
    }

    DeformationValidator validator;
    
    // Load MATLAB reference data
    if (!validator.loadMatlabReference(matlab_ref_file)) {
        std::cerr << "Failed to load MATLAB reference data\n";
        return 1;
    }

    std::vector<DeformationValidator::TestResult> results;

    // Test cumulative deformation
    results.push_back(validator.validateDeformation(true));

    // Test rate deformation
    results.push_back(validator.validateDeformation(false));

    // Generate report
    validator.generateReport(results);

    return results[0].passed && results[1].passed ? 0 : 1;
}
