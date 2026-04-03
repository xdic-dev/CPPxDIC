/**
 * MAT File Writer for CPPXDIC
 * Implementation of MAT file writing in xDIC format
 * Produces MATLAB v7.3 (HDF5) compatible output
 */

#include "mat_writer.h"
#include "cppxdic/io/mat/matio_helpers.h"
#include "cppxdic/io/mat/mat_ncorr_writer.h"
#include "cppxdic/io/mat/mat_results_writer.h"
#include <iostream>
#include <cstring>
#include <vector>
#include <stdexcept>
#include <Eigen/Dense>
#include <filesystem>
#include <algorithm>

namespace cppxdic {

bool MatWriter::writeMatchingFile(const std::string& filename,
                                 const cv::Mat& ref_img,
                                 const cv::Mat& cur_img,
                                 const cv::Mat& ref_roi,
                                 const cv::Mat& cur_roi,
                                 const ncorr::DIC_analysis_output& dic_output,
                                 const std::map<std::string, double>& dispinfo) {
    return io::mat::MatNcorrWriter::writeMatchingFile(
        filename, ref_img, cur_img, ref_roi, cur_roi, dic_output, dispinfo);
}

bool MatWriter::writeMatchingFile(const std::string& filename,
                                 const cv::Mat& ref_img,
                                 const cv::Mat& cur_img,
                                 const cv::Mat& ref_roi,
                                 const cv::Mat& cur_roi,
                                 const ncorr::DIC_analysis_output& dic_lagrangian,
                                 const ncorr::DIC_analysis_output& dic_eulerian,
                                 const std::map<std::string, double>& dispinfo) {
    return io::mat::MatNcorrWriter::writeMatchingFile(
        filename, ref_img, cur_img, ref_roi, cur_roi,
        dic_lagrangian, dic_eulerian, dispinfo);
}

bool MatWriter::writeMultiFrameNcorrFile(const std::string& filename,
                                        const cv::Mat& ref_img,
                                        const std::vector<cv::Mat>& cur_imgs,
                                        const cv::Mat& ref_roi,
                                        const std::vector<cv::Mat>& cur_rois,
                                        const std::vector<ncorr::DIC_analysis_output>& dic_outputs,
                                        const std::map<std::string, double>& dispinfo,
                                        const std::string& type_str,
                                        const std::string& ref_name) {
    return io::mat::MatNcorrWriter::writeMultiFrameNcorrFile(
        filename, ref_img, cur_imgs, ref_roi, cur_rois, dic_outputs,
        dispinfo, type_str, ref_name);
}

bool MatWriter::writeROIMaskFile(const std::string& filename,
                                const cv::Mat& mask) {
    mat_t* matfp = createMatFileV5(filename);
    if (!matfp) {
        std::cerr << "Failed to create ROI mask file: " << filename << std::endl;
        return false;
    }
    
    // Write mask as 'refmask' variable (uint8 2D array)
    bool success = writeMatVariable(matfp, "refmask", mask);
    
    Mat_Close(matfp);
    return success;
}

bool MatWriter::writeSeedFile(const std::string& filename,
                             int seed_x,
                             int seed_y) {
    mat_t* matfp = createMatFileV5(filename);
    if (!matfp) {
        std::cerr << "Failed to create seed file: " << filename << std::endl;
        return false;
    }
    
    // Write seed_point as uint16 1D array [x, y]
    uint16_t seed_data[2] = {static_cast<uint16_t>(seed_x), static_cast<uint16_t>(seed_y)};
    std::vector<size_t> dims = {2};
    
    bool success = writeArrayVariable(matfp, "seed_point", seed_data, dims,
                                     MAT_T_UINT16, MAT_C_UINT16);
    
    Mat_Close(matfp);
    return success;
}

bool MatWriter::writeTrialInfoFile(const std::string& filename,
                                  double fps,
                                  const std::vector<int>& frame_indices) {
    mat_t* matfp = createMatFileV5(filename);
    if (!matfp) {
        std::cerr << "Failed to create trial info file: " << filename << std::endl;
        return false;
    }
    
    // Write actual_fps_meas
    if (!writeScalarVariable(matfp, "actual_fps_meas", fps)) {
        Mat_Close(matfp);
        return false;
    }
    
    // Write idxframe as uint8 1D array
    std::vector<uint8_t> idx_data(frame_indices.begin(), frame_indices.end());
    std::vector<size_t> dims = {idx_data.size()};
    
    bool success = writeArrayVariable(matfp, "idxframe", idx_data.data(), dims,
                                     MAT_T_UINT8, MAT_C_UINT8);
    
    Mat_Close(matfp);
    return success;
}

bool MatWriter::writeDICResultFile(const std::string& filename,
                                  const ncorr::DIC_analysis_input& dic_input,
                                  const ncorr::DIC_analysis_output& dic_output) {
    mat_t* matfp = createMatFileHDF5(filename);
    if (!matfp) {
        std::cerr << "Failed to create DIC result file: " << filename << std::endl;
        return false;
    }
    
    // Create input struct with actual values
    std::vector<std::string> input_fields = {"radius", "spacing", "subregion_type", "interp_type"};
    matvar_t* input_struct = createStructVariable("input", input_fields);
    
    // Populate input fields
    size_t scalar_dims[2] = {1, 1};
    double radius_val = static_cast<double>(dic_input.r);
    double spacing_val = static_cast<double>(dic_input.scalefactor);
    matvar_t* radius_var = Mat_VarCreate("radius", MAT_C_DOUBLE, MAT_T_DOUBLE, 2, scalar_dims, &radius_val, 0);
    matvar_t* spacing_var = Mat_VarCreate("spacing", MAT_C_DOUBLE, MAT_T_DOUBLE, 2, scalar_dims, &spacing_val, 0);
    Mat_VarSetStructFieldByName(input_struct, "radius", 0, radius_var);
    Mat_VarSetStructFieldByName(input_struct, "spacing", 0, spacing_var);
    
    // Write input struct
    Mat_VarWrite(matfp, input_struct, MAT_COMPRESSION_NONE);
    Mat_VarFree(input_struct);
    
    // Create output struct with displacement data
    size_t n_frames = dic_output.disps.size();
    if (n_frames > 0) {
        std::vector<std::string> output_fields = {"u", "v", "cc", "roi"};
        matvar_t* output_struct = createStructVariable("output", output_fields);
        
        // Create cell arrays for displacement components
        size_t cell_dims[2] = {n_frames, 1};
        matvar_t* u_cell = Mat_VarCreate("u", MAT_C_CELL, MAT_T_CELL, 2, cell_dims, nullptr, 0);
        matvar_t* v_cell = Mat_VarCreate("v", MAT_C_CELL, MAT_T_CELL, 2, cell_dims, nullptr, 0);
        matvar_t* cc_cell = Mat_VarCreate("cc", MAT_C_CELL, MAT_T_CELL, 2, cell_dims, nullptr, 0);
        matvar_t* roi_cell = Mat_VarCreate("roi", MAT_C_CELL, MAT_T_CELL, 2, cell_dims, nullptr, 0);
        
        // Fill cells with actual data from each frame
        for (size_t i = 0; i < n_frames; ++i) {
            const auto& disp = dic_output.disps[i];
            const auto& u_array = disp.get_u().get_array();
            const auto& v_array = disp.get_v().get_array();
            const auto& cc_array = disp.get_cc().get_array();
            const auto& roi_mask = disp.get_roi().get_mask();
            
            size_t height = u_array.height();
            size_t width = u_array.width();
            size_t data_dims[2] = {height, width};
            
            // Create u displacement matrix (column-major for MATLAB)
            double* u_data = new double[height * width];
            for (size_t y = 0; y < height; ++y) {
                for (size_t x = 0; x < width; ++x) {
                    u_data[x * height + y] = u_array(y, x);
                }
            }
            matvar_t* u_mat = Mat_VarCreate("", MAT_C_DOUBLE, MAT_T_DOUBLE, 2, data_dims, u_data, MAT_F_DONT_COPY_DATA);
            Mat_VarSetCell(u_cell, i, u_mat);
            
            // Create v displacement matrix
            double* v_data = new double[height * width];
            for (size_t y = 0; y < height; ++y) {
                for (size_t x = 0; x < width; ++x) {
                    v_data[x * height + y] = v_array(y, x);
                }
            }
            matvar_t* v_mat = Mat_VarCreate("", MAT_C_DOUBLE, MAT_T_DOUBLE, 2, data_dims, v_data, MAT_F_DONT_COPY_DATA);
            Mat_VarSetCell(v_cell, i, v_mat);
            
            // Create correlation coefficient matrix
            double* cc_data = new double[height * width];
            for (size_t y = 0; y < height; ++y) {
                for (size_t x = 0; x < width; ++x) {
                    cc_data[x * height + y] = cc_array(y, x);
                }
            }
            matvar_t* cc_mat = Mat_VarCreate("", MAT_C_DOUBLE, MAT_T_DOUBLE, 2, data_dims, cc_data, MAT_F_DONT_COPY_DATA);
            Mat_VarSetCell(cc_cell, i, cc_mat);
            
            // Create ROI mask matrix
            uint8_t* roi_data = new uint8_t[height * width];
            for (size_t y = 0; y < height; ++y) {
                for (size_t x = 0; x < width; ++x) {
                    roi_data[x * height + y] = roi_mask(y, x) ? 1 : 0;
                }
            }
            matvar_t* roi_mat = Mat_VarCreate("", MAT_C_UINT8, MAT_T_UINT8, 2, data_dims, roi_data, MAT_F_DONT_COPY_DATA);
            Mat_VarSetCell(roi_cell, i, roi_mat);
        }
        
        // Add cells to output struct
        Mat_VarSetStructFieldByName(output_struct, "u", 0, u_cell);
        Mat_VarSetStructFieldByName(output_struct, "v", 0, v_cell);
        Mat_VarSetStructFieldByName(output_struct, "cc", 0, cc_cell);
        Mat_VarSetStructFieldByName(output_struct, "roi", 0, roi_cell);
        
        // Write output struct
        Mat_VarWrite(matfp, output_struct, MAT_COMPRESSION_NONE);
        Mat_VarFree(output_struct);
    }
    
    Mat_Close(matfp);
    
    std::cout << "Wrote DIC result file: " << filename << " (" << n_frames << " frames)" << std::endl;
    return true;
}

// Private helper methods

mat_t* MatWriter::createMatFileHDF5(const std::string& filename) {
    // Create HDF5-based MAT file (v7.3)
    mat_t* matfp = Mat_CreateVer(filename.c_str(), nullptr, MAT_FT_MAT73);
    return matfp;
}

mat_t* MatWriter::createMatFileV5(const std::string& filename) {
    // Create standard MAT file (v5)
    mat_t* matfp = Mat_CreateVer(filename.c_str(), nullptr, MAT_FT_MAT5);
    return matfp;
}

bool MatWriter::writeMatVariable(mat_t* matfp,
                                const std::string& varname,
                                const cv::Mat& mat) {
    return io::mat::writeMatVariable(matfp, varname, mat);
}

bool MatWriter::writeArrayVariable(mat_t* matfp,
                                  const std::string& varname,
                                  const void* data,
                                  const std::vector<size_t>& dims,
                                  matio_types data_type,
                                  matio_classes class_type) {
    return io::mat::writeArrayVariable(matfp, varname, data, dims, data_type, class_type);
}

bool MatWriter::writeStringVariable(mat_t* matfp,
                                   const std::string& varname,
                                   const std::string& str) {
    return io::mat::writeStringVariable(matfp, varname, str);
}

bool MatWriter::writeScalarVariable(mat_t* matfp,
                                   const std::string& varname,
                                   double value) {
    return io::mat::writeScalarVariable(matfp, varname, value);
}

matvar_t* MatWriter::createStructVariable(const std::string& struct_name,
                                         const std::vector<std::string>& field_names) {
    return io::mat::createStructVariable(struct_name, field_names);
}

bool MatWriter::addFieldToStruct(matvar_t* struct_var,
                                const std::string& field_name,
                                matvar_t* field_var,
                                size_t index) {
    return io::mat::addFieldToStruct(struct_var, field_name, field_var, index);
}

void MatWriter::convertDispToArrays(const ncorr::Disp2D& disp,
                                   std::vector<double>& u_array,
                                   std::vector<double>& v_array,
                                   cv::Mat& roi_mask) {
    const auto& u = disp.get_u().get_array();
    const auto& v = disp.get_v().get_array();
    const auto& roi = disp.get_roi().get_mask();
    
    size_t height = u.height();
    size_t width = u.width();
    
    // Convert displacement arrays
    u_array.resize(height * width);
    v_array.resize(height * width);
    
    for (size_t y = 0; y < height; ++y) {
        for (size_t x = 0; x < width; ++x) {
            size_t idx = y * width + x;
            u_array[idx] = u(y, x);
            v_array[idx] = v(y, x);
        }
    }
    
    // Convert ROI mask
    roi_mask = cv::Mat(height, width, CV_8UC1);
    for (size_t y = 0; y < height; ++y) {
        for (size_t x = 0; x < width; ++x) {
            roi_mask.at<uint8_t>(y, x) = roi(y, x) ? 255 : 0;
        }
    }
}

matvar_t* MatWriter::formatDispInfo(const std::map<std::string, double>& params) {
    // Create dispinfo struct with all required fields
    std::vector<std::string> field_names = {
        "cutoff_corrcoef", "cutoff_diffnorm", "cutoff_iteration", "imgcorr",
        "lenscoef", "pixtounits", "radius", "spacing", "subsettrunc", 
        "total_threads", "type", "units"
    };
    
    matvar_t* dispinfo = createStructVariable("dispinfo", field_names);
    if (!dispinfo) return nullptr;
    
    // Helper to write scalar field (1x1)
    auto writeScalarField = [&](const std::string& field_name, double value) {
        size_t dims[2] = {1, 1};
        double* data = new double[1];
        data[0] = value;
        matvar_t* field_var = Mat_VarCreate(field_name.c_str(), MAT_C_DOUBLE, MAT_T_DOUBLE, 
                                           2, dims, data, MAT_F_DONT_COPY_DATA);
        if (field_var) {
            Mat_VarSetStructFieldByName(dispinfo, field_name.c_str(), 0, field_var);
        }
    };
    
    // Populate numeric fields from params map (all 1x1 except cutoff_corrcoef)
    for (const auto& p : params) {
        if (p.first == "type" || p.first == "units" || p.first == "cutoff_corrcoef") {
            continue;  // Handle these separately
        }
        writeScalarField(p.first, p.second);
    }
    
    // Special handling for cutoff_corrcoef (1x2 array)
    size_t corrcoef_dims[2] = {1, 2};
    double* corrcoef_data = new double[2];
    // Use value from params if available, otherwise default
    auto it = params.find("cutoff_corrcoef");
    corrcoef_data[0] = (it != params.end()) ? it->second : 0.0;
    corrcoef_data[1] = corrcoef_data[0];  // Both values same by default
    matvar_t* corrcoef_var = Mat_VarCreate("cutoff_corrcoef", MAT_C_DOUBLE, MAT_T_DOUBLE, 
                                          2, corrcoef_dims, corrcoef_data, MAT_F_DONT_COPY_DATA);
    Mat_VarSetStructFieldByName(dispinfo, "cutoff_corrcoef", 0, corrcoef_var);
    
    // Add empty imgcorr struct
    std::vector<std::string> imgcorr_fields = {"idx_ref", "idx_cur"};
    matvar_t* imgcorr_var = createStructVariable("imgcorr", imgcorr_fields);
    Mat_VarSetStructFieldByName(dispinfo, "imgcorr", 0, imgcorr_var);
    
    // Add lenscoef (default 0.0 if not in params)
    auto lenscoef_it = params.find("lenscoef");
    double lenscoef_val = (lenscoef_it != params.end()) ? lenscoef_it->second : 0.0;
    writeScalarField("lenscoef", lenscoef_val);
    
    // Add pixtounits (default 1.0 if not in params)
    auto pixtounits_it = params.find("pixtounits");
    double pixtounits_val = (pixtounits_it != params.end()) ? pixtounits_it->second : 1.0;
    writeScalarField("pixtounits", pixtounits_val);
    
    // Write string fields
    std::string type_str = "Regular";
    size_t type_dims[2] = {1, type_str.length()};
    matvar_t* type_var = Mat_VarCreate("type", MAT_C_CHAR, MAT_T_UTF8, 2, type_dims, 
                                       (void*)type_str.c_str(), 0);
    Mat_VarSetStructFieldByName(dispinfo, "type", 0, type_var);
    
    std::string units_str = "pixels";
    size_t units_dims[2] = {1, units_str.length()};
    matvar_t* units_var = Mat_VarCreate("units", MAT_C_CHAR, MAT_T_UTF8, 2, units_dims,
                                        (void*)units_str.c_str(), 0);
    Mat_VarSetStructFieldByName(dispinfo, "units", 0, units_var);
    
    return dispinfo;
}

matvar_t* MatWriter::formatDisplacements(const ncorr::DIC_analysis_output& dic_output) {
    size_t n_frames = dic_output.disps.size();
    if (n_frames == 0) return nullptr;
    
    // Create cell arrays for each field
    size_t frame_dims[2] = {n_frames, 1};
    
    matvar_t* plot_u_dic = Mat_VarCreate("plot_u_dic", MAT_C_CELL, MAT_T_CELL, 2, frame_dims, nullptr, 0);
    matvar_t* plot_v_dic = Mat_VarCreate("plot_v_dic", MAT_C_CELL, MAT_T_CELL, 2, frame_dims, nullptr, 0);
    matvar_t* plot_u_ref = Mat_VarCreate("plot_u_ref_formatted", MAT_C_CELL, MAT_T_CELL, 2, frame_dims, nullptr, 0);
    matvar_t* plot_v_ref = Mat_VarCreate("plot_v_ref_formatted", MAT_C_CELL, MAT_T_CELL, 2, frame_dims, nullptr, 0);
    matvar_t* roi_dic = Mat_VarCreate("roi_dic", MAT_C_CELL, MAT_T_CELL, 2, frame_dims, nullptr, 0);
    matvar_t* plot_corrcoef = Mat_VarCreate("plot_corrcoef_dic", MAT_C_CELL, MAT_T_CELL, 2, frame_dims, nullptr, 0);
    
    // Fill cells with data from each frame
    for (size_t i = 0; i < n_frames; ++i) {
        const auto& disp = dic_output.disps[i];
        const auto& u_array = disp.get_u().get_array();
        const auto& v_array = disp.get_v().get_array();
        const auto& roi_mask = disp.get_roi().get_mask();
        
        size_t height = u_array.height();
        size_t width = u_array.width();
        size_t data_dims[2] = {height, width};
        
        // Create u displacement matrix (column-major for MATLAB)
        double* u_data = new double[height * width];
        for (size_t y = 0; y < height; ++y) {
            for (size_t x = 0; x < width; ++x) {
                u_data[x * height + y] = u_array(y, x);
            }
        }
        matvar_t* u_mat = Mat_VarCreate("", MAT_C_DOUBLE, MAT_T_DOUBLE, 2, data_dims, u_data, MAT_F_DONT_COPY_DATA);
        Mat_VarSetCell(plot_u_dic, i, u_mat);
        Mat_VarSetCell(plot_u_ref, i, Mat_VarDuplicate(u_mat, 1));
        
        // Create v displacement matrix
        double* v_data = new double[height * width];
        for (size_t y = 0; y < height; ++y) {
            for (size_t x = 0; x < width; ++x) {
                v_data[x * height + y] = v_array(y, x);
            }
        }
        matvar_t* v_mat = Mat_VarCreate("", MAT_C_DOUBLE, MAT_T_DOUBLE, 2, data_dims, v_data, MAT_F_DONT_COPY_DATA);
        Mat_VarSetCell(plot_v_dic, i, v_mat);
        Mat_VarSetCell(plot_v_ref, i, Mat_VarDuplicate(v_mat, 1));
        
        // Create ROI mask matrix
        uint8_t* roi_data = new uint8_t[height * width];
        for (size_t y = 0; y < height; ++y) {
            for (size_t x = 0; x < width; ++x) {
                roi_data[x * height + y] = roi_mask(y, x) ? 1 : 0;
            }
        }
        matvar_t* roi_mat = Mat_VarCreate("", MAT_C_UINT8, MAT_T_UINT8, 2, data_dims, roi_data, MAT_F_DONT_COPY_DATA);
        Mat_VarSetCell(roi_dic, i, roi_mat);
        
        // Correlation coefficient - use actual data from DIC output
        const auto& cc_array = disp.get_cc().get_array();
        double* cc_data = new double[height * width];
        for (size_t y = 0; y < height; ++y) {
            for (size_t x = 0; x < width; ++x) {
                cc_data[x * height + y] = cc_array(y, x);
            }
        }
        matvar_t* cc_mat = Mat_VarCreate("", MAT_C_DOUBLE, MAT_T_DOUBLE, 2, data_dims, cc_data, MAT_F_DONT_COPY_DATA);
        Mat_VarSetCell(plot_corrcoef, i, cc_mat);
    }
    
    // Create struct and populate
    std::vector<std::string> field_names = {
        "plot_corrcoef_dic", "plot_u_dic", "plot_v_dic",
        "plot_u_ref_formatted", "plot_v_ref_formatted", "roi_dic"
    };
    
    matvar_t* displacements = createStructVariable("displacements", field_names);
    Mat_VarSetStructFieldByName(displacements, "plot_corrcoef_dic", 0, plot_corrcoef);
    Mat_VarSetStructFieldByName(displacements, "plot_u_dic", 0, plot_u_dic);
    Mat_VarSetStructFieldByName(displacements, "plot_v_dic", 0, plot_v_dic);
    Mat_VarSetStructFieldByName(displacements, "plot_u_ref_formatted", 0, plot_u_ref);
    Mat_VarSetStructFieldByName(displacements, "plot_v_ref_formatted", 0, plot_v_ref);
    Mat_VarSetStructFieldByName(displacements, "roi_dic", 0, roi_dic);
    
    return displacements;
}

matvar_t* MatWriter::formatDisplacements(const ncorr::DIC_analysis_output& dic_lagrangian,
                                        const ncorr::DIC_analysis_output& dic_eulerian) {
    size_t n_frames = dic_lagrangian.disps.size();
    if (n_frames == 0) return nullptr;
    
    // Verify both outputs have same number of frames
    if (dic_eulerian.disps.size() != n_frames) {
        throw std::runtime_error("Lagrangian and Eulerian outputs must have same number of frames");
    }
    
    // Create cell arrays for each field
    size_t frame_dims[2] = {n_frames, 1};
    
    matvar_t* plot_u_dic = Mat_VarCreate("plot_u_dic", MAT_C_CELL, MAT_T_CELL, 2, frame_dims, nullptr, 0);
    matvar_t* plot_v_dic = Mat_VarCreate("plot_v_dic", MAT_C_CELL, MAT_T_CELL, 2, frame_dims, nullptr, 0);
    matvar_t* plot_u_ref = Mat_VarCreate("plot_u_ref_formatted", MAT_C_CELL, MAT_T_CELL, 2, frame_dims, nullptr, 0);
    matvar_t* plot_v_ref = Mat_VarCreate("plot_v_ref_formatted", MAT_C_CELL, MAT_T_CELL, 2, frame_dims, nullptr, 0);
    matvar_t* plot_u_cur = Mat_VarCreate("plot_u_cur_formatted", MAT_C_CELL, MAT_T_CELL, 2, frame_dims, nullptr, 0);
    matvar_t* plot_v_cur = Mat_VarCreate("plot_v_cur_formatted", MAT_C_CELL, MAT_T_CELL, 2, frame_dims, nullptr, 0);
    matvar_t* roi_dic = Mat_VarCreate("roi_dic", MAT_C_CELL, MAT_T_CELL, 2, frame_dims, nullptr, 0);
    matvar_t* roi_ref = Mat_VarCreate("roi_ref_formatted", MAT_C_CELL, MAT_T_CELL, 2, frame_dims, nullptr, 0);
    matvar_t* roi_cur = Mat_VarCreate("roi_cur_formatted", MAT_C_CELL, MAT_T_CELL, 2, frame_dims, nullptr, 0);
    matvar_t* plot_corrcoef = Mat_VarCreate("plot_corrcoef_dic", MAT_C_CELL, MAT_T_CELL, 2, frame_dims, nullptr, 0);
    
    // Fill cells with data from each frame
    for (size_t i = 0; i < n_frames; ++i) {
        const auto& disp_lag = dic_lagrangian.disps[i];
        const auto& disp_eul = dic_eulerian.disps[i];
        
        // Get arrays from Lagrangian
        const auto& u_lag_array = disp_lag.get_u().get_array();
        const auto& v_lag_array = disp_lag.get_v().get_array();
        const auto& cc_lag_array = disp_lag.get_cc().get_array();
        const auto& roi_lag_mask = disp_lag.get_roi().get_mask();
        
        // Get arrays from Eulerian  
        const auto& u_eul_array = disp_eul.get_u().get_array();
        const auto& v_eul_array = disp_eul.get_v().get_array();
        const auto& roi_eul_mask = disp_eul.get_roi().get_mask();
        
        size_t height = u_lag_array.height();
        size_t width = u_lag_array.width();
        size_t data_dims[2] = {height, width};
        
        // Create u displacement matrices (column-major for MATLAB)
        
        // Raw DIC (plot_u_dic) - from Lagrangian
        double* u_dic_data = new double[height * width];
        for (size_t y = 0; y < height; ++y) {
            for (size_t x = 0; x < width; ++x) {
                u_dic_data[x * height + y] = u_lag_array(y, x);
            }
        }
        matvar_t* u_dic_mat = Mat_VarCreate("", MAT_C_DOUBLE, MAT_T_DOUBLE, 2, data_dims, u_dic_data, MAT_F_DONT_COPY_DATA);
        Mat_VarSetCell(plot_u_dic, i, u_dic_mat);
        
        // Lagrangian formatted (plot_u_ref_formatted)
        double* u_ref_data = new double[height * width];
        for (size_t y = 0; y < height; ++y) {
            for (size_t x = 0; x < width; ++x) {
                u_ref_data[x * height + y] = u_lag_array(y, x);
            }
        }
        matvar_t* u_ref_mat = Mat_VarCreate("", MAT_C_DOUBLE, MAT_T_DOUBLE, 2, data_dims, u_ref_data, MAT_F_DONT_COPY_DATA);
        Mat_VarSetCell(plot_u_ref, i, u_ref_mat);
        
        // Eulerian formatted (plot_u_cur_formatted)
        double* u_cur_data = new double[height * width];
        for (size_t y = 0; y < height; ++y) {
            for (size_t x = 0; x < width; ++x) {
                u_cur_data[x * height + y] = u_eul_array(y, x);
            }
        }
        matvar_t* u_cur_mat = Mat_VarCreate("", MAT_C_DOUBLE, MAT_T_DOUBLE, 2, data_dims, u_cur_data, MAT_F_DONT_COPY_DATA);
        Mat_VarSetCell(plot_u_cur, i, u_cur_mat);
        
        // Create v displacement matrices
        
        // Raw DIC (plot_v_dic) - from Lagrangian
        double* v_dic_data = new double[height * width];
        for (size_t y = 0; y < height; ++y) {
            for (size_t x = 0; x < width; ++x) {
                v_dic_data[x * height + y] = v_lag_array(y, x);
            }
        }
        matvar_t* v_dic_mat = Mat_VarCreate("", MAT_C_DOUBLE, MAT_T_DOUBLE, 2, data_dims, v_dic_data, MAT_F_DONT_COPY_DATA);
        Mat_VarSetCell(plot_v_dic, i, v_dic_mat);
        
        // Lagrangian formatted (plot_v_ref_formatted)
        double* v_ref_data = new double[height * width];
        for (size_t y = 0; y < height; ++y) {
            for (size_t x = 0; x < width; ++x) {
                v_ref_data[x * height + y] = v_lag_array(y, x);
            }
        }
        matvar_t* v_ref_mat = Mat_VarCreate("", MAT_C_DOUBLE, MAT_T_DOUBLE, 2, data_dims, v_ref_data, MAT_F_DONT_COPY_DATA);
        Mat_VarSetCell(plot_v_ref, i, v_ref_mat);
        
        // Eulerian formatted (plot_v_cur_formatted)
        double* v_cur_data = new double[height * width];
        for (size_t y = 0; y < height; ++y) {
            for (size_t x = 0; x < width; ++x) {
                v_cur_data[x * height + y] = v_eul_array(y, x);
            }
        }
        matvar_t* v_cur_mat = Mat_VarCreate("", MAT_C_DOUBLE, MAT_T_DOUBLE, 2, data_dims, v_cur_data, MAT_F_DONT_COPY_DATA);
        Mat_VarSetCell(plot_v_cur, i, v_cur_mat);
        
        // Create ROI mask matrices
        
        // Raw ROI (roi_dic) - from Lagrangian
        uint8_t* roi_dic_data = new uint8_t[height * width];
        for (size_t y = 0; y < height; ++y) {
            for (size_t x = 0; x < width; ++x) {
                roi_dic_data[x * height + y] = roi_lag_mask(y, x) ? 1 : 0;
            }
        }
        matvar_t* roi_dic_mat = Mat_VarCreate("", MAT_C_UINT8, MAT_T_UINT8, 2, data_dims, roi_dic_data, MAT_F_DONT_COPY_DATA);
        Mat_VarSetCell(roi_dic, i, roi_dic_mat);
        
        // Lagrangian ROI (roi_ref_formatted)
        uint8_t* roi_ref_data = new uint8_t[height * width];
        for (size_t y = 0; y < height; ++y) {
            for (size_t x = 0; x < width; ++x) {
                roi_ref_data[x * height + y] = roi_lag_mask(y, x) ? 1 : 0;
            }
        }
        matvar_t* roi_ref_mat = Mat_VarCreate("", MAT_C_UINT8, MAT_T_UINT8, 2, data_dims, roi_ref_data, MAT_F_DONT_COPY_DATA);
        Mat_VarSetCell(roi_ref, i, roi_ref_mat);
        
        // Eulerian ROI (roi_cur_formatted)
        uint8_t* roi_cur_data = new uint8_t[height * width];
        for (size_t y = 0; y < height; ++y) {
            for (size_t x = 0; x < width; ++x) {
                roi_cur_data[x * height + y] = roi_eul_mask(y, x) ? 1 : 0;
            }
        }
        matvar_t* roi_cur_mat = Mat_VarCreate("", MAT_C_UINT8, MAT_T_UINT8, 2, data_dims, roi_cur_data, MAT_F_DONT_COPY_DATA);
        Mat_VarSetCell(roi_cur, i, roi_cur_mat);
        
        // Correlation coefficient from Lagrangian
        double* cc_data = new double[height * width];
        for (size_t y = 0; y < height; ++y) {
            for (size_t x = 0; x < width; ++x) {
                cc_data[x * height + y] = cc_lag_array(y, x);
            }
        }
        matvar_t* cc_mat = Mat_VarCreate("", MAT_C_DOUBLE, MAT_T_DOUBLE, 2, data_dims, cc_data, MAT_F_DONT_COPY_DATA);
        Mat_VarSetCell(plot_corrcoef, i, cc_mat);
    }
    
    // Create struct and populate with all fields
    std::vector<std::string> field_names = {
        "plot_corrcoef_dic", 
        "plot_u_dic", "plot_v_dic",
        "plot_u_ref_formatted", "plot_v_ref_formatted", 
        "plot_u_cur_formatted", "plot_v_cur_formatted",
        "roi_dic",
        "roi_ref_formatted", "roi_cur_formatted"
    };
    
    matvar_t* displacements = createStructVariable("displacements", field_names);
    Mat_VarSetStructFieldByName(displacements, "plot_corrcoef_dic", 0, plot_corrcoef);
    Mat_VarSetStructFieldByName(displacements, "plot_u_dic", 0, plot_u_dic);
    Mat_VarSetStructFieldByName(displacements, "plot_v_dic", 0, plot_v_dic);
    Mat_VarSetStructFieldByName(displacements, "plot_u_ref_formatted", 0, plot_u_ref);
    Mat_VarSetStructFieldByName(displacements, "plot_v_ref_formatted", 0, plot_v_ref);
    Mat_VarSetStructFieldByName(displacements, "plot_u_cur_formatted", 0, plot_u_cur);
    Mat_VarSetStructFieldByName(displacements, "plot_v_cur_formatted", 0, plot_v_cur);
    Mat_VarSetStructFieldByName(displacements, "roi_dic", 0, roi_dic);
    Mat_VarSetStructFieldByName(displacements, "roi_ref_formatted", 0, roi_ref);
    Mat_VarSetStructFieldByName(displacements, "roi_cur_formatted", 0, roi_cur);
    
    return displacements;
}

bool MatWriter::convertBinToMat(const std::string& bin_path,
                                const std::string& mat_path,
                                const ncorr::DIC_analysis_input& dic_input) {
    // Load DIC output from binary
    ncorr::DIC_analysis_output dic_output;
    try {
        dic_output = ncorr::DIC_analysis_output::load(bin_path);
    } catch (const std::exception& e) {
        std::cerr << "Failed to load .bin file: " << bin_path << ": " << e.what() << std::endl;
        return false;
    }
    
    // Write to .mat format
    bool success = writeDICResultFile(mat_path, dic_input, dic_output);
    
    if (success) {
        std::cout << "Converted " << bin_path << " -> " << mat_path << std::endl;
    }
    
    return success;
}

bool MatWriter::writeDIC2DPairResults(const std::string& filename,
                                      const DIC2DPairResults& results) {
    return io::mat::MatResultsWriter::writeDIC2DPairResults(filename, results);
}

matvar_t* MatWriter::buildCombinedStructFields(mat_t* matfp,
                                                 const DIC3Dcombined& combined,
                                                 const std::string& struct_name,
                                                 const std::vector<std::string>& extra_fields) {
    return io::mat::MatResultsWriter::buildCombinedStructFields(
        matfp, combined, struct_name, extra_fields);
}

bool MatWriter::write3DCombinedResults(const std::string& filename,
                                       const DIC3Dcombined& combined,
                                       const std::string& struct_name) {
    return io::mat::MatResultsWriter::write3DCombinedResults(filename, combined, struct_name);
}

bool MatWriter::write3DPPresults(const std::string& filename,
                                const DIC3DPPresults& ppresults) {
    return io::mat::MatResultsWriter::write3DPPresults(filename, ppresults);
}

matvar_t* MatWriter::buildDeformationStruct(const std::string& group_name,
                                              const FrameDeformationResult& deform_data) {
    return io::mat::MatResultsWriter::buildDeformationStruct(group_name, deform_data);
}

bool MatWriter::writeDeformationGroup(mat_t* matfp,
                                      const std::string& group_name,
                                      const FrameDeformationResult& deform_data) {
    if (!matfp) {
        std::cerr << "Invalid MAT file pointer" << std::endl;
        return false;
    }
    
    matvar_t* deform_struct = buildDeformationStruct(group_name, deform_data);
    if (!deform_struct) {
        std::cerr << "Failed to build deformation struct" << std::endl;
        return false;
    }
    
    Mat_VarWrite(matfp, deform_struct, MAT_COMPRESSION_NONE);
    Mat_VarFree(deform_struct);
    
    std::cout << "Successfully wrote all deformation fields to '" << group_name << "' group" << std::endl;
    return true;
}

bool MatWriter::writeDisplacementGroup(mat_t* matfp,
                                       const std::vector<std::vector<Eigen::Vector3d>>& disp_vec,
                                       const std::vector<std::vector<double>>& disp_mgn,
                                       size_t n_frames) {
    if (!matfp) {
        std::cerr << "Invalid MAT file pointer" << std::endl;
        return false;
    }
    
    std::cout << "Writing displacement group with " << n_frames << " frames" << std::endl;
    
    // Create DispVec cell array (Nx3 per frame)
    matvar_t* disp_vec_cell = createCellArrayFromVectors("DispVec", disp_vec, n_frames);
    if (!disp_vec_cell) {
        std::cerr << "Failed to create DispVec cell array" << std::endl;
        return false;
    }
    
    // Create DispMgn cell array (Nx1 per frame)
    matvar_t* disp_mgn_cell = createCellArrayFromScalars("DispMgn", disp_mgn, n_frames);
    if (!disp_mgn_cell) {
        std::cerr << "Failed to create DispMgn cell array" << std::endl;
        Mat_VarFree(disp_vec_cell);
        return false;
    }
    
    // Write both to file
    Mat_VarWrite(matfp, disp_vec_cell, MAT_COMPRESSION_NONE);
    Mat_VarWrite(matfp, disp_mgn_cell, MAT_COMPRESSION_NONE);
    
    // Free cell arrays
    Mat_VarFree(disp_vec_cell);
    Mat_VarFree(disp_mgn_cell);
    
    std::cout << "Successfully wrote displacement fields (DispVec, DispMgn)" << std::endl;
    return true;
}

bool MatWriter::writeFaceArrays(mat_t* matfp,
                               const std::vector<std::vector<Eigen::Vector3d>>& face_centroids,
                               const std::vector<std::vector<double>>& face_corr_comb,
                               const std::vector<std::vector<double>>& face_iso_ind,
                               size_t n_frames) {
    if (!matfp) {
        std::cerr << "Invalid MAT file pointer" << std::endl;
        return false;
    }
    
    std::cout << "Writing face-based arrays with " << n_frames << " frames" << std::endl;
    
    // Create FaceCentroids cell array (Mx3 per frame)
    matvar_t* face_centroids_cell = createCellArrayFromVectors("FaceCentroids", face_centroids, n_frames);
    if (!face_centroids_cell) {
        std::cerr << "Failed to create FaceCentroids cell array" << std::endl;
        return false;
    }
    
    // Create FaceCorrComb cell array (Mx1 per frame)
    matvar_t* face_corr_comb_cell = createCellArrayFromScalars("FaceCorrComb", face_corr_comb, n_frames);
    if (!face_corr_comb_cell) {
        std::cerr << "Failed to create FaceCorrComb cell array" << std::endl;
        Mat_VarFree(face_centroids_cell);
        return false;
    }
    
    // Create FaceIsoInd cell array (Mx1 per frame)
    matvar_t* face_iso_ind_cell = createCellArrayFromScalars("FaceIsoInd", face_iso_ind, n_frames);
    if (!face_iso_ind_cell) {
        std::cerr << "Failed to create FaceIsoInd cell array" << std::endl;
        Mat_VarFree(face_centroids_cell);
        Mat_VarFree(face_corr_comb_cell);
        return false;
    }
    
    // Write all to file
    Mat_VarWrite(matfp, face_centroids_cell, MAT_COMPRESSION_NONE);
    Mat_VarWrite(matfp, face_corr_comb_cell, MAT_COMPRESSION_NONE);
    Mat_VarWrite(matfp, face_iso_ind_cell, MAT_COMPRESSION_NONE);
    
    // Free cell arrays
    Mat_VarFree(face_centroids_cell);
    Mat_VarFree(face_corr_comb_cell);
    Mat_VarFree(face_iso_ind_cell);
    
    std::cout << "Successfully wrote face arrays (FaceCentroids, FaceCorrComb, FaceIsoInd)" << std::endl;
    return true;
}

bool MatWriter::writeCalibrationGroup(mat_t* matfp,
                                      const CalibrationData& calibration) {
    if (!matfp) {
        std::cerr << "Invalid MAT file pointer" << std::endl;
        return false;
    }
    
    std::cout << "Writing calibration group..." << std::endl;
    
    // Determine dimensions (should be 2x2 for stereo pairs)
    size_t rows = calibration.DLT_paths.size();
    size_t cols = (rows > 0) ? calibration.DLT_paths[0].size() : 0;
    
    if (rows == 0 || cols == 0) {
        std::cout << "Warning: Empty calibration data, skipping" << std::endl;
        return true;
    }
    
    // Create DLTpath cell array (2x2 strings)
    matvar_t* dlt_path_cell = createCellArray2DFromStrings("DLTpath", calibration.DLT_paths, rows, cols);
    if (!dlt_path_cell) {
        std::cerr << "Failed to create DLTpath cell array" << std::endl;
        return false;
    }
    
    // Create DLTparameters cell array (2x2 double vectors)
    matvar_t* dlt_params_cell = createCellArray2DFromVectors("DLTparameters", calibration.DLT_params, rows, cols);
    if (!dlt_params_cell) {
        std::cerr << "Failed to create DLTparameters cell array" << std::endl;
        Mat_VarFree(dlt_path_cell);
        return false;
    }
    
    // Create calibration struct/group to wrap both fields
    std::vector<std::string> calib_fields = {"DLTpath", "DLTparameters"};
    matvar_t* calib_struct = createStructVariable("calibration", calib_fields);
    if (!calib_struct) {
        std::cerr << "Failed to create calibration struct" << std::endl;
        Mat_VarFree(dlt_path_cell);
        Mat_VarFree(dlt_params_cell);
        return false;
    }
    
    // Add fields to calibration struct
    Mat_VarSetStructFieldByName(calib_struct, "DLTpath", 0, dlt_path_cell);
    Mat_VarSetStructFieldByName(calib_struct, "DLTparameters", 0, dlt_params_cell);
    
    // Write calibration struct to file (writes as HDF5 group)
    Mat_VarWrite(matfp, calib_struct, MAT_COMPRESSION_NONE);
    Mat_VarFree(calib_struct);
    
    std::cout << "Successfully wrote calibration group (DLTpath, DLTparameters)" << std::endl;
    return true;
}

bool MatWriter::writeDistortionGroup(mat_t* matfp,
                                    const DistortionData& distortion) {
    if (!matfp) {
        std::cerr << "Invalid MAT file pointer" << std::endl;
        return false;
    }
    
    std::cout << "Writing distortion group..." << std::endl;
    
    // Determine dimensions (should be 2x2 for stereo pairs)
    size_t rows = distortion.distortion_models.size();
    size_t cols = (rows > 0) ? distortion.distortion_models[0].size() : 0;
    
    if (rows == 0 || cols == 0) {
        std::cout << "Warning: Empty distortion data, skipping" << std::endl;
        return true;
    }
    
    // Create distortionModel cell array (2x2 strings)
    matvar_t* dist_model_cell = createCellArray2DFromStrings("distortionModel", distortion.distortion_models, rows, cols);
    if (!dist_model_cell) {
        std::cerr << "Failed to create distortionModel cell array" << std::endl;
        return false;
    }
    
    // Create distortionPath cell array (2x2 strings)
    matvar_t* dist_path_cell = createCellArray2DFromStrings("distortionPath", distortion.distortion_paths, rows, cols);
    if (!dist_path_cell) {
        std::cerr << "Failed to create distortionPath cell array" << std::endl;
        Mat_VarFree(dist_model_cell);
        return false;
    }
    
    // Create distortion struct/group to wrap both fields
    std::vector<std::string> dist_fields = {"distortionModel", "distortionPath"};
    matvar_t* dist_struct = createStructVariable("distortion", dist_fields);
    if (!dist_struct) {
        std::cerr << "Failed to create distortion struct" << std::endl;
        Mat_VarFree(dist_model_cell);
        Mat_VarFree(dist_path_cell);
        return false;
    }
    
    // Add fields to distortion struct
    Mat_VarSetStructFieldByName(dist_struct, "distortionModel", 0, dist_model_cell);
    Mat_VarSetStructFieldByName(dist_struct, "distortionPath", 0, dist_path_cell);
    
    // Write distortion struct to file (writes as HDF5 group)
    Mat_VarWrite(matfp, dist_struct, MAT_COMPRESSION_NONE);
    Mat_VarFree(dist_struct);
    
    std::cout << "Successfully wrote distortion group (distortionModel, distortionPath)" << std::endl;
    return true;
}

bool MatWriter::writeAllPairsResults(mat_t* matfp,
                                    const std::vector<DIC3DpairResults>& all_pairs) {
    return io::mat::MatResultsWriter::writeAllPairsResults(matfp, all_pairs);
}

bool MatWriter::writeDIC2Dinfo(mat_t* matfp,
                              const std::vector<DIC2DPairResults>& dic2d_info) {
    return io::mat::MatResultsWriter::writeDIC2Dinfo(matfp, dic2d_info);
}

// Helper function implementations ============================================

ncorr::ROI2D MatWriter::convertMatToROI2D(const cv::Mat& mask) {
    // Convert cv::Mat (CV_8U) to ncorr::Array2D<bool>
    ncorr::Array2D<bool> mask_array(mask.rows, mask.cols);
    
    for (int i = 0; i < mask.rows; ++i) {
        for (int j = 0; j < mask.cols; ++j) {
            mask_array(i, j) = (mask.at<uint8_t>(i, j) > 0);
        }
    }
    
    // Create ROI2D from mask array
    return ncorr::ROI2D(std::move(mask_array));
}

cv::Mat MatWriter::convertROI2DToMat(const ncorr::ROI2D& roi) {
    // Extract mask from ROI2D
    const auto& mask_array = roi.get_mask();
    
    // Create cv::Mat (CV_8U)
    cv::Mat mask(mask_array.height(), mask_array.width(), CV_8UC1);
    
    for (ncorr::ROI2D::difference_type i = 0; i < mask_array.height(); ++i) {
        for (ncorr::ROI2D::difference_type j = 0; j < mask_array.width(); ++j) {
            mask.at<uint8_t>(i, j) = mask_array(i, j) ? 255 : 0;
        }
    }
    
    return mask;
}

// Cell Array Helper Functions ================================================

matvar_t* MatWriter::createCellArrayFromScalars(
    const std::string& name,
    const std::vector<std::vector<double>>& data,
    size_t n_frames) {
    return io::mat::createCellArrayFromScalars(name, data, n_frames);
}

matvar_t* MatWriter::createCellArrayFromVectors(
    const std::string& name,
    const std::vector<std::vector<Eigen::Vector3d>>& data,
    size_t n_frames) {
    return io::mat::createCellArrayFromVectors(name, data, n_frames);
}

matvar_t* MatWriter::createCellArrayFromMatrices(
    const std::string& name,
    const std::vector<std::vector<Eigen::Matrix3d>>& data,
    size_t n_frames) {
    return io::mat::createCellArrayFromMatrices(name, data, n_frames);
}

matvar_t* MatWriter::createCellArray2DFromStrings(
    const std::string& name,
    const std::vector<std::vector<std::string>>& data,
    size_t rows,
    size_t cols) {
    return io::mat::createCellArray2DFromStrings(name, data, rows, cols);
}

matvar_t* MatWriter::createCellArray2DFromVectors(
    const std::string& name,
    const std::vector<std::vector<std::vector<double>>>& data,
    size_t rows,
    size_t cols) {
    return io::mat::createCellArray2DFromVectors(name, data, rows, cols);
}

} // namespace cppxdic
