/**
 * MAT File Writer for CPPXDIC
 * Implementation of MAT file writing in xDIC format
 * Produces MATLAB v7.3 (HDF5) compatible output
 */

#include "mat_writer.h"
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
    
    // Format dispinfo and displacements
    matvar_t* dispinfo_var = formatDispInfo(dispinfo);
    matvar_t* displacements_var = formatDisplacements(dic_output);
    
    // Write using common function
    bool success = writeDicNcorrFile(filename, ref_img, cur_img, ref_roi, cur_roi,
                                     dispinfo_var, displacements_var, dic_output);
    
    if (success) {
        std::cout << "Wrote MATCHING file: " << filename << std::endl;
    }
    
    return success;
}

bool MatWriter::writeMatchingFile(const std::string& filename,
                                 const cv::Mat& ref_img,
                                 const cv::Mat& cur_img,
                                 const cv::Mat& ref_roi,
                                 const cv::Mat& cur_roi,
                                 const ncorr::DIC_analysis_output& dic_lagrangian,
                                 const ncorr::DIC_analysis_output& dic_eulerian,
                                 const std::map<std::string, double>& dispinfo) {
    
    // Format dispinfo and displacements with both perspectives
    matvar_t* dispinfo_var = formatDispInfo(dispinfo);
    matvar_t* displacements_var = formatDisplacements(dic_lagrangian, dic_eulerian);
    
    // Write using common function (use lagrangian for ROI update)
    bool success = writeDicNcorrFile(filename, ref_img, cur_img, ref_roi, cur_roi,
                                     dispinfo_var, displacements_var, dic_lagrangian);
    
    if (success) {
        std::cout << "Wrote MATCHING file with both perspectives: " << filename << std::endl;
    }
    
    return success;
}

bool MatWriter::writeDicNcorrFile(const std::string& filename,
                                 const cv::Mat& ref_img,
                                 const cv::Mat& cur_img,
                                 const cv::Mat& ref_roi,
                                 const cv::Mat& cur_roi,
                                 matvar_t* dispinfo_var,
                                 matvar_t* displacements_var,
                                 const ncorr::DIC_analysis_output& dic_output) {
    
    // Create MAT file (v7.3 HDF5 format for xDIC compatibility)
    mat_t* matfp = createMatFileHDF5(filename);
    if (!matfp) {
        std::cerr << "Failed to create MAT file: " << filename << std::endl;
        return false;
    }
    
    // ===== CREATE REFERENCE_SAVE STRUCT =====
    std::vector<std::string> ref_fields = {"gs", "name", "path", "roi", "type"};
    matvar_t* reference_save = createStructVariable("reference_save", ref_fields);
    
    // Add ref image
    writeMatVariable(matfp, "ref_gs_temp", ref_img);
    matvar_t* ref_gs = Mat_VarRead(matfp, "ref_gs_temp");
    addFieldToStruct(reference_save, "gs", ref_gs, 0);
    
    // Add ref ROI
    std::vector<std::string> roi_fields = {"mask"};
    matvar_t* ref_roi_struct = createStructVariable("roi", roi_fields);
    writeMatVariable(matfp, "ref_roi_temp", ref_roi);
    matvar_t* ref_roi_mask = Mat_VarRead(matfp, "ref_roi_temp");
    addFieldToStruct(ref_roi_struct, "mask", ref_roi_mask, 0);
    addFieldToStruct(reference_save, "roi", ref_roi_struct, 0);
    
    // ===== UPDATE CURRENT ROI WITH DISPLACEMENT FIELD =====
    cv::Mat cur_roi_updated = cur_roi.clone();  // Default to original
    if (!dic_output.disps.empty()) {
        try {
            // Convert cv::Mat to ncorr::ROI2D
            ncorr::ROI2D roi_current = convertMatToROI2D(cur_roi);
            
            // Apply ROI update using first displacement field
            // This updates the ROI boundary based on displacement interpolation
            ncorr::ROI2D roi_updated = ncorr::update(
                roi_current, 
                dic_output.disps[0],           // First displacement field
                ncorr::INTERP::CUBIC_KEYS      // Cubic interpolation for smooth boundaries
            );
            
            // Convert back to cv::Mat
            cur_roi_updated = convertROI2DToMat(roi_updated);
            
            std::cout << "  Applied ROI update with displacement field" << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "  Warning: ROI update failed: " << e.what() << std::endl;
            std::cerr << "  Using original ROI instead" << std::endl;
            // cur_roi_updated remains as clone of cur_roi
        }
    }
    
    // ===== CREATE CURRENT_SAVE STRUCT =====
    matvar_t* current_save = createStructVariable("current_save", ref_fields);
    
    // Add cur image
    writeMatVariable(matfp, "cur_gs_temp", cur_img);
    matvar_t* cur_gs = Mat_VarRead(matfp, "cur_gs_temp");
    addFieldToStruct(current_save, "gs", cur_gs, 0);
    
    // Add cur ROI (updated)
    matvar_t* cur_roi_struct = createStructVariable("roi", roi_fields);
    writeMatVariable(matfp, "cur_roi_temp", cur_roi_updated);  // Write UPDATED ROI
    matvar_t* cur_roi_mask = Mat_VarRead(matfp, "cur_roi_temp");
    addFieldToStruct(cur_roi_struct, "mask", cur_roi_mask, 0);
    addFieldToStruct(current_save, "roi", cur_roi_struct, 0);
    
    // ===== CREATE DATA_DIC_SAVE STRUCT =====
    std::vector<std::string> data_fields = {"dispinfo", "displacements", "straininfo", "strains"};
    matvar_t* data_dic_save = createStructVariable("data_dic_save", data_fields);
    
    // Add dispinfo and displacements (passed as arguments)
    addFieldToStruct(data_dic_save, "dispinfo", dispinfo_var, 0);
    addFieldToStruct(data_dic_save, "displacements", displacements_var, 0);
    
    // Add empty straininfo struct (placeholder for compatibility)
    std::vector<std::string> straininfo_fields = {"radius", "subsettrunc"};
    matvar_t* straininfo_var = createStructVariable("straininfo", straininfo_fields);
    addFieldToStruct(data_dic_save, "straininfo", straininfo_var, 0);
    
    // Add empty strains struct (placeholder for compatibility)
    std::vector<std::string> strains_fields = {
        "plot_exx_ref_formatted", "plot_exy_ref_formatted", "plot_eyy_ref_formatted",
        "roi_ref_formatted", "plot_exx_cur_formatted", "plot_exy_cur_formatted",
        "plot_eyy_cur_formatted", "roi_cur_formatted"
    };
    matvar_t* strains_var = createStructVariable("strains", strains_fields);
    addFieldToStruct(data_dic_save, "strains", strains_var, 0);
    
    // ===== WRITE ALL STRUCTS TO FILE =====
    Mat_VarWrite(matfp, reference_save, MAT_COMPRESSION_NONE);
    Mat_VarWrite(matfp, current_save, MAT_COMPRESSION_NONE);
    Mat_VarWrite(matfp, data_dic_save, MAT_COMPRESSION_NONE);
    
    // Clean up
    Mat_VarFree(reference_save);
    Mat_VarFree(current_save);
    Mat_VarFree(data_dic_save);
    
    Mat_Close(matfp);
    
    return true;
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
    if (mat.empty()) {
        std::cerr << "Cannot write empty Mat" << std::endl;
        return false;
    }
    
    // Convert OpenCV Mat to matio format
    // Note: MATLAB is column-major, OpenCV is row-major
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
    
    // Transpose data for MATLAB column-major format
    cv::Mat transposed = mat.t();
    
    matvar_t* matvar = Mat_VarCreate(varname.c_str(), mat_class, mat_type,
                                     2, dims.data(), transposed.data, 0);
    if (!matvar) {
        std::cerr << "Failed to create variable: " << varname << std::endl;
        return false;
    }
    
    int result = Mat_VarWrite(matfp, matvar, MAT_COMPRESSION_NONE);
    Mat_VarFree(matvar);
    
    return (result == 0);
}

bool MatWriter::writeArrayVariable(mat_t* matfp,
                                  const std::string& varname,
                                  const void* data,
                                  const std::vector<size_t>& dims,
                                  matio_types data_type,
                                  matio_classes class_type) {
    int rank = dims.size();
    
    matvar_t* matvar = Mat_VarCreate(varname.c_str(), class_type, data_type,
                                     rank, dims.data(), (void*)data, 0);
    if (!matvar) {
        std::cerr << "Failed to create array variable: " << varname << std::endl;
        return false;
    }
    
    int result = Mat_VarWrite(matfp, matvar, MAT_COMPRESSION_NONE);
    Mat_VarFree(matvar);
    
    return (result == 0);
}

bool MatWriter::writeStringVariable(mat_t* matfp,
                                   const std::string& varname,
                                   const std::string& str) {
    std::vector<size_t> dims = {1, str.length()};
    
    matvar_t* matvar = Mat_VarCreate(varname.c_str(), MAT_C_CHAR, MAT_T_UTF8,
                                     2, dims.data(), (void*)str.c_str(), 0);
    if (!matvar) {
        std::cerr << "Failed to create string variable: " << varname << std::endl;
        return false;
    }
    
    int result = Mat_VarWrite(matfp, matvar, MAT_COMPRESSION_NONE);
    Mat_VarFree(matvar);
    
    return (result == 0);
}

bool MatWriter::writeScalarVariable(mat_t* matfp,
                                   const std::string& varname,
                                   double value) {
    std::vector<size_t> dims = {1, 1};
    
    matvar_t* matvar = Mat_VarCreate(varname.c_str(), MAT_C_DOUBLE, MAT_T_DOUBLE,
                                     2, dims.data(), &value, 0);
    if (!matvar) {
        std::cerr << "Failed to create scalar variable: " << varname << std::endl;
        return false;
    }
    
    int result = Mat_VarWrite(matfp, matvar, MAT_COMPRESSION_NONE);
    Mat_VarFree(matvar);
    
    return (result == 0);
}

matvar_t* MatWriter::createStructVariable(const std::string& struct_name,
                                         const std::vector<std::string>& field_names) {
    // Create struct variable
    std::vector<size_t> dims = {1, 1};  // Scalar struct
    
    // Convert field names to char** format
    std::vector<const char*> c_field_names;
    for (const auto& name : field_names) {
        c_field_names.push_back(name.c_str());
    }
    
    matvar_t* struct_var = Mat_VarCreateStruct(struct_name.c_str(),
                                               2, dims.data(),
                                               c_field_names.data(),
                                               field_names.size());
    
    return struct_var;
}

bool MatWriter::addFieldToStruct(matvar_t* struct_var,
                                const std::string& field_name,
                                matvar_t* field_var,
                                size_t index) {
    if (!struct_var || struct_var->class_type != MAT_C_STRUCT) {
        std::cerr << "Error: Not a struct variable" << std::endl;
        return false;
    }
    
    if (!field_var) {
        std::cerr << "Error: Null field variable" << std::endl;
        return false;
    }
    
    // Use matio API to set struct field
    Mat_VarSetStructFieldByName(struct_var, field_name.c_str(), index, field_var);
    return true;
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
        
        // Correlation coefficient (placeholder - all ones for now)
        double* cc_data = new double[height * width];
        std::fill_n(cc_data, height * width, 1.0);
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
    mat_t* matfp = createMatFileHDF5(filename);
    if (!matfp) {
        std::cerr << "Failed to create DIC2DPairResults file: " << filename << std::endl;
        return false;
    }
    
    // Write scalar fields
    writeScalarVariable(matfp, "nCamRef", results.nCamRef);
    writeScalarVariable(matfp, "nCamDef", results.nCamDef);
    writeScalarVariable(matfp, "nImages", results.nImages);
    
    // Write ROI mask
    if (!results.ROImask.empty()) {
        writeMatVariable(matfp, "ROImask", results.ROImask);
    }
    
    // Write Points as cell array
    size_t n_frames = results.Points.size();
    std::vector<size_t> cell_dims = {1, n_frames};
    matvar_t* points_cell = Mat_VarCreate("Points", MAT_C_CELL, MAT_T_CELL, 2, cell_dims.data(), nullptr, 0);
    
    for (size_t i = 0; i < n_frames; ++i) {
        const auto& pts = results.Points[i];
        std::vector<std::string> pt_fields = {"x", "y"};
        matvar_t* pt_struct = createStructVariable("point", pt_fields);
        
        // Write x and y arrays
        std::vector<size_t> dims = {pts.x.size(), 1};
        matvar_t* x_var = Mat_VarCreate("x", MAT_C_DOUBLE, MAT_T_DOUBLE, 2, dims.data(), 
                                        (void*)pts.x.data(), 0);
        matvar_t* y_var = Mat_VarCreate("y", MAT_C_DOUBLE, MAT_T_DOUBLE, 2, dims.data(), 
                                        (void*)pts.y.data(), 0);
        
        Mat_VarSetCell(points_cell, i, pt_struct);
        Mat_VarFree(x_var);
        Mat_VarFree(y_var);
    }
    
    Mat_VarWrite(matfp, points_cell, MAT_COMPRESSION_NONE);
    Mat_VarFree(points_cell);
    
    // Write CorCoeffVec
    if (!results.CorCoeffVec.empty()) {
        std::vector<size_t> dims = {results.CorCoeffVec.size(), 1};
        writeArrayVariable(matfp, "CorCoeffVec", results.CorCoeffVec.data(), dims, 
                          MAT_T_DOUBLE, MAT_C_DOUBLE);
    }
    
    // Write Faces (Nx3 matrix)
    if (!results.Faces.empty()) {
        size_t n_faces = results.Faces.size() / 3;
        std::vector<size_t> dims = {n_faces, 3};
        writeArrayVariable(matfp, "Faces", results.Faces.data(), dims, 
                          MAT_T_INT32, MAT_C_INT32);
    }
    
    // Write FaceColors
    if (!results.FaceColors.empty()) {
        std::vector<size_t> dims = {results.FaceColors.size(), 1};
        writeArrayVariable(matfp, "FaceColors", results.FaceColors.data(), dims, 
                          MAT_T_DOUBLE, MAT_C_DOUBLE);
    }
    
    Mat_Close(matfp);
    std::cout << "Wrote DIC2DPairResults: " << filename << std::endl;
    return true;
}

bool MatWriter::write3DCombinedResults(const std::string& filename,
                                       const DIC3Dcombined& combined) {
    mat_t* matfp = createMatFileHDF5(filename);
    if (!matfp) {
        std::cerr << "Failed to create DIC3Dcombined file: " << filename << std::endl;
        return false;
    }
    
    // Write pairIndices
    if (!combined.pairIndices.empty()) {
        std::vector<size_t> dims = {combined.pairIndices.size(), 1};
        writeArrayVariable(matfp, "pairIndices", combined.pairIndices.data(), dims, 
                          MAT_T_INT32, MAT_C_INT32);
    }
    
    // Write Points3D as cell array
    size_t n_frames = combined.Points3D.size();
    std::vector<size_t> cell_dims = {1, n_frames};
    matvar_t* points3d_cell = Mat_VarCreate("Points3D", MAT_C_CELL, MAT_T_CELL, 2, cell_dims.data(), nullptr, 0);
    
    for (size_t i = 0; i < n_frames; ++i) {
        const auto& pts = combined.Points3D[i];
        std::vector<std::string> pt_fields = {"x", "y", "z"};
        matvar_t* pt_struct = createStructVariable("point3d", pt_fields);
        
        // Write x, y, z arrays if they contain data
        size_t n_points = pts.x.size();
        if (n_points > 0) {
            std::vector<size_t> dims = {n_points, 1};
            
            // Create x array
            matvar_t* x_var = Mat_VarCreate("x", MAT_C_DOUBLE, MAT_T_DOUBLE, 2, dims.data(), 
                                            (void*)pts.x.data(), 0);
            Mat_VarSetStructFieldByName(pt_struct, "x", 0, x_var);
            
            // Create y array
            matvar_t* y_var = Mat_VarCreate("y", MAT_C_DOUBLE, MAT_T_DOUBLE, 2, dims.data(), 
                                            (void*)pts.y.data(), 0);
            Mat_VarSetStructFieldByName(pt_struct, "y", 0, y_var);
            
            // Create z array
            matvar_t* z_var = Mat_VarCreate("z", MAT_C_DOUBLE, MAT_T_DOUBLE, 2, dims.data(), 
                                            (void*)pts.z.data(), 0);
            Mat_VarSetStructFieldByName(pt_struct, "z", 0, z_var);
        }
        
        Mat_VarSetCell(points3d_cell, i, pt_struct);
    }
    
    Mat_VarWrite(matfp, points3d_cell, MAT_COMPRESSION_NONE);
    Mat_VarFree(points3d_cell);
    
    // Write Faces
    if (!combined.Faces.empty()) {
        size_t n_faces = combined.Faces.size() / 3;
        std::vector<size_t> dims = {n_faces, 3};
        writeArrayVariable(matfp, "Faces", combined.Faces.data(), dims, 
                          MAT_T_INT32, MAT_C_INT32);
    }
    
    // Write FaceColors
    if (!combined.FaceColors.empty()) {
        std::vector<size_t> dims = {combined.FaceColors.size(), 1};
        writeArrayVariable(matfp, "FaceColors", combined.FaceColors.data(), dims, 
                          MAT_T_DOUBLE, MAT_C_DOUBLE);
    }
    
    // Write corrComb as cell array
    if (!combined.corrComb.empty()) {
        size_t n_frames_corr = combined.corrComb.size();
        std::vector<size_t> cell_dims = {1, n_frames_corr};
        matvar_t* corr_cell = Mat_VarCreate("corrComb", MAT_C_CELL, MAT_T_CELL, 2, cell_dims.data(), nullptr, 0);
        
        for (size_t i = 0; i < n_frames_corr; ++i) {
            const auto& corr_frame = combined.corrComb[i];
            if (!corr_frame.empty()) {
                std::vector<size_t> dims = {corr_frame.size(), 1};
                matvar_t* corr_var = Mat_VarCreate("", MAT_C_DOUBLE, MAT_T_DOUBLE, 2, dims.data(), 
                                                  (void*)corr_frame.data(), 0);
                Mat_VarSetCell(corr_cell, i, corr_var);
            }
        }
        Mat_VarWrite(matfp, corr_cell, MAT_COMPRESSION_NONE);
        Mat_VarFree(corr_cell);
    }
    
    // Write FaceCorrComb as cell array
    if (!combined.FaceCorrComb.empty()) {
        size_t n_frames_face = combined.FaceCorrComb.size();
        std::vector<size_t> cell_dims = {1, n_frames_face};
        matvar_t* face_corr_cell = Mat_VarCreate("FaceCorrComb", MAT_C_CELL, MAT_T_CELL, 2, cell_dims.data(), nullptr, 0);
        
        for (size_t i = 0; i < n_frames_face; ++i) {
            const auto& face_corr = combined.FaceCorrComb[i];
            if (!face_corr.empty()) {
                std::vector<size_t> dims = {face_corr.size(), 1};
                matvar_t* fc_var = Mat_VarCreate("", MAT_C_DOUBLE, MAT_T_DOUBLE, 2, dims.data(), 
                                                (void*)face_corr.data(), 0);
                Mat_VarSetCell(face_corr_cell, i, fc_var);
            }
        }
        Mat_VarWrite(matfp, face_corr_cell, MAT_COMPRESSION_NONE);
        Mat_VarFree(face_corr_cell);
    }
    
    // Write FaceCentroids as cell array
    if (!combined.FaceCentroids.empty()) {
        size_t n_frames_cent = combined.FaceCentroids.size();
        std::vector<size_t> cell_dims = {1, n_frames_cent};
        matvar_t* cent_cell = Mat_VarCreate("FaceCentroids", MAT_C_CELL, MAT_T_CELL, 2, cell_dims.data(), nullptr, 0);
        
        for (size_t i = 0; i < n_frames_cent; ++i) {
            const auto& centroids = combined.FaceCentroids[i];
            if (!centroids.empty()) {
                std::vector<size_t> dims = {centroids.size(), 1};
                matvar_t* cent_var = Mat_VarCreate("", MAT_C_DOUBLE, MAT_T_DOUBLE, 2, dims.data(), 
                                                  (void*)centroids.data(), 0);
                Mat_VarSetCell(cent_cell, i, cent_var);
            }
        }
        Mat_VarWrite(matfp, cent_cell, MAT_COMPRESSION_NONE);
        Mat_VarFree(cent_cell);
    }
    
    // Write Displacement data
    std::vector<std::string> disp_fields = {"DispVec", "DispMgn"};
    matvar_t* disp_struct = createStructVariable("Disp", disp_fields);
    
    // Write DispVec if available
    if (!combined.Disp.DispVec.empty()) {
        size_t n_disp = combined.Disp.DispVec.size();
        std::vector<size_t> cell_dims = {1, n_disp};
        matvar_t* disp_vec_cell = Mat_VarCreate("DispVec", MAT_C_CELL, MAT_T_CELL, 2, cell_dims.data(), nullptr, 0);
        
        for (size_t i = 0; i < n_disp; ++i) {
            const auto& disp_vec = combined.Disp.DispVec[i];
            if (!disp_vec.empty()) {
                std::vector<size_t> dims = {disp_vec.size(), 1};
                matvar_t* dv_var = Mat_VarCreate("", MAT_C_DOUBLE, MAT_T_DOUBLE, 2, dims.data(), 
                                                (void*)disp_vec.data(), 0);
                Mat_VarSetCell(disp_vec_cell, i, dv_var);
            }
        }
        Mat_VarSetStructFieldByName(disp_struct, "DispVec", 0, disp_vec_cell);
    }
    
    // Write DispMgn if available
    if (!combined.Disp.DispMgn.empty()) {
        size_t n_mgn = combined.Disp.DispMgn.size();
        std::vector<size_t> cell_dims = {1, n_mgn};
        matvar_t* disp_mgn_cell = Mat_VarCreate("DispMgn", MAT_C_CELL, MAT_T_CELL, 2, cell_dims.data(), nullptr, 0);
        
        for (size_t i = 0; i < n_mgn; ++i) {
            const auto& disp_mgn = combined.Disp.DispMgn[i];
            if (!disp_mgn.empty()) {
                std::vector<size_t> dims = {disp_mgn.size(), 1};
                matvar_t* dm_var = Mat_VarCreate("", MAT_C_DOUBLE, MAT_T_DOUBLE, 2, dims.data(), 
                                                (void*)disp_mgn.data(), 0);
                Mat_VarSetCell(disp_mgn_cell, i, dm_var);
            }
        }
        Mat_VarSetStructFieldByName(disp_struct, "DispMgn", 0, disp_mgn_cell);
    }
    
    Mat_VarWrite(matfp, disp_struct, MAT_COMPRESSION_NONE);
    Mat_VarFree(disp_struct);
    
    // Write FacePairInds
    if (!combined.FacePairInds.empty()) {
        std::vector<size_t> dims = {combined.FacePairInds.size(), 1};
        writeArrayVariable(matfp, "FacePairInds", combined.FacePairInds.data(), dims, 
                          MAT_T_INT32, MAT_C_INT32);
    }
    
    // Write PointPairInds
    if (!combined.PointPairInds.empty()) {
        std::vector<size_t> dims = {combined.PointPairInds.size(), 1};
        writeArrayVariable(matfp, "PointPairInds", combined.PointPairInds.data(), dims, 
                          MAT_T_INT32, MAT_C_INT32);
    }
    
    Mat_Close(matfp);
    std::cout << "Wrote DIC3Dcombined: " << filename << std::endl;
    return true;
}

bool MatWriter::write3DPPresults(const std::string& filename,
                                const DIC3DPPresults& ppresults) {
    // First write all DIC3Dcombined fields
    if (!write3DCombinedResults(filename, ppresults)) {
        return false;
    }
    
    // Reopen to add deformation fields
    mat_t* matfp = Mat_Open(filename.c_str(), MAT_ACC_RDWR);
    if (!matfp) {
        std::cerr << "Failed to reopen file for deformation data: " << filename << std::endl;
        return false;
    }
    
    // NOTE: This function currently writes placeholder deformation data
    // To write full deformation fields, use writeDeformationGroup() with FrameDeformationResult
    
    // Write Deformation structure (placeholder)
    std::vector<std::string> deform_fields = {"F", "strain", "princStrain", "maxShearStrain"};
    matvar_t* deform_struct = createStructVariable("Deform", deform_fields);
    Mat_VarWrite(matfp, deform_struct, MAT_COMPRESSION_NONE);
    Mat_VarFree(deform_struct);
    
    // Write FaceIsoInd
    if (!ppresults.FaceIsoInd.empty()) {
        std::vector<size_t> dims = {ppresults.FaceIsoInd.size(), 1};
        writeArrayVariable(matfp, "FaceIsoInd", ppresults.FaceIsoInd.data(), dims, 
                          MAT_T_DOUBLE, MAT_C_DOUBLE);
    }
    
    // Write deftype
    writeStringVariable(matfp, "deftype", ppresults.deftype);
    
    Mat_Close(matfp);
    std::cout << "Wrote DIC3DPPresults: " << filename << std::endl;
    return true;
}

bool MatWriter::writeDeformationGroup(mat_t* matfp,
                                      const std::string& group_name,
                                      const FrameDeformationResult& deform_data) {
    if (!matfp) {
        std::cerr << "Invalid MAT file pointer" << std::endl;
        return false;
    }
    
    size_t n_frames = deform_data.n_frames;
    size_t n_faces = deform_data.n_faces;
    
    std::cout << "Writing deformation group '" << group_name << "' with " 
              << n_frames << " frames and " << n_faces << " faces" << std::endl;
    
    // Prepare data in format: vector<vector<T>> where outer = frames, inner = faces
    // Scalars
    std::vector<std::vector<double>> Area_data(n_frames);
    std::vector<std::vector<double>> Lamda1_data(n_frames);
    std::vector<std::vector<double>> Lamda2_data(n_frames);
    std::vector<std::vector<double>> J_data(n_frames);
    std::vector<std::vector<double>> Emgn_data(n_frames);
    std::vector<std::vector<double>> emgn_data(n_frames);
    std::vector<std::vector<double>> Epc1_data(n_frames);
    std::vector<std::vector<double>> Epc2_data(n_frames);
    std::vector<std::vector<double>> epc1_data(n_frames);
    std::vector<std::vector<double>> epc2_data(n_frames);
    std::vector<std::vector<double>> EShearMax_data(n_frames);
    std::vector<std::vector<double>> eShearMax_data(n_frames);
    std::vector<std::vector<double>> Eeq_data(n_frames);
    std::vector<std::vector<double>> eeq_data(n_frames);
    std::vector<std::vector<double>> Dnorm_data(n_frames);
    
    // Vectors (3D)
    std::vector<std::vector<Eigen::Vector3d>> D1_data(n_frames);
    std::vector<std::vector<Eigen::Vector3d>> D2_data(n_frames);
    std::vector<std::vector<Eigen::Vector3d>> D3_data(n_frames);
    std::vector<std::vector<Eigen::Vector3d>> d1_data(n_frames);
    std::vector<std::vector<Eigen::Vector3d>> d2_data(n_frames);
    std::vector<std::vector<Eigen::Vector3d>> d3_data(n_frames);
    std::vector<std::vector<Eigen::Vector3d>> Drec1_data(n_frames);
    std::vector<std::vector<Eigen::Vector3d>> Drec2_data(n_frames);
    std::vector<std::vector<Eigen::Vector3d>> Epc1vec_data(n_frames);
    std::vector<std::vector<Eigen::Vector3d>> Epc2vec_data(n_frames);
    std::vector<std::vector<Eigen::Vector3d>> Epc1vecCur_data(n_frames);
    std::vector<std::vector<Eigen::Vector3d>> Epc2vecCur_data(n_frames);
    std::vector<std::vector<Eigen::Vector3d>> epc1vec_data(n_frames);
    std::vector<std::vector<Eigen::Vector3d>> epc2vec_data(n_frames);
    std::vector<std::vector<Eigen::Vector3d>> EShearMaxVec1_data(n_frames);
    std::vector<std::vector<Eigen::Vector3d>> EShearMaxVec2_data(n_frames);
    std::vector<std::vector<Eigen::Vector3d>> EShearMaxVecCur1_data(n_frames);
    std::vector<std::vector<Eigen::Vector3d>> EShearMaxVecCur2_data(n_frames);
    std::vector<std::vector<Eigen::Vector3d>> eShearMaxVec1_data(n_frames);
    std::vector<std::vector<Eigen::Vector3d>> eShearMaxVec2_data(n_frames);
    
    // Matrices (3x3)
    std::vector<std::vector<Eigen::Matrix3d>> Fmat_data(n_frames);
    std::vector<std::vector<Eigen::Matrix3d>> Cmat_data(n_frames);
    std::vector<std::vector<Eigen::Matrix3d>> Emat_data(n_frames);
    std::vector<std::vector<Eigen::Matrix3d>> emat_data(n_frames);
    
    // Copy data from frames
    for (size_t i = 0; i < n_frames && i < deform_data.frames.size(); ++i) {
        const auto& frame = deform_data.frames[i];
        
        Area_data[i] = frame.Area;
        Lamda1_data[i] = frame.Lamda1;
        Lamda2_data[i] = frame.Lamda2;
        J_data[i] = frame.J;
        Emgn_data[i] = frame.Emgn;
        emgn_data[i] = frame.emgn;
        Epc1_data[i] = frame.Epc1;
        Epc2_data[i] = frame.Epc2;
        epc1_data[i] = frame.epc1;
        epc2_data[i] = frame.epc2;
        EShearMax_data[i] = frame.EShearMax;
        eShearMax_data[i] = frame.eShearMax;
        Eeq_data[i] = frame.Eeq;
        eeq_data[i] = frame.eeq;
        Dnorm_data[i] = frame.Dnorm;
        
        D1_data[i] = frame.D1;
        D2_data[i] = frame.D2;
        D3_data[i] = frame.D3;
        d1_data[i] = frame.d1;
        d2_data[i] = frame.d2;
        d3_data[i] = frame.d3;
        Drec1_data[i] = frame.Drec1;
        Drec2_data[i] = frame.Drec2;
        Epc1vec_data[i] = frame.Epc1vec;
        Epc2vec_data[i] = frame.Epc2vec;
        Epc1vecCur_data[i] = frame.Epc1vecCur;
        Epc2vecCur_data[i] = frame.Epc2vecCur;
        epc1vec_data[i] = frame.epc1vec;
        epc2vec_data[i] = frame.epc2vec;
        EShearMaxVec1_data[i] = frame.EShearMaxVec1;
        EShearMaxVec2_data[i] = frame.EShearMaxVec2;
        EShearMaxVecCur1_data[i] = frame.EShearMaxVecCur1;
        EShearMaxVecCur2_data[i] = frame.EShearMaxVecCur2;
        eShearMaxVec1_data[i] = frame.eShearMaxVec1;
        eShearMaxVec2_data[i] = frame.eShearMaxVec2;
        
        Fmat_data[i] = frame.Fmat;
        Cmat_data[i] = frame.Cmat;
        Emat_data[i] = frame.Emat;
        emat_data[i] = frame.emat;
    }
    
    // Create and write cell arrays for all 36 fields
    // Scalars (15 fields)
    matvar_t* Area_cell = createCellArrayFromScalars("Area", Area_data, n_frames);
    matvar_t* Lamda1_cell = createCellArrayFromScalars("Lamda1", Lamda1_data, n_frames);
    matvar_t* Lamda2_cell = createCellArrayFromScalars("Lamda2", Lamda2_data, n_frames);
    matvar_t* J_cell = createCellArrayFromScalars("J", J_data, n_frames);
    matvar_t* Emgn_cell = createCellArrayFromScalars("Emgn", Emgn_data, n_frames);
    matvar_t* emgn_cell = createCellArrayFromScalars("emgn", emgn_data, n_frames);
    matvar_t* Epc1_cell = createCellArrayFromScalars("Epc1", Epc1_data, n_frames);
    matvar_t* Epc2_cell = createCellArrayFromScalars("Epc2", Epc2_data, n_frames);
    matvar_t* epc1_cell = createCellArrayFromScalars("epc1", epc1_data, n_frames);
    matvar_t* epc2_cell = createCellArrayFromScalars("epc2", epc2_data, n_frames);
    matvar_t* EShearMax_cell = createCellArrayFromScalars("EShearMax", EShearMax_data, n_frames);
    matvar_t* eShearMax_cell = createCellArrayFromScalars("eShearMax", eShearMax_data, n_frames);
    matvar_t* Eeq_cell = createCellArrayFromScalars("Eeq", Eeq_data, n_frames);
    matvar_t* eeq_cell = createCellArrayFromScalars("eeq", eeq_data, n_frames);
    matvar_t* Dnorm_cell = createCellArrayFromScalars("Dnorm", Dnorm_data, n_frames);
    
    // Vectors (20 fields)
    matvar_t* D1_cell = createCellArrayFromVectors("D1", D1_data, n_frames);
    matvar_t* D2_cell = createCellArrayFromVectors("D2", D2_data, n_frames);
    matvar_t* D3_cell = createCellArrayFromVectors("D3", D3_data, n_frames);
    matvar_t* d1_cell = createCellArrayFromVectors("d1", d1_data, n_frames);
    matvar_t* d2_cell = createCellArrayFromVectors("d2", d2_data, n_frames);
    matvar_t* d3_cell = createCellArrayFromVectors("d3", d3_data, n_frames);
    matvar_t* Drec1_cell = createCellArrayFromVectors("Drec1", Drec1_data, n_frames);
    matvar_t* Drec2_cell = createCellArrayFromVectors("Drec2", Drec2_data, n_frames);
    matvar_t* Epc1vec_cell = createCellArrayFromVectors("Epc1vec", Epc1vec_data, n_frames);
    matvar_t* Epc2vec_cell = createCellArrayFromVectors("Epc2vec", Epc2vec_data, n_frames);
    matvar_t* Epc1vecCur_cell = createCellArrayFromVectors("Epc1vecCur", Epc1vecCur_data, n_frames);
    matvar_t* Epc2vecCur_cell = createCellArrayFromVectors("Epc2vecCur", Epc2vecCur_data, n_frames);
    matvar_t* epc1vec_cell = createCellArrayFromVectors("epc1vec", epc1vec_data, n_frames);
    matvar_t* epc2vec_cell = createCellArrayFromVectors("epc2vec", epc2vec_data, n_frames);
    matvar_t* EShearMaxVec1_cell = createCellArrayFromVectors("EShearMaxVec1", EShearMaxVec1_data, n_frames);
    matvar_t* EShearMaxVec2_cell = createCellArrayFromVectors("EShearMaxVec2", EShearMaxVec2_data, n_frames);
    matvar_t* EShearMaxVecCur1_cell = createCellArrayFromVectors("EShearMaxVecCur1", EShearMaxVecCur1_data, n_frames);
    matvar_t* EShearMaxVecCur2_cell = createCellArrayFromVectors("EShearMaxVecCur2", EShearMaxVecCur2_data, n_frames);
    matvar_t* eShearMaxVec1_cell = createCellArrayFromVectors("eShearMaxVec1", eShearMaxVec1_data, n_frames);
    matvar_t* eShearMaxVec2_cell = createCellArrayFromVectors("eShearMaxVec2", eShearMaxVec2_data, n_frames);
    
    // Matrices (4 fields)
    matvar_t* Fmat_cell = createCellArrayFromMatrices("Fmat", Fmat_data, n_frames);
    matvar_t* Cmat_cell = createCellArrayFromMatrices("Cmat", Cmat_data, n_frames);
    matvar_t* Emat_cell = createCellArrayFromMatrices("Emat", Emat_data, n_frames);
    matvar_t* emat_cell = createCellArrayFromMatrices("emat", emat_data, n_frames);
    
    // Write all cell arrays to file
    Mat_VarWrite(matfp, Area_cell, MAT_COMPRESSION_NONE);
    Mat_VarWrite(matfp, Lamda1_cell, MAT_COMPRESSION_NONE);
    Mat_VarWrite(matfp, Lamda2_cell, MAT_COMPRESSION_NONE);
    Mat_VarWrite(matfp, J_cell, MAT_COMPRESSION_NONE);
    Mat_VarWrite(matfp, Emgn_cell, MAT_COMPRESSION_NONE);
    Mat_VarWrite(matfp, emgn_cell, MAT_COMPRESSION_NONE);
    Mat_VarWrite(matfp, Epc1_cell, MAT_COMPRESSION_NONE);
    Mat_VarWrite(matfp, Epc2_cell, MAT_COMPRESSION_NONE);
    Mat_VarWrite(matfp, epc1_cell, MAT_COMPRESSION_NONE);
    Mat_VarWrite(matfp, epc2_cell, MAT_COMPRESSION_NONE);
    Mat_VarWrite(matfp, EShearMax_cell, MAT_COMPRESSION_NONE);
    Mat_VarWrite(matfp, eShearMax_cell, MAT_COMPRESSION_NONE);
    Mat_VarWrite(matfp, Eeq_cell, MAT_COMPRESSION_NONE);
    Mat_VarWrite(matfp, eeq_cell, MAT_COMPRESSION_NONE);
    Mat_VarWrite(matfp, Dnorm_cell, MAT_COMPRESSION_NONE);
    
    Mat_VarWrite(matfp, D1_cell, MAT_COMPRESSION_NONE);
    Mat_VarWrite(matfp, D2_cell, MAT_COMPRESSION_NONE);
    Mat_VarWrite(matfp, D3_cell, MAT_COMPRESSION_NONE);
    Mat_VarWrite(matfp, d1_cell, MAT_COMPRESSION_NONE);
    Mat_VarWrite(matfp, d2_cell, MAT_COMPRESSION_NONE);
    Mat_VarWrite(matfp, d3_cell, MAT_COMPRESSION_NONE);
    Mat_VarWrite(matfp, Drec1_cell, MAT_COMPRESSION_NONE);
    Mat_VarWrite(matfp, Drec2_cell, MAT_COMPRESSION_NONE);
    Mat_VarWrite(matfp, Epc1vec_cell, MAT_COMPRESSION_NONE);
    Mat_VarWrite(matfp, Epc2vec_cell, MAT_COMPRESSION_NONE);
    Mat_VarWrite(matfp, Epc1vecCur_cell, MAT_COMPRESSION_NONE);
    Mat_VarWrite(matfp, Epc2vecCur_cell, MAT_COMPRESSION_NONE);
    Mat_VarWrite(matfp, epc1vec_cell, MAT_COMPRESSION_NONE);
    Mat_VarWrite(matfp, epc2vec_cell, MAT_COMPRESSION_NONE);
    Mat_VarWrite(matfp, EShearMaxVec1_cell, MAT_COMPRESSION_NONE);
    Mat_VarWrite(matfp, EShearMaxVec2_cell, MAT_COMPRESSION_NONE);
    Mat_VarWrite(matfp, EShearMaxVecCur1_cell, MAT_COMPRESSION_NONE);
    Mat_VarWrite(matfp, EShearMaxVecCur2_cell, MAT_COMPRESSION_NONE);
    Mat_VarWrite(matfp, eShearMaxVec1_cell, MAT_COMPRESSION_NONE);
    Mat_VarWrite(matfp, eShearMaxVec2_cell, MAT_COMPRESSION_NONE);
    
    Mat_VarWrite(matfp, Fmat_cell, MAT_COMPRESSION_NONE);
    Mat_VarWrite(matfp, Cmat_cell, MAT_COMPRESSION_NONE);
    Mat_VarWrite(matfp, Emat_cell, MAT_COMPRESSION_NONE);
    Mat_VarWrite(matfp, emat_cell, MAT_COMPRESSION_NONE);
    
    // Free all cell arrays
    Mat_VarFree(Area_cell);
    Mat_VarFree(Lamda1_cell);
    Mat_VarFree(Lamda2_cell);
    Mat_VarFree(J_cell);
    Mat_VarFree(Emgn_cell);
    Mat_VarFree(emgn_cell);
    Mat_VarFree(Epc1_cell);
    Mat_VarFree(Epc2_cell);
    Mat_VarFree(epc1_cell);
    Mat_VarFree(epc2_cell);
    Mat_VarFree(EShearMax_cell);
    Mat_VarFree(eShearMax_cell);
    Mat_VarFree(Eeq_cell);
    Mat_VarFree(eeq_cell);
    Mat_VarFree(Dnorm_cell);
    
    Mat_VarFree(D1_cell);
    Mat_VarFree(D2_cell);
    Mat_VarFree(D3_cell);
    Mat_VarFree(d1_cell);
    Mat_VarFree(d2_cell);
    Mat_VarFree(d3_cell);
    Mat_VarFree(Drec1_cell);
    Mat_VarFree(Drec2_cell);
    Mat_VarFree(Epc1vec_cell);
    Mat_VarFree(Epc2vec_cell);
    Mat_VarFree(Epc1vecCur_cell);
    Mat_VarFree(Epc2vecCur_cell);
    Mat_VarFree(epc1vec_cell);
    Mat_VarFree(epc2vec_cell);
    Mat_VarFree(EShearMaxVec1_cell);
    Mat_VarFree(EShearMaxVec2_cell);
    Mat_VarFree(EShearMaxVecCur1_cell);
    Mat_VarFree(EShearMaxVecCur2_cell);
    Mat_VarFree(eShearMaxVec1_cell);
    Mat_VarFree(eShearMaxVec2_cell);
    
    Mat_VarFree(Fmat_cell);
    Mat_VarFree(Cmat_cell);
    Mat_VarFree(Emat_cell);
    Mat_VarFree(emat_cell);
    
    std::cout << "Successfully wrote all 39 deformation fields to '" << group_name << "'" << std::endl;
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
    
    // Create cell array (1 x n_frames)
    std::vector<size_t> cell_dims = {1, n_frames};
    matvar_t* cell_array = Mat_VarCreate(name.c_str(), MAT_C_CELL, MAT_T_CELL, 
                                         2, cell_dims.data(), nullptr, 0);
    
    if (!cell_array) {
        std::cerr << "Failed to create cell array: " << name << std::endl;
        return nullptr;
    }
    
    // Fill each cell with scalar array
    for (size_t i = 0; i < n_frames && i < data.size(); ++i) {
        const auto& frame_data = data[i];
        size_t n_elements = frame_data.size();
        
        if (n_elements == 0) {
            // Empty cell
            Mat_VarSetCell(cell_array, i, nullptr);
            continue;
        }
        
        // Create double array for this frame
        std::vector<size_t> dims = {n_elements, 1};
        matvar_t* cell_data = Mat_VarCreate(nullptr, MAT_C_DOUBLE, MAT_T_DOUBLE,
                                           2, dims.data(), (void*)frame_data.data(), 0);
        
        Mat_VarSetCell(cell_array, i, cell_data);
    }
    
    return cell_array;
}

matvar_t* MatWriter::createCellArrayFromVectors(
    const std::string& name,
    const std::vector<std::vector<Eigen::Vector3d>>& data,
    size_t n_frames) {
    
    // Create cell array (1 x n_frames)
    std::vector<size_t> cell_dims = {1, n_frames};
    matvar_t* cell_array = Mat_VarCreate(name.c_str(), MAT_C_CELL, MAT_T_CELL,
                                         2, cell_dims.data(), nullptr, 0);
    
    if (!cell_array) {
        std::cerr << "Failed to create cell array: " << name << std::endl;
        return nullptr;
    }
    
    // Fill each cell with Nx3 array
    for (size_t i = 0; i < n_frames && i < data.size(); ++i) {
        const auto& frame_data = data[i];
        size_t n_vectors = frame_data.size();
        
        if (n_vectors == 0) {
            Mat_VarSetCell(cell_array, i, nullptr);
            continue;
        }
        
        // Convert vector<Vector3d> to flat double array (Nx3)
        std::vector<double> flat_data(n_vectors * 3);
        for (size_t j = 0; j < n_vectors; ++j) {
            flat_data[j * 3 + 0] = frame_data[j](0);
            flat_data[j * 3 + 1] = frame_data[j](1);
            flat_data[j * 3 + 2] = frame_data[j](2);
        }
        
        // Create Nx3 array
        std::vector<size_t> dims = {n_vectors, 3};
        matvar_t* cell_data = Mat_VarCreate(nullptr, MAT_C_DOUBLE, MAT_T_DOUBLE,
                                           2, dims.data(), flat_data.data(), 0);
        
        Mat_VarSetCell(cell_array, i, cell_data);
    }
    
    return cell_array;
}

matvar_t* MatWriter::createCellArrayFromMatrices(
    const std::string& name,
    const std::vector<std::vector<Eigen::Matrix3d>>& data,
    size_t n_frames) {
    
    // Create cell array (1 x n_frames)
    std::vector<size_t> cell_dims = {1, n_frames};
    matvar_t* cell_array = Mat_VarCreate(name.c_str(), MAT_C_CELL, MAT_T_CELL,
                                         2, cell_dims.data(), nullptr, 0);
    
    if (!cell_array) {
        std::cerr << "Failed to create cell array: " << name << std::endl;
        return nullptr;
    }
    
    // Fill each cell with Nx9 array (3x3 matrices flattened)
    for (size_t i = 0; i < n_frames && i < data.size(); ++i) {
        const auto& frame_data = data[i];
        size_t n_matrices = frame_data.size();
        
        if (n_matrices == 0) {
            Mat_VarSetCell(cell_array, i, nullptr);
            continue;
        }
        
        // Convert vector<Matrix3d> to flat double array (Nx9)
        std::vector<double> flat_data(n_matrices * 9);
        for (size_t j = 0; j < n_matrices; ++j) {
            // Flatten in column-major order (MATLAB convention)
            for (int col = 0; col < 3; ++col) {
                for (int row = 0; row < 3; ++row) {
                    flat_data[j * 9 + col * 3 + row] = frame_data[j](row, col);
                }
            }
        }
        
        // Create Nx9 array
        std::vector<size_t> dims = {n_matrices, 9};
        matvar_t* cell_data = Mat_VarCreate(nullptr, MAT_C_DOUBLE, MAT_T_DOUBLE,
                                           2, dims.data(), flat_data.data(), 0);
        
        Mat_VarSetCell(cell_array, i, cell_data);
    }
    
    return cell_array;
}

} // namespace cppxdic
