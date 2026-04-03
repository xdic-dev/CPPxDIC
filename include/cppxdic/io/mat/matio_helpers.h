#ifndef CPPXDIC_IO_MAT_MATIO_HELPERS_H
#define CPPXDIC_IO_MAT_MATIO_HELPERS_H

#include <Eigen/Dense>
#include <matio.h>
#include <opencv2/opencv.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace cppxdic::io::mat {

bool writeMatVariable(::mat_t* file, std::string_view variable_name, const cv::Mat& mat);
bool writeArrayVariable(::mat_t* file,
                        std::string_view variable_name,
                        const void* data,
                        const std::vector<size_t>& dims,
                        matio_types data_type,
                        matio_classes class_type);
bool writeStringVariable(::mat_t* file, std::string_view variable_name, std::string_view value);
bool writeScalarVariable(::mat_t* file, std::string_view variable_name, double value);

matvar_t* createStructVariable(std::string_view struct_name,
                               const std::vector<std::string>& field_names);
bool addFieldToStruct(matvar_t* struct_var,
                      std::string_view field_name,
                      matvar_t* field_var,
                      size_t index = 0);

matvar_t* createCellArrayFromScalars(const std::string& name,
                                     const std::vector<std::vector<double>>& data,
                                     size_t n_frames);
matvar_t* createCellArrayFromVectors(const std::string& name,
                                     const std::vector<std::vector<Eigen::Vector3d>>& data,
                                     size_t n_frames);
matvar_t* createCellArrayFromMatrices(const std::string& name,
                                      const std::vector<std::vector<Eigen::Matrix3d>>& data,
                                      size_t n_frames);
matvar_t* createCellArray2DFromStrings(const std::string& name,
                                       const std::vector<std::vector<std::string>>& data,
                                       size_t rows,
                                       size_t cols);
matvar_t* createCellArray2DFromVectors(const std::string& name,
                                       const std::vector<std::vector<std::vector<double>>>& data,
                                       size_t rows,
                                       size_t cols);

} // namespace cppxdic::io::mat

#endif // CPPXDIC_IO_MAT_MATIO_HELPERS_H
