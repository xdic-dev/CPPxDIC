/**
 * MAT File Comparator Test
 * 
 * Compares C++ generated MAT files with MATLAB reference files
 * Validates structure, data types, dimensions, and values
 */

#include <iostream>
#include <string>
#include <vector>
#include <cmath>
#include <matio.h>
#include <filesystem>
#include <sstream>
#include <algorithm>

namespace fs = std::filesystem;

// Helper functions for matio enum to string conversion
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

const char* matioTypeToStr(matio_types data_type) {
    switch (data_type) {
        case MAT_T_UNKNOWN: return "UNKNOWN";
        case MAT_T_INT8: return "INT8";
        case MAT_T_UINT8: return "UINT8";
        case MAT_T_INT16: return "INT16";
        case MAT_T_UINT16: return "UINT16";
        case MAT_T_INT32: return "INT32";
        case MAT_T_UINT32: return "UINT32";
        case MAT_T_SINGLE: return "SINGLE";
        case MAT_T_DOUBLE: return "DOUBLE";
        case MAT_T_INT64: return "INT64";
        case MAT_T_UINT64: return "UINT64";
        case MAT_T_MATRIX: return "MATRIX";
        case MAT_T_COMPRESSED: return "COMPRESSED";
        case MAT_T_UTF8: return "UTF8";
        case MAT_T_UTF16: return "UTF16";
        case MAT_T_UTF32: return "UTF32";
        case MAT_T_STRING: return "STRING";
        case MAT_T_CELL: return "CELL";
        case MAT_T_STRUCT: return "STRUCT";
        case MAT_T_ARRAY: return "ARRAY";
        case MAT_T_FUNCTION: return "FUNCTION";
        default: return "UNKNOWN";
    }
}

// Comparison result structure
struct ComparisonResult {
    bool passed = true;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    std::vector<std::string> info;
    
    void addError(const std::string& msg) {
        errors.push_back(msg);
        passed = false;
    }
    
    void addWarning(const std::string& msg) {
        warnings.push_back(msg);
    }
    
    void addInfo(const std::string& msg) {
        info.push_back(msg);
    }
    
    void print() const {
        std::cout << "\n========================================\n";
        std::cout << "COMPARISON RESULT: " << (passed ? "PASSED" : "FAILED") << "\n";
        std::cout << "========================================\n";
        
        if (!info.empty()) {
            std::cout << "\nINFO (" << info.size() << "):\n";
            for (const auto& msg : info) {
                std::cout << "  ℹ️  " << msg << "\n";
            }
        }
        
        if (!warnings.empty()) {
            std::cout << "\nWARNINGS (" << warnings.size() << "):\n";
            for (const auto& msg : warnings) {
                std::cout << "  ⚠️  " << msg << "\n";
            }
        }
        
        if (!errors.empty()) {
            std::cout << "\nERRORS (" << errors.size() << "):\n";
            for (const auto& msg : errors) {
                std::cout << "  ❌ " << msg << "\n";
            }
        }
        
        std::cout << "\n========================================\n";
    }
};

// Helper to get variable info string
std::string getVarInfo(matvar_t* var) {
    if (!var) return "NULL";
    
    std::stringstream ss;
    ss << "class=" << matioClassToStr(var->class_type);
    ss << ", type=" << matioTypeToStr(var->data_type);
    ss << ", rank=" << var->rank;
    ss << ", dims=[";
    for (int i = 0; i < var->rank; ++i) {
        if (i > 0) ss << "x";
        ss << var->dims[i];
    }
    ss << "]";
    
    if (var->class_type == MAT_C_STRUCT) {
        ss << ", nfields=" << Mat_VarGetNumberOfFields(var);
    } else if (var->class_type == MAT_C_CELL) {
        size_t total = 1;
        for (int i = 0; i < var->rank; ++i) {
            total *= var->dims[i];
        }
        ss << ", ncells=" << total;
    }
    
    return ss.str();
}

// Helper to get string from char array
std::string getStringFromVar(matvar_t* var) {
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
    }
    
    return "";
}

// Compare dimensions - returns: 0=match, 1=mismatch but can compare first element, -1=cannot compare
int compareDimensionsEx(matvar_t* ref, matvar_t* test, ComparisonResult& result, const std::string& path) {
    bool has_mismatch = false;
    bool can_compare_first = true;
    
    if (ref->rank != test->rank) {
        result.addError(path + ": Rank mismatch (ref=" + std::to_string(ref->rank) + 
                       ", test=" + std::to_string(test->rank) + ")");
        has_mismatch = true;
    }
    
    int min_rank = std::min(ref->rank, test->rank);
    for (int i = 0; i < min_rank; ++i) {
        if (ref->dims[i] != test->dims[i]) {
            result.addError(path + ": Dimension[" + std::to_string(i) + "] mismatch (ref=" + 
                           std::to_string(ref->dims[i]) + ", test=" + std::to_string(test->dims[i]) + ")");
            has_mismatch = true;
        }
        // Check if either dimension is 0 - cannot compare first element
        if (ref->dims[i] == 0 || test->dims[i] == 0) {
            can_compare_first = false;
        }
    }
    
    // Check remaining dimensions for zeros
    for (int i = min_rank; i < ref->rank; ++i) {
        if (ref->dims[i] == 0) can_compare_first = false;
    }
    for (int i = min_rank; i < test->rank; ++i) {
        if (test->dims[i] == 0) can_compare_first = false;
    }
    
    if (!has_mismatch) return 0;  // Match
    return can_compare_first ? 1 : -1;  // Mismatch but can/cannot compare first element
}

// Compare dimensions (legacy wrapper)
bool compareDimensions(matvar_t* ref, matvar_t* test, ComparisonResult& result, const std::string& path) {
    return compareDimensionsEx(ref, test, result, path) == 0;
}

// Compare scalar values with tolerance
bool compareScalars(matvar_t* ref, matvar_t* test, ComparisonResult& result, 
                   const std::string& path, double tolerance = 1e-6) {
    if (!ref->data || !test->data) {
        if (ref->data != test->data) {
            result.addError(path + ": Data pointer mismatch (one is NULL)");
            return false;
        }
        return true;
    }
    
    // Compare based on data type
    if (ref->data_type == MAT_T_DOUBLE && test->data_type == MAT_T_DOUBLE) {
        double* ref_data = static_cast<double*>(ref->data);
        double* test_data = static_cast<double*>(test->data);
        
        size_t n_elements = 1;
        for (int i = 0; i < ref->rank; ++i) {
            n_elements *= ref->dims[i];
        }
        
        size_t mismatches = 0;
        double max_diff = 0.0;
        
        for (size_t i = 0; i < n_elements; ++i) {
            double diff = std::abs(ref_data[i] - test_data[i]);
            if (diff > tolerance) {
                mismatches++;
                max_diff = std::max(max_diff, diff);
            }
        }
        
        if (mismatches > 0) {
            double mismatch_pct = 100.0 * mismatches / n_elements;
            if (mismatch_pct > 1.0) {
                result.addError(path + ": " + std::to_string(mismatches) + "/" + 
                               std::to_string(n_elements) + " elements differ (max_diff=" + 
                               std::to_string(max_diff) + ")");
                return false;
            } else {
                result.addWarning(path + ": " + std::to_string(mismatches) + "/" + 
                                 std::to_string(n_elements) + " elements differ (max_diff=" + 
                                 std::to_string(max_diff) + ")");
            }
        }
    } else if (ref->data_type == MAT_T_INT32 && test->data_type == MAT_T_INT32) {
        int32_t* ref_data = static_cast<int32_t*>(ref->data);
        int32_t* test_data = static_cast<int32_t*>(test->data);
        
        size_t n_elements = 1;
        for (int i = 0; i < ref->rank; ++i) {
            n_elements *= ref->dims[i];
        }
        
        for (size_t i = 0; i < n_elements; ++i) {
            if (ref_data[i] != test_data[i]) {
                result.addError(path + ": Integer mismatch at index " + std::to_string(i) + 
                               " (ref=" + std::to_string(ref_data[i]) + ", test=" + 
                               std::to_string(test_data[i]) + ")");
                return false;
            }
        }
    }
    
    return true;
}

// Forward declaration for recursive comparison
bool compareVariables(matvar_t* ref, matvar_t* test, ComparisonResult& result, 
                     const std::string& path, double tolerance);

// Compare struct variables
bool compareStructs(matvar_t* ref, matvar_t* test, ComparisonResult& result, 
                   const std::string& path, double tolerance) {
    int n_fields_ref = Mat_VarGetNumberOfFields(ref);
    int n_fields_test = Mat_VarGetNumberOfFields(test);
    
    // Get field names from both
    char* const* ref_field_names = Mat_VarGetStructFieldnames(ref);
    char* const* test_field_names = Mat_VarGetStructFieldnames(test);
    
    // Build sets of field names
    std::vector<std::string> ref_fields, test_fields;
    for (int i = 0; i < n_fields_ref; ++i) {
        ref_fields.push_back(ref_field_names[i]);
    }
    for (int i = 0; i < n_fields_test; ++i) {
        test_fields.push_back(test_field_names[i]);
    }
    
    // Find missing fields (in ref but not in test)
    std::vector<std::string> missing_fields;
    for (const auto& field : ref_fields) {
        if (std::find(test_fields.begin(), test_fields.end(), field) == test_fields.end()) {
            missing_fields.push_back(field);
        }
    }
    
    // Find extra fields (in test but not in ref)
    std::vector<std::string> extra_fields;
    for (const auto& field : test_fields) {
        if (std::find(ref_fields.begin(), ref_fields.end(), field) == ref_fields.end()) {
            extra_fields.push_back(field);
        }
    }
    
    // Report field count mismatch with details
    if (n_fields_ref != n_fields_test) {
        std::string msg = path + ": Number of fields mismatch (ref=" + 
                         std::to_string(n_fields_ref) + ", test=" + std::to_string(n_fields_test) + ")";
        if (!missing_fields.empty()) {
            msg += " | Missing in test: [";
            for (size_t i = 0; i < missing_fields.size(); ++i) {
                if (i > 0) msg += ", ";
                msg += missing_fields[i];
            }
            msg += "]";
        }
        if (!extra_fields.empty()) {
            msg += " | Extra in test: [";
            for (size_t i = 0; i < extra_fields.size(); ++i) {
                if (i > 0) msg += ", ";
                msg += extra_fields[i];
            }
            msg += "]";
        }
        result.addError(msg);
    }
    
    // Compare common fields
    for (const auto& field_name : ref_fields) {
        // Skip fields that are missing in test
        if (std::find(test_fields.begin(), test_fields.end(), field_name) == test_fields.end()) {
            continue;
        }
        
        std::string field_path = path + "/" + field_name;
        
        matvar_t* ref_field = Mat_VarGetStructFieldByName(ref, field_name.c_str(), 0);
        matvar_t* test_field = Mat_VarGetStructFieldByName(test, field_name.c_str(), 0);
        
        if (!ref_field || !test_field) {
            result.addWarning(field_path + ": Could not retrieve field from one of the structs");
            continue;
        }
        
        compareVariables(ref_field, test_field, result, field_path, tolerance);
    }
    
    return result.passed;
}

// Compare cell array variables
bool compareCellArrays(matvar_t* ref, matvar_t* test, ComparisonResult& result, 
                      const std::string& path, double tolerance) {
    size_t n_cells_ref = 1;
    size_t n_cells_test = 1;
    
    for (int i = 0; i < ref->rank; ++i) {
        n_cells_ref *= ref->dims[i];
    }
    for (int i = 0; i < test->rank; ++i) {
        n_cells_test *= test->dims[i];
    }
    
    if (n_cells_ref != n_cells_test) {
        result.addError(path + ": Number of cells mismatch (ref=" + 
                       std::to_string(n_cells_ref) + ", test=" + std::to_string(n_cells_test) + ")");
        return false;
    }
    
    // Compare each cell
    for (size_t i = 0; i < n_cells_ref; ++i) {
        std::string cell_path = path + "{" + std::to_string(i) + "}";
        
        matvar_t* ref_cell = Mat_VarGetCell(ref, i);
        matvar_t* test_cell = Mat_VarGetCell(test, i);
        
        if (!ref_cell && !test_cell) {
            continue;
        }
        
        if (!ref_cell || !test_cell) {
            result.addError(cell_path + ": Cell exists in one file but not the other");
            continue;
        }
        
        compareVariables(ref_cell, test_cell, result, cell_path, tolerance);
    }
    
    return result.passed;
}

// Main variable comparison function
bool compareVariables(matvar_t* ref, matvar_t* test, ComparisonResult& result, 
                     const std::string& path, double tolerance = 1e-6) {
    if (!ref || !test) {
        if (ref != test) {
            result.addError(path + ": One variable is NULL");
            return false;
        }
        return true;
    }
    
    // Compare class type
    if (ref->class_type != test->class_type) {
        result.addError(path + ": Class type mismatch (ref=" + 
                       std::string(matioClassToStr(ref->class_type)) + ", test=" + 
                       std::string(matioClassToStr(test->class_type)) + ")");
        return false;
    }
    
    // Compare data type (allow some flexibility)
    if (ref->data_type != test->data_type) {
        result.addWarning(path + ": Data type mismatch (ref=" + 
                         std::string(matioTypeToStr(ref->data_type)) + ", test=" + 
                         std::string(matioTypeToStr(test->data_type)) + ")");
    }
    
    // Compare dimensions
    int dim_result = compareDimensionsEx(ref, test, result, path);
    if (dim_result != 0) {
        // Dimension mismatch - try to compare first element if possible
        if (dim_result == 1) {
            result.addInfo(path + ": Attempting to compare first element despite dimension mismatch");
            // For numeric types, try to compare first element
            if (ref->class_type == MAT_C_DOUBLE || ref->class_type == MAT_C_SINGLE ||
                ref->class_type == MAT_C_INT32 || ref->class_type == MAT_C_UINT32) {
                if (ref->data && test->data) {
                    if (ref->data_type == MAT_T_DOUBLE && test->data_type == MAT_T_DOUBLE) {
                        double ref_val = *static_cast<double*>(ref->data);
                        double test_val = *static_cast<double*>(test->data);
                        double diff = std::abs(ref_val - test_val);
                        if (diff <= tolerance) {
                            result.addInfo(path + ": First element matches (ref=" + std::to_string(ref_val) + 
                                          ", test=" + std::to_string(test_val) + ")");
                        } else {
                            result.addError(path + ": First element differs (ref=" + std::to_string(ref_val) + 
                                           ", test=" + std::to_string(test_val) + ", diff=" + std::to_string(diff) + ")");
                        }
                    } else if (ref->data_type == MAT_T_INT32 && test->data_type == MAT_T_INT32) {
                        int32_t ref_val = *static_cast<int32_t*>(ref->data);
                        int32_t test_val = *static_cast<int32_t*>(test->data);
                        if (ref_val == test_val) {
                            result.addInfo(path + ": First element matches (value=" + std::to_string(ref_val) + ")");
                        } else {
                            result.addError(path + ": First element differs (ref=" + std::to_string(ref_val) + 
                                           ", test=" + std::to_string(test_val) + ")");
                        }
                    }
                }
            }
        }
        return false;
    }
    
    // Type-specific comparison
    switch (ref->class_type) {
        case MAT_C_STRUCT:
            return compareStructs(ref, test, result, path, tolerance);
            
        case MAT_C_CELL:
            return compareCellArrays(ref, test, result, path, tolerance);
            
        case MAT_C_DOUBLE:
        case MAT_C_SINGLE:
        case MAT_C_INT32:
        case MAT_C_UINT32:
        case MAT_C_INT16:
        case MAT_C_UINT16:
        case MAT_C_INT8:
        case MAT_C_UINT8:
            return compareScalars(ref, test, result, path, tolerance);
            
        case MAT_C_CHAR:
            // String comparison
            {
                std::string ref_str = getStringFromVar(ref);
                std::string test_str = getStringFromVar(test);
                if (ref_str != test_str) {
                    result.addError(path + ": String mismatch (ref=\"" + ref_str + 
                                   "\", test=\"" + test_str + "\")");
                    return false;
                }
            }
            break;
            
        default:
            result.addWarning(path + ": Unsupported class type for comparison: " + 
                             std::string(matioClassToStr(ref->class_type)));
            break;
    }
    
    return result.passed;
}

// Compare two MAT files
ComparisonResult compareMatFiles(const std::string& ref_path, const std::string& test_path, 
                                 double tolerance = 1e-6) {
    ComparisonResult result;
    
    // Open reference file
    mat_t* ref_mat = Mat_Open(ref_path.c_str(), MAT_ACC_RDONLY);
    if (!ref_mat) {
        result.addError("Failed to open reference file: " + ref_path);
        return result;
    }
    
    // Open test file
    mat_t* test_mat = Mat_Open(test_path.c_str(), MAT_ACC_RDONLY);
    if (!test_mat) {
        result.addError("Failed to open test file: " + test_path);
        Mat_Close(ref_mat);
        return result;
    }
    
    result.addInfo("Comparing: " + ref_path + " vs " + test_path);
    result.addInfo("Tolerance: " + std::to_string(tolerance));
    
    // Get all variables from reference file
    matvar_t* ref_var = nullptr;
    std::vector<std::string> ref_var_names;
    
    while ((ref_var = Mat_VarReadNext(ref_mat)) != nullptr) {
        ref_var_names.push_back(ref_var->name);
        Mat_VarFree(ref_var);
    }
    
    Mat_Rewind(ref_mat);
    
    result.addInfo("Reference file has " + std::to_string(ref_var_names.size()) + " top-level variables");
    
    // Compare each variable
    for (const auto& var_name : ref_var_names) {
        matvar_t* ref_var = Mat_VarRead(ref_mat, var_name.c_str());
        matvar_t* test_var = Mat_VarRead(test_mat, var_name.c_str());
        
        if (!test_var) {
            result.addError("Variable '" + var_name + "' missing in test file");
            Mat_VarFree(ref_var);
            continue;
        }
        
        result.addInfo("Comparing variable: " + var_name + " [" + getVarInfo(ref_var) + "]");
        
        compareVariables(ref_var, test_var, result, var_name, tolerance);
        
        Mat_VarFree(ref_var);
        Mat_VarFree(test_var);
    }
    
    // Check for extra variables in test file
    Mat_Rewind(test_mat);
    matvar_t* test_var = nullptr;
    while ((test_var = Mat_VarReadNext(test_mat)) != nullptr) {
        std::string test_name = test_var->name;
        if (std::find(ref_var_names.begin(), ref_var_names.end(), test_name) == ref_var_names.end()) {
            result.addWarning("Extra variable in test file: " + test_name);
        }
        Mat_VarFree(test_var);
    }
    
    Mat_Close(ref_mat);
    Mat_Close(test_mat);
    
    return result;
}

// Main test function
int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <reference_mat> <test_mat> [tolerance]\n";
        std::cerr << "\nExample:\n";
        std::cerr << "  " << argv[0] << " reference/DIC3DPPresults.mat output/DIC3DPPresults.mat 1e-6\n";
        return 1;
    }
    
    std::string ref_path = argv[1];
    std::string test_path = argv[2];
    double tolerance = (argc > 3) ? std::stod(argv[3]) : 1e-6;
    
    // Check files exist
    if (!fs::exists(ref_path)) {
        std::cerr << "Reference file not found: " << ref_path << "\n";
        return 1;
    }
    
    if (!fs::exists(test_path)) {
        std::cerr << "Test file not found: " << test_path << "\n";
        return 1;
    }
    
    std::cout << "MAT File Comparator\n";
    std::cout << "==================\n";
    std::cout << "Reference: " << ref_path << "\n";
    std::cout << "Test:      " << test_path << "\n";
    std::cout << "Tolerance: " << tolerance << "\n";
    
    ComparisonResult result = compareMatFiles(ref_path, test_path, tolerance);
    result.print();
    
    return result.passed ? 0 : 1;
}
