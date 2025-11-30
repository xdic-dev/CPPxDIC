/**
 * Test for var_test2.mat - StepD Workflow Intermediate Outputs
 * 
 * This test loads var_test2.mat and verifies the intermediate outputs
 * from step_d_workflow.cpp:
 * - cfr1: first image from cam_first_raw (uint8 2d array 1216x1936)
 * - cfs1: first image from cam_first_satur after performSaturation (uint8 2d array 1216x1936)
 * - cf1: first image from cam_first after applyImageFiltering (float64 2d array 1216x1936)
 * - refmask_REF: reference mask (uint8 2d array 1216x1936)
 * - refmask_trial: trial mask (uint8 2d array 1216x1936)
 * - refmask_trial_matched: matched trial mask from performMatching (uint8 2d array 1216x1936)
 * - initial_seed_point_set1: seed point struct with pw and sw fields
 * - initial_seed_point_set2: seed point struct with pw and sw fields
 * - U_mapped, V_mapped: displacement maps (float64 2d array 111x176)
 * - gsboundaries: grayscale boundaries (float64 1d array 2)
 * - analysis_direction: string
 */

#include <iostream>
#include <string>
#include <vector>
#include <iomanip>
#include <matio.h>
#include <opencv2/opencv.hpp>
#include <filesystem>

namespace fs = std::filesystem;

// Helper to get type string
const char* matioClassToStr(matio_classes class_type) {
    switch (class_type) {
        case MAT_C_EMPTY: return "EMPTY";
        case MAT_C_CELL: return "CELL";
        case MAT_C_STRUCT: return "STRUCT";
        case MAT_C_OBJECT: return "OBJECT";
        case MAT_C_CHAR: return "CHAR";
        case MAT_C_SPARSE: return "SPARSE";
        case MAT_C_DOUBLE: return "DOUBLE";
        case MAT_C_SINGLE: return "SINGLE";
        case MAT_C_INT8: return "INT8";
        case MAT_C_UINT8: return "UINT8";
        case MAT_C_INT16: return "INT16";
        case MAT_C_UINT16: return "UINT16";
        case MAT_C_INT32: return "INT32";
        case MAT_C_UINT32: return "UINT32";
        case MAT_C_INT64: return "INT64";
        case MAT_C_UINT64: return "UINT64";
        case MAT_C_FUNCTION: return "FUNCTION";
        case MAT_C_OPAQUE: return "OPAQUE";
        default: return "UNKNOWN";
    }
}

// Read cv::Mat from matvar
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
                // MATLAB stores column-major
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

// Read struct field as array
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

// Read string from matvar
std::string readString(matvar_t* var) {
    if (!var || !var->data) return "";
    
    if (var->data_type == MAT_T_UINT16) {
        uint16_t* data = static_cast<uint16_t*>(var->data);
        size_t len = 1;
        for (int i = 0; i < var->rank; ++i) {
            len *= var->dims[i];
        }
        std::string result;
        for (size_t i = 0; i < len; ++i) {
            if (data[i] == 0) break;
            result += static_cast<char>(data[i]);
        }
        return result;
    } else if (var->data_type == MAT_T_UTF8 || var->class_type == MAT_C_CHAR) {
        char* data = static_cast<char*>(var->data);
        size_t len = 1;
        for (int i = 0; i < var->rank; ++i) {
            len *= var->dims[i];
        }
        return std::string(data, len);
    }
    
    return "";
}

// Test result structure
struct TestResult {
    bool passed = true;
    std::vector<std::string> errors;
    std::vector<std::string> info;
    
    void addError(const std::string& msg) {
        errors.push_back(msg);
        passed = false;
    }
    
    void addInfo(const std::string& msg) {
        info.push_back(msg);
    }
    
    void print() const {
        std::cout << "\n========================================\n";
        std::cout << "TEST RESULT: " << (passed ? "PASSED" : "FAILED") << "\n";
        std::cout << "========================================\n";
        
        if (!info.empty()) {
            std::cout << "\nINFO:\n";
            for (const auto& msg : info) {
                std::cout << "  [INFO] " << msg << "\n";
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

int main(int argc, char** argv) {
    std::string mat_path;
    
    if (argc > 1) {
        mat_path = argv[1];
    } else {
        // Default path
        mat_path = "test_output/var_test2.mat";
    }
    
    std::cout << "=== var_test2.mat Test ===\n";
    std::cout << "Loading: " << mat_path << "\n\n";
    
    if (!fs::exists(mat_path)) {
        std::cerr << "File not found: " << mat_path << "\n";
        return 1;
    }
    
    mat_t* matfp = Mat_Open(mat_path.c_str(), MAT_ACC_RDONLY);
    if (!matfp) {
        std::cerr << "Failed to open MAT file: " << mat_path << "\n";
        return 1;
    }
    
    TestResult result;
    
    // Expected variables and their properties
    struct VarSpec {
        const char* name;
        matio_classes expected_class;
        int expected_rank;
        size_t expected_dims[2];
        bool is_struct;
    };
    
    std::vector<VarSpec> expected_vars = {
        {"cfr1", MAT_C_UINT8, 2, {1216, 1936}, false},
        {"cfs1", MAT_C_UINT8, 2, {1216, 1936}, false},
        {"cf1", MAT_C_DOUBLE, 2, {1216, 1936}, false},
        {"refmask_REF", MAT_C_UINT8, 2, {1216, 1936}, false},
        {"refmask_trial", MAT_C_UINT8, 2, {1216, 1936}, false},
        {"refmask_trial_matched", MAT_C_UINT8, 2, {1216, 1936}, false},
        {"U_mapped", MAT_C_DOUBLE, 2, {111, 176}, false},
        {"V_mapped", MAT_C_DOUBLE, 2, {111, 176}, false},
        {"gsboundaries", MAT_C_DOUBLE, 2, {2, 1}, false},
        {"initial_seed_point_set1", MAT_C_STRUCT, 2, {1, 1}, true},
        {"initial_seed_point_set2", MAT_C_STRUCT, 2, {1, 1}, true},
    };
    
    // Check each expected variable
    for (const auto& spec : expected_vars) {
        matvar_t* var = Mat_VarRead(matfp, spec.name);
        
        if (!var) {
            result.addError(std::string(spec.name) + ": Variable not found");
            continue;
        }
        
        std::string var_info = std::string(spec.name) + ": class=" + 
                               matioClassToStr(var->class_type) + ", dims=[";
        for (int i = 0; i < var->rank; ++i) {
            if (i > 0) var_info += "x";
            var_info += std::to_string(var->dims[i]);
        }
        var_info += "]";
        result.addInfo(var_info);
        
        // Check class type
        if (var->class_type != spec.expected_class) {
            result.addError(std::string(spec.name) + ": Expected class " + 
                           matioClassToStr(spec.expected_class) + ", got " + 
                           matioClassToStr(var->class_type));
        }
        
        // Check dimensions
        if (var->rank != spec.expected_rank) {
            result.addError(std::string(spec.name) + ": Expected rank " + 
                           std::to_string(spec.expected_rank) + ", got " + 
                           std::to_string(var->rank));
        } else {
            for (int i = 0; i < spec.expected_rank; ++i) {
                if (var->dims[i] != spec.expected_dims[i]) {
                    result.addError(std::string(spec.name) + ": Dimension[" + 
                                   std::to_string(i) + "] expected " + 
                                   std::to_string(spec.expected_dims[i]) + ", got " + 
                                   std::to_string(var->dims[i]));
                }
            }
        }
        
        // For structs, check fields
        if (spec.is_struct && var->class_type == MAT_C_STRUCT) {
            // Check for pw and sw fields
            matvar_t* pw = Mat_VarGetStructFieldByName(var, "pw", 0);
            matvar_t* sw = Mat_VarGetStructFieldByName(var, "sw", 0);
            
            if (!pw) {
                result.addError(std::string(spec.name) + ": Missing 'pw' field");
            } else {
                auto pw_vals = readStructFieldArray<int>(var, "pw");
                result.addInfo(std::string(spec.name) + ".pw = [" + 
                              std::to_string(pw_vals[0]) + ", " + 
                              std::to_string(pw_vals[1]) + "]");
            }
            
            if (!sw) {
                result.addError(std::string(spec.name) + ": Missing 'sw' field");
            } else {
                auto sw_vals = readStructFieldArray<int>(var, "sw");
                result.addInfo(std::string(spec.name) + ".sw = [" + 
                              std::to_string(sw_vals[0]) + ", " + 
                              std::to_string(sw_vals[1]) + "]");
            }
        }
        
        Mat_VarFree(var);
    }
    
    // Check analysis_direction string
    matvar_t* analysis_dir = Mat_VarRead(matfp, "analysis_direction");
    if (analysis_dir) {
        std::string dir_str = readString(analysis_dir);
        result.addInfo("analysis_direction = \"" + dir_str + "\"");
        Mat_VarFree(analysis_dir);
    } else {
        result.addError("analysis_direction: Variable not found");
    }
    
    // Load and display some statistics for the image data
    std::cout << "\n--- Image Statistics ---\n";
    
    // cfr1 (raw image)
    matvar_t* cfr1 = Mat_VarRead(matfp, "cfr1");
    if (cfr1) {
        cv::Mat img = readMatFromVar(cfr1);
        if (!img.empty()) {
            double minVal, maxVal;
            cv::minMaxLoc(img, &minVal, &maxVal);
            std::cout << "cfr1 (raw): min=" << minVal << ", max=" << maxVal 
                      << ", size=" << img.rows << "x" << img.cols << "\n";
        }
        Mat_VarFree(cfr1);
    }
    
    // cfs1 (saturated image)
    matvar_t* cfs1 = Mat_VarRead(matfp, "cfs1");
    if (cfs1) {
        cv::Mat img = readMatFromVar(cfs1);
        if (!img.empty()) {
            double minVal, maxVal;
            cv::minMaxLoc(img, &minVal, &maxVal);
            std::cout << "cfs1 (saturated): min=" << minVal << ", max=" << maxVal 
                      << ", size=" << img.rows << "x" << img.cols << "\n";
        }
        Mat_VarFree(cfs1);
    }
    
    // cf1 (filtered image)
    matvar_t* cf1 = Mat_VarRead(matfp, "cf1");
    if (cf1) {
        cv::Mat img = readMatFromVar(cf1);
        if (!img.empty()) {
            double minVal, maxVal;
            cv::minMaxLoc(img, &minVal, &maxVal);
            std::cout << "cf1 (filtered): min=" << minVal << ", max=" << maxVal 
                      << ", size=" << img.rows << "x" << img.cols << "\n";
        }
        Mat_VarFree(cf1);
    }
    
    // Masks
    std::cout << "\n--- Mask Statistics ---\n";
    for (const char* mask_name : {"refmask_REF", "refmask_trial", "refmask_trial_matched"}) {
        matvar_t* mask_var = Mat_VarRead(matfp, mask_name);
        if (mask_var) {
            cv::Mat mask = readMatFromVar(mask_var);
            if (!mask.empty()) {
                int nonzero = cv::countNonZero(mask);
                double pct = 100.0 * nonzero / (mask.rows * mask.cols);
                std::cout << mask_name << ": " << nonzero << " non-zero pixels (" 
                          << std::fixed << std::setprecision(2) << pct << "%)\n";
            }
            Mat_VarFree(mask_var);
        }
    }
    
    // Displacement maps
    std::cout << "\n--- Displacement Map Statistics ---\n";
    for (const char* disp_name : {"U_mapped", "V_mapped"}) {
        matvar_t* disp_var = Mat_VarRead(matfp, disp_name);
        if (disp_var) {
            cv::Mat disp = readMatFromVar(disp_var);
            if (!disp.empty()) {
                double minVal, maxVal;
                cv::minMaxLoc(disp, &minVal, &maxVal);
                cv::Scalar mean = cv::mean(disp);
                std::cout << disp_name << ": min=" << std::fixed << std::setprecision(4) 
                          << minVal << ", max=" << maxVal 
                          << ", mean=" << mean[0] 
                          << ", size=" << disp.rows << "x" << disp.cols << "\n";
            }
            Mat_VarFree(disp_var);
        }
    }
    
    // gsboundaries
    matvar_t* gsb = Mat_VarRead(matfp, "gsboundaries");
    if (gsb && gsb->data) {
        double* data = static_cast<double*>(gsb->data);
        std::cout << "\ngsboundaries: [" << data[0] << ", " << data[1] << "]\n";
        Mat_VarFree(gsb);
    }
    
    Mat_Close(matfp);
    
    result.print();
    
    return result.passed ? 0 : 1;
}
