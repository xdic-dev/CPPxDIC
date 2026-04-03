#include "cppxdic/io/mat/matio_helpers.h"

#include <iostream>

namespace cppxdic::io::mat {

bool writeMatVariable(::mat_t* file, std::string_view variable_name, const cv::Mat& mat) {
    if (mat.empty()) {
        std::cerr << "Cannot write empty Mat" << std::endl;
        return false;
    }

    std::vector<size_t> dims = {static_cast<size_t>(mat.rows), static_cast<size_t>(mat.cols)};

    matio_types mat_type;
    matio_classes mat_class;

    switch (mat.type()) {
        case CV_8UC1:
            mat_type = MAT_T_UINT8;
            mat_class = MAT_C_UINT8;
            break;
        case CV_16UC1:
            mat_type = MAT_T_UINT16;
            mat_class = MAT_C_UINT16;
            break;
        case CV_32FC1:
            mat_type = MAT_T_SINGLE;
            mat_class = MAT_C_SINGLE;
            break;
        case CV_64FC1:
            mat_type = MAT_T_DOUBLE;
            mat_class = MAT_C_DOUBLE;
            break;
        default:
            std::cerr << "Unsupported Mat type: " << mat.type() << std::endl;
            return false;
    }

    cv::Mat transposed = mat.t();
    matvar_t* matvar = Mat_VarCreate(std::string(variable_name).c_str(), mat_class, mat_type,
                                     2, dims.data(), transposed.data, 0);
    if (!matvar) {
        std::cerr << "Failed to create variable: " << variable_name << std::endl;
        return false;
    }

    const int result = Mat_VarWrite(file, matvar, MAT_COMPRESSION_NONE);
    Mat_VarFree(matvar);
    return result == 0;
}

bool writeArrayVariable(::mat_t* file,
                        std::string_view variable_name,
                        const void* data,
                        const std::vector<size_t>& dims,
                        matio_types data_type,
                        matio_classes class_type) {
    matvar_t* matvar = Mat_VarCreate(std::string(variable_name).c_str(), class_type, data_type,
                                     static_cast<int>(dims.size()), dims.data(),
                                     const_cast<void*>(data), 0);
    if (!matvar) {
        std::cerr << "Failed to create array variable: " << variable_name << std::endl;
        return false;
    }

    const int result = Mat_VarWrite(file, matvar, MAT_COMPRESSION_NONE);
    Mat_VarFree(matvar);
    return result == 0;
}

bool writeStringVariable(::mat_t* file, std::string_view variable_name, std::string_view value) {
    std::vector<size_t> dims = {1, value.length()};
    matvar_t* matvar = Mat_VarCreate(std::string(variable_name).c_str(), MAT_C_CHAR, MAT_T_UTF8,
                                     2, dims.data(),
                                     const_cast<char*>(value.data()), 0);
    if (!matvar) {
        std::cerr << "Failed to create string variable: " << variable_name << std::endl;
        return false;
    }

    const int result = Mat_VarWrite(file, matvar, MAT_COMPRESSION_NONE);
    Mat_VarFree(matvar);
    return result == 0;
}

bool writeScalarVariable(::mat_t* file, std::string_view variable_name, double value) {
    std::vector<size_t> dims = {1, 1};
    matvar_t* matvar = Mat_VarCreate(std::string(variable_name).c_str(), MAT_C_DOUBLE, MAT_T_DOUBLE,
                                     2, dims.data(), &value, 0);
    if (!matvar) {
        std::cerr << "Failed to create scalar variable: " << variable_name << std::endl;
        return false;
    }

    const int result = Mat_VarWrite(file, matvar, MAT_COMPRESSION_NONE);
    Mat_VarFree(matvar);
    return result == 0;
}

matvar_t* createStructVariable(std::string_view struct_name,
                               const std::vector<std::string>& field_names) {
    std::vector<size_t> dims = {1, 1};
    std::vector<const char*> c_field_names;
    c_field_names.reserve(field_names.size());
    for (const auto& field_name : field_names) {
        c_field_names.push_back(field_name.c_str());
    }

    return Mat_VarCreateStruct(std::string(struct_name).c_str(), 2, dims.data(),
                               c_field_names.data(), field_names.size());
}

bool addFieldToStruct(matvar_t* struct_var,
                      std::string_view field_name,
                      matvar_t* field_var,
                      size_t index) {
    if (!struct_var || struct_var->class_type != MAT_C_STRUCT) {
        std::cerr << "Error: Not a struct variable" << std::endl;
        return false;
    }

    if (!field_var) {
        std::cerr << "Error: Null field variable adding: " << field_name << std::endl;
        return false;
    }

    Mat_VarSetStructFieldByName(struct_var, std::string(field_name).c_str(), index, field_var);
    return true;
}

matvar_t* createCellArrayFromScalars(const std::string& name,
                                     const std::vector<std::vector<double>>& data,
                                     size_t n_frames) {
    std::vector<size_t> cell_dims = {1, n_frames};
    matvar_t* cell_array = Mat_VarCreate(name.c_str(), MAT_C_CELL, MAT_T_CELL,
                                         2, cell_dims.data(), nullptr, 0);
    if (!cell_array) {
        std::cerr << "Failed to create cell array: " << name << std::endl;
        return nullptr;
    }

    for (size_t i = 0; i < n_frames && i < data.size(); ++i) {
        const auto& frame_data = data[i];
        if (frame_data.empty()) {
            Mat_VarSetCell(cell_array, i, nullptr);
            continue;
        }

        std::vector<size_t> dims = {frame_data.size(), 1};
        matvar_t* cell_data = Mat_VarCreate(nullptr, MAT_C_DOUBLE, MAT_T_DOUBLE,
                                            2, dims.data(),
                                            const_cast<double*>(frame_data.data()), 0);
        Mat_VarSetCell(cell_array, i, cell_data);
    }

    return cell_array;
}

matvar_t* createCellArrayFromVectors(const std::string& name,
                                     const std::vector<std::vector<Eigen::Vector3d>>& data,
                                     size_t n_frames) {
    std::vector<size_t> cell_dims = {1, n_frames};
    matvar_t* cell_array = Mat_VarCreate(name.c_str(), MAT_C_CELL, MAT_T_CELL,
                                         2, cell_dims.data(), nullptr, 0);
    if (!cell_array) {
        std::cerr << "Failed to create cell array: " << name << std::endl;
        return nullptr;
    }

    for (size_t i = 0; i < n_frames && i < data.size(); ++i) {
        const auto& frame_data = data[i];
        const size_t n_vectors = frame_data.size();
        if (n_vectors == 0) {
            Mat_VarSetCell(cell_array, i, nullptr);
            continue;
        }

        std::vector<double> flat_data(n_vectors * 3);
        for (size_t j = 0; j < n_vectors; ++j) {
            flat_data[j] = frame_data[j](0);
            flat_data[j + n_vectors] = frame_data[j](1);
            flat_data[j + 2 * n_vectors] = frame_data[j](2);
        }

        std::vector<size_t> dims = {n_vectors, 3};
        matvar_t* cell_data = Mat_VarCreate(nullptr, MAT_C_DOUBLE, MAT_T_DOUBLE,
                                            2, dims.data(), flat_data.data(), 0);
        Mat_VarSetCell(cell_array, i, cell_data);
    }

    return cell_array;
}

matvar_t* createCellArrayFromMatrices(const std::string& name,
                                      const std::vector<std::vector<Eigen::Matrix3d>>& data,
                                      size_t n_frames) {
    std::vector<size_t> cell_dims = {1, n_frames};
    matvar_t* cell_array = Mat_VarCreate(name.c_str(), MAT_C_CELL, MAT_T_CELL,
                                         2, cell_dims.data(), nullptr, 0);
    if (!cell_array) {
        std::cerr << "Failed to create cell array: " << name << std::endl;
        return nullptr;
    }

    for (size_t i = 0; i < n_frames && i < data.size(); ++i) {
        const auto& frame_data = data[i];
        const size_t n_matrices = frame_data.size();
        if (n_matrices == 0) {
            Mat_VarSetCell(cell_array, i, nullptr);
            continue;
        }

        std::vector<double> flat_data(n_matrices * 9);
        for (size_t j = 0; j < n_matrices; ++j) {
            for (int col = 0; col < 3; ++col) {
                for (int row = 0; row < 3; ++row) {
                    flat_data[row + 3 * col + 9 * j] = frame_data[j](row, col);
                }
            }
        }

        std::vector<size_t> dims = {3, 3, n_matrices};
        matvar_t* cell_data = Mat_VarCreate(nullptr, MAT_C_DOUBLE, MAT_T_DOUBLE,
                                            3, dims.data(), flat_data.data(), 0);
        Mat_VarSetCell(cell_array, i, cell_data);
    }

    return cell_array;
}

matvar_t* createCellArray2DFromStrings(const std::string& name,
                                       const std::vector<std::vector<std::string>>& data,
                                       size_t rows,
                                       size_t cols) {
    std::vector<size_t> cell_dims = {rows, cols};
    matvar_t* cell_array = Mat_VarCreate(name.c_str(), MAT_C_CELL, MAT_T_CELL,
                                         2, cell_dims.data(), nullptr, 0);
    if (!cell_array) {
        std::cerr << "Failed to create 2D cell array: " << name << std::endl;
        return nullptr;
    }

    for (size_t i = 0; i < rows && i < data.size(); ++i) {
        for (size_t j = 0; j < cols && j < data[i].size(); ++j) {
            const std::string& str = data[i][j];
            if (str.empty()) {
                Mat_VarSetCell(cell_array, i * cols + j, nullptr);
                continue;
            }

            std::vector<size_t> dims = {1, str.length()};
            matvar_t* str_var = Mat_VarCreate(nullptr, MAT_C_CHAR, MAT_T_UINT8,
                                              2, dims.data(),
                                              const_cast<char*>(str.c_str()), 0);
            Mat_VarSetCell(cell_array, i * cols + j, str_var);
        }
    }

    return cell_array;
}

matvar_t* createCellArray2DFromVectors(const std::string& name,
                                       const std::vector<std::vector<std::vector<double>>>& data,
                                       size_t rows,
                                       size_t cols) {
    std::vector<size_t> cell_dims = {rows, cols};
    matvar_t* cell_array = Mat_VarCreate(name.c_str(), MAT_C_CELL, MAT_T_CELL,
                                         2, cell_dims.data(), nullptr, 0);
    if (!cell_array) {
        std::cerr << "Failed to create 2D cell array: " << name << std::endl;
        return nullptr;
    }

    for (size_t i = 0; i < rows && i < data.size(); ++i) {
        for (size_t j = 0; j < cols && j < data[i].size(); ++j) {
            const auto& vec = data[i][j];
            if (vec.empty()) {
                Mat_VarSetCell(cell_array, i * cols + j, nullptr);
                continue;
            }

            std::vector<size_t> dims = {1, vec.size()};
            matvar_t* vec_var = Mat_VarCreate(nullptr, MAT_C_DOUBLE, MAT_T_DOUBLE,
                                              2, dims.data(),
                                              const_cast<double*>(vec.data()), 0);
            Mat_VarSetCell(cell_array, i * cols + j, vec_var);
        }
    }

    return cell_array;
}

} // namespace cppxdic::io::mat
