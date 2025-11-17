/**
 * MAT File Reader Implementation
 */

#include "mat_reader.h"
#include <iostream>
#include <sstream>
#include <cstring>

namespace cppxdic {

matvar_t* MatReader::readStructField(mat_t* matfp, 
                                     const std::string& struct_name,
                                     const std::string& field_name) {
    if (!matfp) return nullptr;
    
    // Read struct variable
    matvar_t* struct_var = Mat_VarRead(matfp, struct_name.c_str());
    if (!struct_var || struct_var->class_type != MAT_C_STRUCT) {
        if (struct_var) Mat_VarFree(struct_var);
        return nullptr;
    }
    
    // Get field
    matvar_t* field = Mat_VarGetStructFieldByName(struct_var, field_name.c_str(), 0);
    matvar_t* result = nullptr;
    if (field) {
        result = Mat_VarDuplicate(field, 1);  // Deep copy
    }
    
    Mat_VarFree(struct_var);
    return result;
}

matvar_t* MatReader::readNestedField(mat_t* matfp, const std::string& path) {
    if (!matfp) return nullptr;
    
    // Split path by '.'
    std::vector<std::string> parts;
    std::istringstream iss(path);
    std::string part;
    while (std::getline(iss, part, '.')) {
        parts.push_back(part);
    }
    
    if (parts.empty()) return nullptr;
    
    // Read root variable
    matvar_t* current = Mat_VarRead(matfp, parts[0].c_str());
    if (!current) return nullptr;
    
    // Navigate through nested fields
    for (size_t i = 1; i < parts.size(); ++i) {
        if (current->class_type != MAT_C_STRUCT) {
            Mat_VarFree(current);
            return nullptr;
        }
        
        matvar_t* field = Mat_VarGetStructFieldByName(current, parts[i].c_str(), 0);
        if (!field) {
            Mat_VarFree(current);
            return nullptr;
        }
        
        matvar_t* temp = Mat_VarDuplicate(field, 1);
        Mat_VarFree(current);
        current = temp;
    }
    
    return current;
}

std::vector<double> MatReader::readDoubleArray(matvar_t* var) {
    std::vector<double> result;
    if (!var || !var->data) return result;
    
    if (var->class_type != MAT_C_DOUBLE) {
        std::cerr << "Warning: Variable is not double type" << std::endl;
        return result;
    }
    
    size_t n_elements = 1;
    for (int i = 0; i < var->rank; ++i) {
        n_elements *= var->dims[i];
    }
    
    const double* data = static_cast<const double*>(var->data);
    result.assign(data, data + n_elements);
    
    return result;
}

std::vector<double> MatReader::readDouble2DArray(matvar_t* var, size_t& rows, size_t& cols) {
    std::vector<double> result;
    rows = 0;
    cols = 0;
    
    if (!var || !var->data || var->rank < 2) return result;
    
    if (var->class_type != MAT_C_DOUBLE) {
        std::cerr << "Warning: Variable is not double type" << std::endl;
        return result;
    }
    
    rows = var->dims[0];
    cols = var->dims[1];
    
    const double* data = static_cast<const double*>(var->data);
    size_t n_elements = rows * cols;
    
    // MATLAB stores in column-major, convert to row-major
    result.resize(n_elements);
    for (size_t r = 0; r < rows; ++r) {
        for (size_t c = 0; c < cols; ++c) {
            result[r * cols + c] = data[c * rows + r];
        }
    }
    
    return result;
}

cv::Mat MatReader::readImage(matvar_t* var) {
    if (!var || !var->data) return cv::Mat();
    
    if (var->rank != 2) {
        std::cerr << "Warning: Expected 2D array for image" << std::endl;
        return cv::Mat();
    }
    
    size_t rows = var->dims[0];
    size_t cols = var->dims[1];
    
    cv::Mat img;
    
    if (var->class_type == MAT_C_DOUBLE) {
        const double* data = static_cast<const double*>(var->data);
        img = cv::Mat(rows, cols, CV_64F);
        
        // Convert from column-major (MATLAB) to row-major (OpenCV)
        for (size_t r = 0; r < rows; ++r) {
            for (size_t c = 0; c < cols; ++c) {
                img.at<double>(r, c) = data[c * rows + r];
            }
        }
    } else if (var->class_type == MAT_C_UINT8) {
        const uint8_t* data = static_cast<const uint8_t*>(var->data);
        img = cv::Mat(rows, cols, CV_8U);
        
        for (size_t r = 0; r < rows; ++r) {
            for (size_t c = 0; c < cols; ++c) {
                img.at<uint8_t>(r, c) = data[c * rows + r];
            }
        }
    } else {
        std::cerr << "Warning: Unsupported image data type" << std::endl;
        return cv::Mat();
    }
    
    return img;
}

double MatReader::readScalar(matvar_t* var) {
    if (!var || !var->data) return 0.0;
    
    if (var->class_type == MAT_C_DOUBLE) {
        const double* data = static_cast<const double*>(var->data);
        return data[0];
    } else if (var->class_type == MAT_C_INT32) {
        const int32_t* data = static_cast<const int32_t*>(var->data);
        return static_cast<double>(data[0]);
    } else if (var->class_type == MAT_C_UINT8) {
        const uint8_t* data = static_cast<const uint8_t*>(var->data);
        return static_cast<double>(data[0]);
    }
    
    std::cerr << "Warning: Unsupported scalar type" << std::endl;
    return 0.0;
}

std::string MatReader::readString(matvar_t* var) {
    if (!var || !var->data) return "";
    
    if (var->class_type == MAT_C_CHAR) {
        const char* data = static_cast<const char*>(var->data);
        size_t len = 1;
        for (int i = 0; i < var->rank; ++i) {
            len *= var->dims[i];
        }
        return std::string(data, len);
    } else if (var->class_type == MAT_C_UINT16) {
        // MATLAB often stores strings as uint16
        const uint16_t* data = static_cast<const uint16_t*>(var->data);
        size_t len = 1;
        for (int i = 0; i < var->rank; ++i) {
            len *= var->dims[i];
        }
        std::string result;
        for (size_t i = 0; i < len; ++i) {
            result += static_cast<char>(data[i]);
        }
        return result;
    }
    
    std::cerr << "Warning: Unsupported string type" << std::endl;
    return "";
}

size_t MatReader::getCellArraySize(matvar_t* var) {
    if (!var || var->class_type != MAT_C_CELL) return 0;
    
    size_t n_cells = 1;
    for (int i = 0; i < var->rank; ++i) {
        n_cells *= var->dims[i];
    }
    
    return n_cells;
}

matvar_t* MatReader::getCellElement(matvar_t* var, size_t index) {
    if (!var || var->class_type != MAT_C_CELL) return nullptr;
    
    size_t n_cells = getCellArraySize(var);
    if (index >= n_cells) return nullptr;
    
    matvar_t** cells = static_cast<matvar_t**>(var->data);
    return cells[index];
}

std::map<std::string, double> MatReader::readDispInfo(mat_t* matfp) {
    std::map<std::string, double> params;
    
    if (!matfp) return params;
    
    // Try to read from data_dic_save.dispinfo
    matvar_t* dispinfo = readNestedField(matfp, "data_dic_save.dispinfo");
    if (!dispinfo || dispinfo->class_type != MAT_C_STRUCT) {
        if (dispinfo) Mat_VarFree(dispinfo);
        return params;
    }
    
    // Read common dispinfo fields
    std::vector<std::string> field_names = {
        "cutoff_corrcoef", "cutoff_diffnorm", "cutoff_iteration",
        "radius", "spacing", "subsettrunc", "total_threads"
    };
    
    for (const auto& field : field_names) {
        matvar_t* field_var = Mat_VarGetStructFieldByName(dispinfo, field.c_str(), 0);
        if (field_var && field_var->data) {
            double value = readScalar(field_var);
            params[field] = value;
        }
    }
    
    Mat_VarFree(dispinfo);
    return params;
}

bool MatReader::variableExists(mat_t* matfp, const std::string& var_name) {
    if (!matfp) return false;
    
    matvar_t* var = Mat_VarRead(matfp, var_name.c_str());
    if (var) {
        Mat_VarFree(var);
        return true;
    }
    
    return false;
}

cv::Mat MatReader::readROIMask(const std::string& mat_path, const std::string& var_name) {
    mat_t* matfp = Mat_Open(mat_path.c_str(), MAT_ACC_RDONLY);
    if (!matfp) {
        std::cerr << "Failed to open MAT file: " << mat_path << std::endl;
        return cv::Mat();
    }
    
    matvar_t* var = Mat_VarRead(matfp, var_name.c_str());
    cv::Mat mask;
    
    if (var) {
        mask = readImage(var);
        Mat_VarFree(var);
    } else {
        std::cerr << "Variable '" << var_name << "' not found in " << mat_path << std::endl;
    }
    
    Mat_Close(matfp);
    return mask;
}

} // namespace cppxdic
