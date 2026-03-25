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

bool MatWriter::writeMultiFrameNcorrFile(const std::string& filename,
                                        const cv::Mat& ref_img,
                                        const std::vector<cv::Mat>& cur_imgs,
                                        const cv::Mat& ref_roi,
                                        const std::vector<cv::Mat>& cur_rois,
                                        const std::vector<ncorr::DIC_analysis_output>& dic_outputs,
                                        const std::map<std::string, double>& dispinfo,
                                        const std::string& type_str,
                                        const std::string& ref_name) {
    
    if (cur_imgs.empty()) {
        std::cerr << "Error: No current images provided" << std::endl;
        return false;
    }
    
    size_t n_frames = cur_imgs.size();
    std::cout << "Writing multi-frame ncorr file with " << n_frames << " frames..." << std::endl;
    
    // Create MAT file (v7.3 HDF5 format)
    mat_t* matfp = createMatFileHDF5(filename);
    if (!matfp) {
        std::cerr << "Failed to create MAT file: " << filename << std::endl;
        return false;
    }
    
    // ===== CREATE REFERENCE_SAVE STRUCT (single image) =====
    std::vector<std::string> ref_fields = {"gs", "name", "path", "roi", "type"};
    matvar_t* reference_save = createStructVariable("reference_save", ref_fields);
    
    // Add ref image (single 2D array)
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
    
    // ===== CREATE CURRENT_SAVE STRUCT (cell arrays for multi-frame) =====
    matvar_t* current_save = createStructVariable("current_save", ref_fields);
    
    // Create gs cell array (n_frames x 1)
    std::vector<size_t> cell_dims = {n_frames, 1};
    matvar_t* gs_cell = Mat_VarCreate("gs", MAT_C_CELL, MAT_T_CELL, 2, cell_dims.data(), nullptr, 0);
    
    for (size_t i = 0; i < n_frames; ++i) {
        // Write each image temporarily and read it back
        std::string temp_name = "cur_gs_temp_" + std::to_string(i);
        writeMatVariable(matfp, temp_name, cur_imgs[i]);
        matvar_t* img_var = Mat_VarRead(matfp, temp_name.c_str());
        Mat_VarSetCell(gs_cell, i, img_var);
    }
    
    addFieldToStruct(current_save, "gs", gs_cell, 0);
    
    // Create roi cell array (n_frames x 1)
    matvar_t* roi_cell = Mat_VarCreate("roi", MAT_C_CELL, MAT_T_CELL, 2, cell_dims.data(), nullptr, 0);
    
    for (size_t i = 0; i < n_frames; ++i) {
        // Use provided ROI or clone from reference if not enough provided
        cv::Mat cur_roi_to_use = (i < cur_rois.size()) ? cur_rois[i] : ref_roi.clone();
        
        // Update ROI with displacement if available
        if (i < dic_outputs.size() && !dic_outputs[i].disps.empty()) {
            try {
                ncorr::ROI2D roi_current = convertMatToROI2D(cur_roi_to_use);
                ncorr::ROI2D roi_updated = ncorr::update(
                    roi_current, 
                    dic_outputs[i].disps[0],
                    ncorr::INTERP::CUBIC_KEYS
                );
                cur_roi_to_use = convertROI2DToMat(roi_updated);
            } catch (const std::exception& e) {
                std::cerr << "  Warning: ROI update failed for frame " << i << ": " << e.what() << std::endl;
            }
        }
        
        // Create ROI struct for this frame
        matvar_t* frame_roi_struct = createStructVariable("roi", roi_fields);
        std::string temp_roi_name = "cur_roi_temp_" + std::to_string(i);
        writeMatVariable(matfp, temp_roi_name, cur_roi_to_use);
        matvar_t* roi_mask_var = Mat_VarRead(matfp, temp_roi_name.c_str());
        addFieldToStruct(frame_roi_struct, "mask", roi_mask_var, 0);
        
        Mat_VarSetCell(roi_cell, i, frame_roi_struct);
    }
    
    addFieldToStruct(current_save, "roi", roi_cell, 0);
    
    // Create name cell array (current_1, current_2, ...)
    matvar_t* name_cell = Mat_VarCreate("name", MAT_C_CELL, MAT_T_CELL, 2, cell_dims.data(), nullptr, 0);
    for (size_t i = 0; i < n_frames; ++i) {
        std::string name = "current_" + std::to_string(i + 1);  // 1-indexed
        std::vector<size_t> str_dims = {1, name.length()};
        matvar_t* name_var = Mat_VarCreate(nullptr, MAT_C_CHAR, MAT_T_UINT8, 2, str_dims.data(), 
                                          (void*)name.c_str(), 0);
        Mat_VarSetCell(name_cell, i, name_var);
    }
    addFieldToStruct(current_save, "name", name_cell, 0);
    
    // Create path cell array (empty for all frames)
    matvar_t* path_cell = Mat_VarCreate("path", MAT_C_CELL, MAT_T_CELL, 2, cell_dims.data(), nullptr, 0);
    for (size_t i = 0; i < n_frames; ++i) {
        Mat_VarSetCell(path_cell, i, nullptr);  // Empty path
    }
    addFieldToStruct(current_save, "path", path_cell, 0);
    
    // Create type cell array (use type_str parameter for all frames)
    matvar_t* type_cell = Mat_VarCreate("type", MAT_C_CELL, MAT_T_CELL, 2, cell_dims.data(), nullptr, 0);
    for (size_t i = 0; i < n_frames; ++i) {
        std::vector<size_t> str_dims = {1, type_str.length()};
        matvar_t* type_var = Mat_VarCreate(nullptr, MAT_C_CHAR, MAT_T_UINT8, 2, str_dims.data(), 
                                          (void*)type_str.c_str(), 0);
        Mat_VarSetCell(type_cell, i, type_var);
    }
    addFieldToStruct(current_save, "type", type_cell, 0);
    
    // Add name and type to reference_save (use ref_name parameter)
    std::vector<size_t> ref_name_dims = {1, ref_name.length()};
    matvar_t* ref_name_var = Mat_VarCreate("name", MAT_C_CHAR, MAT_T_UINT8, 2, ref_name_dims.data(), 
                                          (void*)ref_name.c_str(), 0);
    addFieldToStruct(reference_save, "name", ref_name_var, 0);
    
    std::vector<size_t> ref_type_dims = {1, type_str.length()};
    matvar_t* ref_type_var = Mat_VarCreate("type", MAT_C_CHAR, MAT_T_UINT8, 2, ref_type_dims.data(), 
                                          (void*)type_str.c_str(), 0);
    addFieldToStruct(reference_save, "type", ref_type_var, 0);
    
    // Add empty path to reference_save
    addFieldToStruct(reference_save, "path", nullptr, 0);
    
    // ===== CREATE DATA_DIC_SAVE STRUCT =====
    // Format dispinfo
    matvar_t* dispinfo_var = formatDispInfo(dispinfo);
    
    // Format displacements (multi-frame)
    matvar_t* displacements_var = nullptr;
    if (!dic_outputs.empty() && dic_outputs.size() == n_frames) {
        // Use the first DIC output to format displacements
        // In multi-frame tracking, all frames typically use the same reference
        displacements_var = formatDisplacements(dic_outputs[0]);
    }
    
    std::vector<std::string> data_fields = {"dispinfo", "displacements", "straininfo", "strains"};
    matvar_t* data_dic_save = createStructVariable("data_dic_save", data_fields);
    addFieldToStruct(data_dic_save, "dispinfo", dispinfo_var, 0);
    
    if (displacements_var) {
        addFieldToStruct(data_dic_save, "displacements", displacements_var, 0);
    }
    
    // Add empty straininfo and strains (placeholders)
    std::vector<std::string> straininfo_fields = {"radius", "subsettrunc"};
    matvar_t* straininfo_var = createStructVariable("straininfo", straininfo_fields);
    addFieldToStruct(data_dic_save, "straininfo", straininfo_var, 0);
    
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
    
    std::cout << "Successfully wrote multi-frame ncorr file: " << filename << std::endl;
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
    mat_t* matfp = createMatFileHDF5(filename);
    if (!matfp) {
        std::cerr << "Failed to create DIC2DPairResults file: " << filename << std::endl;
        return false;
    }
    
    // Write scalar fields
    writeScalarVariable(matfp, "nCamRef", results.nCamRef);
    writeScalarVariable(matfp, "nCamDef", results.nCamDef);
    writeScalarVariable(matfp, "nImages", results.nImages);
    writeScalarVariable(matfp, "pairForced", results.pairForced ? 1.0 : 0.0);
    if (!results.pairOrder.empty()) {
        std::vector<double> pair_order(results.pairOrder.begin(), results.pairOrder.end());
        std::vector<size_t> dims = {1, pair_order.size()};
        writeArrayVariable(matfp, "pairOrder", pair_order.data(), dims, MAT_T_DOUBLE, MAT_C_DOUBLE);
    }
    
    // Write ROI mask
    if (!results.ROImask.empty()) {
        writeMatVariable(matfp, "ROImask", results.ROImask);
    }
    
    // Write ncorrInfo structure
    std::vector<std::string> ncorr_fields = {
        "cutoff_corrcoef", "cutoff_diffnorm", "cutoff_iteration",
        "imgcorr", "lenscoef", "pixtounits", "radius", "spacing",
        "stepanalysis", "subsettrunc", "total_threads", "type", "units"
    };
    matvar_t* ncorr_struct = createStructVariable("ncorrInfo", ncorr_fields);
    
    // Add cutoff_corrcoef array (MATLAB: 1×N row vector)
    if (!results.ncorrInfo.cutoff_corrcoef.empty()) {
        std::vector<size_t> dims = {1, results.ncorrInfo.cutoff_corrcoef.size()};
        matvar_t* cutoff_var = Mat_VarCreate("cutoff_corrcoef", MAT_C_DOUBLE, MAT_T_DOUBLE,
                                            2, dims.data(), (void*)results.ncorrInfo.cutoff_corrcoef.data(), 0);
        Mat_VarSetStructFieldByName(ncorr_struct, "cutoff_corrcoef", 0, cutoff_var);
    }
    
    // Add scalar fields
    matvar_t* diffnorm_var = Mat_VarCreate("cutoff_diffnorm", MAT_C_DOUBLE, MAT_T_DOUBLE,
                                          2, (size_t[]){1,1}, &results.ncorrInfo.cutoff_diffnorm, 0);
    Mat_VarSetStructFieldByName(ncorr_struct, "cutoff_diffnorm", 0, diffnorm_var);
    
    matvar_t* iteration_var = Mat_VarCreate("cutoff_iteration", MAT_C_INT32, MAT_T_INT32,
                                           2, (size_t[]){1,1}, &results.ncorrInfo.cutoff_iteration, 0);
    Mat_VarSetStructFieldByName(ncorr_struct, "cutoff_iteration", 0, iteration_var);
    
    matvar_t* lenscoef_var = Mat_VarCreate("lenscoef", MAT_C_INT32, MAT_T_INT32,
                                          2, (size_t[]){1,1}, &results.ncorrInfo.lenscoef, 0);
    Mat_VarSetStructFieldByName(ncorr_struct, "lenscoef", 0, lenscoef_var);
    
    matvar_t* pixtounits_var = Mat_VarCreate("pixtounits", MAT_C_DOUBLE, MAT_T_DOUBLE,
                                            2, (size_t[]){1,1}, &results.ncorrInfo.pixtounits, 0);
    Mat_VarSetStructFieldByName(ncorr_struct, "pixtounits", 0, pixtounits_var);
    
    matvar_t* radius_var = Mat_VarCreate("radius", MAT_C_INT32, MAT_T_INT32,
                                        2, (size_t[]){1,1}, &results.ncorrInfo.radius, 0);
    Mat_VarSetStructFieldByName(ncorr_struct, "radius", 0, radius_var);
    
    matvar_t* spacing_var = Mat_VarCreate("spacing", MAT_C_INT32, MAT_T_INT32,
                                         2, (size_t[]){1,1}, &results.ncorrInfo.spacing, 0);
    Mat_VarSetStructFieldByName(ncorr_struct, "spacing", 0, spacing_var);
    
    // Add stepanalysis struct
    std::vector<std::string> stepanalysis_fields = {"enabled", "type", "auto", "step"};
    matvar_t* stepanalysis_struct = createStructVariable("stepanalysis", stepanalysis_fields);
    
    int enabled_int = results.ncorrInfo.stepanalysis.enabled ? 1 : 0;
    matvar_t* enabled_var = Mat_VarCreate("enabled", MAT_C_INT32, MAT_T_INT32,
                                         2, (size_t[]){1,1}, &enabled_int, 0);
    Mat_VarSetStructFieldByName(stepanalysis_struct, "enabled", 0, enabled_var);
    
    if (!results.ncorrInfo.stepanalysis.type.empty()) {
        std::vector<size_t> dims = {1, results.ncorrInfo.stepanalysis.type.length()};
        matvar_t* type_var = Mat_VarCreate("type", MAT_C_CHAR, MAT_T_UINT8,
                                          2, dims.data(), (void*)results.ncorrInfo.stepanalysis.type.c_str(), 0);
        Mat_VarSetStructFieldByName(stepanalysis_struct, "type", 0, type_var);
    }
    
    int auto_int = results.ncorrInfo.stepanalysis.auto_update ? 1 : 0;
    matvar_t* auto_var = Mat_VarCreate("auto", MAT_C_INT32, MAT_T_INT32,
                                      2, (size_t[]){1,1}, &auto_int, 0);
    Mat_VarSetStructFieldByName(stepanalysis_struct, "auto", 0, auto_var);
    
    matvar_t* step_var = Mat_VarCreate("step", MAT_C_INT32, MAT_T_INT32,
                                      2, (size_t[]){1,1}, &results.ncorrInfo.stepanalysis.step, 0);
    Mat_VarSetStructFieldByName(stepanalysis_struct, "step", 0, step_var);
    
    Mat_VarSetStructFieldByName(ncorr_struct, "stepanalysis", 0, stepanalysis_struct);
    
    int subsettrunc_int = results.ncorrInfo.subsettrunc ? 1 : 0;
    matvar_t* subsettrunc_var = Mat_VarCreate("subsettrunc", MAT_C_INT32, MAT_T_INT32,
                                             2, (size_t[]){1,1}, &subsettrunc_int, 0);
    Mat_VarSetStructFieldByName(ncorr_struct, "subsettrunc", 0, subsettrunc_var);
    
    matvar_t* threads_var = Mat_VarCreate("total_threads", MAT_C_INT32, MAT_T_INT32,
                                         2, (size_t[]){1,1}, &results.ncorrInfo.total_threads, 0);
    Mat_VarSetStructFieldByName(ncorr_struct, "total_threads", 0, threads_var);
    
    // Add string fields
    if (!results.ncorrInfo.type.empty()) {
        std::vector<size_t> dims = {1, results.ncorrInfo.type.length()};
        matvar_t* type_var = Mat_VarCreate("type", MAT_C_CHAR, MAT_T_UINT8,
                                          2, dims.data(), (void*)results.ncorrInfo.type.c_str(), 0);
        Mat_VarSetStructFieldByName(ncorr_struct, "type", 0, type_var);
    }
    
    if (!results.ncorrInfo.units.empty()) {
        std::vector<size_t> dims = {1, results.ncorrInfo.units.length()};
        matvar_t* units_var = Mat_VarCreate("units", MAT_C_CHAR, MAT_T_UINT8,
                                           2, dims.data(), (void*)results.ncorrInfo.units.c_str(), 0);
        Mat_VarSetStructFieldByName(ncorr_struct, "units", 0, units_var);
    }
    
    // Add imgcorr cell array
    if (!results.ncorrInfo.imgcorr.empty()) {
        std::vector<size_t> cell_dims = {results.ncorrInfo.imgcorr.size(), 1};
        matvar_t* imgcorr_cell = Mat_VarCreate("imgcorr", MAT_C_CELL, MAT_T_CELL,
                                              2, cell_dims.data(), nullptr, 0);
        for (size_t i = 0; i < results.ncorrInfo.imgcorr.size(); ++i) {
            const std::string& img = results.ncorrInfo.imgcorr[i];
            if (!img.empty()) {
                std::vector<size_t> dims = {1, img.length()};
                matvar_t* str_var = Mat_VarCreate(nullptr, MAT_C_CHAR, MAT_T_UINT8,
                                                 2, dims.data(), (void*)img.c_str(), 0);
                Mat_VarSetCell(imgcorr_cell, i, str_var);
            }
        }
        Mat_VarSetStructFieldByName(ncorr_struct, "imgcorr", 0, imgcorr_cell);
    }
    
    // Write ncorrInfo struct
    Mat_VarWrite(matfp, ncorr_struct, MAT_COMPRESSION_NONE);
    Mat_VarFree(ncorr_struct);
    
    // Write Points as cell array (MATLAB: 1×nFrames cell, each cell is 2×nPts)
    size_t n_frames = results.Points.size();
    if (n_frames > 0) {
        std::vector<size_t> cell_dims = {1, n_frames};
        matvar_t* points_cell = Mat_VarCreate("Points", MAT_C_CELL, MAT_T_CELL, 2, cell_dims.data(), nullptr, 0);
        
        for (size_t i = 0; i < n_frames; ++i) {
            const auto& pts = results.Points[i];
            size_t n_points = pts.x.size();
            
            if (n_points == 0) {
                Mat_VarSetCell(points_cell, i, nullptr);
                continue;
            }
            
            // Create 2×N array (MATLAB column-major: interleaved xy)
            std::vector<double> xy_data(n_points * 2);
            for (size_t j = 0; j < n_points; ++j) {
                xy_data[j * 2 + 0] = pts.x[j];
                xy_data[j * 2 + 1] = pts.y[j];
            }
            
            std::vector<size_t> dims = {2, n_points};
            matvar_t* xy_var = Mat_VarCreate(nullptr, MAT_C_DOUBLE, MAT_T_DOUBLE, 2, dims.data(), 
                                            xy_data.data(), 0);
            
            Mat_VarSetCell(points_cell, i, xy_var);
        }
        
        Mat_VarWrite(matfp, points_cell, MAT_COMPRESSION_NONE);
        Mat_VarFree(points_cell);
    }
    
    // Write CorCoeffVec as cell array (300x1, each cell is Nx1 array)
    if (!results.CorCoeffVec.empty()) {
        matvar_t* corcoeff_cell = createCellArrayFromScalars("CorCoeffVec", results.CorCoeffVec, results.CorCoeffVec.size());
        if (corcoeff_cell) {
            Mat_VarWrite(matfp, corcoeff_cell, MAT_COMPRESSION_NONE);
            Mat_VarFree(corcoeff_cell);
        }
    }
    
    // Write Faces (MATLAB: 3×nFaces double, 1-indexed)
    if (!results.Faces.empty()) {
        size_t n_faces = results.Faces.size() / 3;
        std::vector<size_t> dims = {3, n_faces};
        std::vector<double> faces_dbl(results.Faces.size());
        for (size_t i = 0; i < results.Faces.size(); ++i) {
            faces_dbl[i] = static_cast<double>(results.Faces[i] + 1);
        }
        writeArrayVariable(matfp, "Faces", faces_dbl.data(), dims, 
                          MAT_T_DOUBLE, MAT_C_DOUBLE);
    }
    
    // Write FaceColors (MATLAB: 1×nFaces)
    if (!results.FaceColors.empty()) {
        std::vector<size_t> dims = {1, results.FaceColors.size()};
        writeArrayVariable(matfp, "FaceColors", results.FaceColors.data(), dims, 
                          MAT_T_DOUBLE, MAT_C_DOUBLE);
    }
    
    Mat_Close(matfp);
    std::cout << "Wrote DIC2DPairResults: " << filename << std::endl;
    return true;
}

matvar_t* MatWriter::buildCombinedStructFields(mat_t* matfp,
                                                 const DIC3Dcombined& combined,
                                                 const std::string& struct_name,
                                                 const std::vector<std::string>& extra_fields) {
    // Base fields for DIC3Dcombined
    std::vector<std::string> combined_fields = {
        "pairIndices", "Points3D", "Faces", "FaceColors", "corrComb",
        "FaceCorrComb", "FaceCentroids", "Disp", "FacePairInds",
        "PointPairInds", "calibration", "distortion", "AllPairsResults", "DIC2Dinfo"
    };
    // Append any extra fields (e.g. Deform, FaceIsoInd for PPresults)
    for (const auto& f : extra_fields) {
        combined_fields.push_back(f);
    }
    
    matvar_t* combined_struct = createStructVariable(struct_name, combined_fields);
    if (!combined_struct) {
        std::cerr << "Failed to create " << struct_name << " struct" << std::endl;
        return nullptr;
    }
    
    // pairIndices (MATLAB: nPairs x 2 double matrix)
    if (!combined.pairIndices.empty()) {
        size_t nCams = combined.pairIndices.size();
        size_t nPairs = nCams / 2;
        if (nPairs == 0) nPairs = 1;
        size_t nCols = (nPairs > 0) ? nCams / nPairs : nCams;
        std::vector<double> pair_dbl(nCams);
        for (size_t i = 0; i < nCams; ++i) pair_dbl[i] = static_cast<double>(combined.pairIndices[i]);
        std::vector<size_t> dims = {nPairs, nCols};
        matvar_t* pair_ind_var = Mat_VarCreate("pairIndices", MAT_C_DOUBLE, MAT_T_DOUBLE,
                                               2, dims.data(), pair_dbl.data(), 0);
        Mat_VarSetStructFieldByName(combined_struct, "pairIndices", 0, pair_ind_var);
    }
    
    // Points3D cell array (MATLAB: nFrames×1 cell, each cell is 3×nPts double)
    size_t n_frames = combined.Points3D.size();
    {
        std::vector<size_t> cell_dims = {n_frames, 1};
        matvar_t* points3d_cell = Mat_VarCreate("Points3D", MAT_C_CELL, MAT_T_CELL, 2, cell_dims.data(), nullptr, 0);
        for (size_t i = 0; i < n_frames; ++i) {
            const auto& pts = combined.Points3D[i];
            size_t n_points = pts.x.size();
            std::vector<size_t> dims = {3, n_points};
            // Interleaved layout for MATLAB [3,N] column-major
            std::vector<double> pts_data(n_points * 3);
            for (size_t p = 0; p < n_points; ++p) {
                pts_data[p * 3 + 0] = pts.x[p];
                pts_data[p * 3 + 1] = pts.y[p];
                pts_data[p * 3 + 2] = pts.z[p];
            }
            matvar_t* pt_var = Mat_VarCreate(nullptr, MAT_C_DOUBLE, MAT_T_DOUBLE,
                                             2, dims.data(), pts_data.data(), 0);
            Mat_VarSetCell(points3d_cell, i, pt_var);
        }
        Mat_VarSetStructFieldByName(combined_struct, "Points3D", 0, points3d_cell);
    }
    
    // Faces (MATLAB: 3×nFaces double, 1-indexed)
    if (!combined.Faces.empty()) {
        size_t n_faces = combined.Faces.size() / 3;
        std::vector<size_t> dims = {3, n_faces};
        std::vector<double> faces_dbl(combined.Faces.size());
        for (size_t i = 0; i < combined.Faces.size(); ++i)
            faces_dbl[i] = static_cast<double>(combined.Faces[i] + 1);
        Mat_VarSetStructFieldByName(combined_struct, "Faces", 0,
            Mat_VarCreate("Faces", MAT_C_DOUBLE, MAT_T_DOUBLE, 2, dims.data(), faces_dbl.data(), 0));
    }
    
    // FaceColors (MATLAB: 1×nFaces double)
    if (!combined.FaceColors.empty()) {
        std::vector<size_t> dims = {1, combined.FaceColors.size()};
        Mat_VarSetStructFieldByName(combined_struct, "FaceColors", 0,
            Mat_VarCreate("FaceColors", MAT_C_DOUBLE, MAT_T_DOUBLE, 2, dims.data(), (void*)combined.FaceColors.data(), 0));
    }
    
    // corrComb cell array (MATLAB: nFrames×1 cell, each cell is 1×nPts)
    if (!combined.corrComb.empty()) {
        size_t n = combined.corrComb.size();
        std::vector<size_t> cd = {n, 1};
        matvar_t* corr_cell = Mat_VarCreate("corrComb", MAT_C_CELL, MAT_T_CELL, 2, cd.data(), nullptr, 0);
        for (size_t i = 0; i < n; ++i) {
            const auto& cf = combined.corrComb[i];
            if (!cf.empty()) {
                std::vector<size_t> d = {1, cf.size()};
                Mat_VarSetCell(corr_cell, i, Mat_VarCreate("", MAT_C_DOUBLE, MAT_T_DOUBLE, 2, d.data(), (void*)cf.data(), 0));
            }
        }
        Mat_VarSetStructFieldByName(combined_struct, "corrComb", 0, corr_cell);
    }
    
    // FaceCorrComb cell array (MATLAB: nFrames×1 cell, each cell is 1×nFaces)
    if (!combined.FaceCorrComb.empty()) {
        size_t n = combined.FaceCorrComb.size();
        std::vector<size_t> cd = {n, 1};
        matvar_t* fcc = Mat_VarCreate("FaceCorrComb", MAT_C_CELL, MAT_T_CELL, 2, cd.data(), nullptr, 0);
        for (size_t i = 0; i < n; ++i) {
            const auto& fc = combined.FaceCorrComb[i];
            if (!fc.empty()) {
                std::vector<size_t> d = {1, fc.size()};
                Mat_VarSetCell(fcc, i, Mat_VarCreate("", MAT_C_DOUBLE, MAT_T_DOUBLE, 2, d.data(), (void*)fc.data(), 0));
            }
        }
        Mat_VarSetStructFieldByName(combined_struct, "FaceCorrComb", 0, fcc);
    }
    
    // FaceCentroids cell array (MATLAB: nFrames×1 cell, each cell is 3×nFaces)
    if (!combined.FaceCentroids.empty()) {
        size_t n = combined.FaceCentroids.size();
        std::vector<size_t> cd = {n, 1};
        matvar_t* cc = Mat_VarCreate("FaceCentroids", MAT_C_CELL, MAT_T_CELL, 2, cd.data(), nullptr, 0);
        for (size_t i = 0; i < n; ++i) {
            const auto& c = combined.FaceCentroids[i];
            if (!c.empty()) {
                size_t n_faces = c.size() / 3;
                std::vector<size_t> d = {3, n_faces};
                Mat_VarSetCell(cc, i, Mat_VarCreate("", MAT_C_DOUBLE, MAT_T_DOUBLE, 2, d.data(), (void*)c.data(), 0));
            }
        }
        Mat_VarSetStructFieldByName(combined_struct, "FaceCentroids", 0, cc);
    }
    
    // Disp sub-struct (MATLAB: nFrames×1 cells)
    {
        std::vector<std::string> disp_fields = {"DispVec", "DispMgn"};
        matvar_t* disp_struct = createStructVariable("Disp", disp_fields);
        if (!combined.Disp.DispVec.empty()) {
            size_t n = combined.Disp.DispVec.size();
            std::vector<size_t> cd = {n, 1};
            matvar_t* dvc = Mat_VarCreate("DispVec", MAT_C_CELL, MAT_T_CELL, 2, cd.data(), nullptr, 0);
            for (size_t i = 0; i < n; ++i) {
                const auto& dv = combined.Disp.DispVec[i];
                if (!dv.empty()) {
                    size_t nPts = dv.size() / 3;
                    std::vector<size_t> d = {3, nPts};
                    Mat_VarSetCell(dvc, i, Mat_VarCreate("", MAT_C_DOUBLE, MAT_T_DOUBLE, 2, d.data(), (void*)dv.data(), 0));
                }
            }
            Mat_VarSetStructFieldByName(disp_struct, "DispVec", 0, dvc);
        }
        if (!combined.Disp.DispMgn.empty()) {
            size_t n = combined.Disp.DispMgn.size();
            std::vector<size_t> cd = {n, 1};
            matvar_t* dmc = Mat_VarCreate("DispMgn", MAT_C_CELL, MAT_T_CELL, 2, cd.data(), nullptr, 0);
            for (size_t i = 0; i < n; ++i) {
                const auto& dm = combined.Disp.DispMgn[i];
                if (!dm.empty()) {
                    std::vector<size_t> d = {1, dm.size()};
                    Mat_VarSetCell(dmc, i, Mat_VarCreate("", MAT_C_DOUBLE, MAT_T_DOUBLE, 2, d.data(), (void*)dm.data(), 0));
                }
            }
            Mat_VarSetStructFieldByName(disp_struct, "DispMgn", 0, dmc);
        }
        Mat_VarSetStructFieldByName(combined_struct, "Disp", 0, disp_struct);
    }
    
    // FacePairInds (MATLAB: 1×nFaces double)
    if (!combined.FacePairInds.empty()) {
        std::vector<size_t> dims = {1, combined.FacePairInds.size()};
        std::vector<double> fpi_dbl(combined.FacePairInds.size());
        for (size_t i = 0; i < combined.FacePairInds.size(); ++i)
            fpi_dbl[i] = static_cast<double>(combined.FacePairInds[i]);
        Mat_VarSetStructFieldByName(combined_struct, "FacePairInds", 0,
            Mat_VarCreate("FacePairInds", MAT_C_DOUBLE, MAT_T_DOUBLE, 2, dims.data(), fpi_dbl.data(), 0));
    }
    
    // PointPairInds (MATLAB: 1×nPts double)
    if (!combined.PointPairInds.empty()) {
        std::vector<size_t> dims = {1, combined.PointPairInds.size()};
        std::vector<double> ppi_dbl(combined.PointPairInds.size());
        for (size_t i = 0; i < combined.PointPairInds.size(); ++i)
            ppi_dbl[i] = static_cast<double>(combined.PointPairInds[i]);
        Mat_VarSetStructFieldByName(combined_struct, "PointPairInds", 0,
            Mat_VarCreate("PointPairInds", MAT_C_DOUBLE, MAT_T_DOUBLE, 2, dims.data(), ppi_dbl.data(), 0));
    }
    
    // calibration sub-struct
    if (!combined.calibration.DLT_paths.empty()) {
        std::vector<std::string> calib_fields = {"DLTpath", "DLTparameters"};
        matvar_t* calib_struct = createStructVariable("calibration", calib_fields);
        size_t rows = combined.calibration.DLT_paths.size();
        size_t cols = (rows > 0) ? combined.calibration.DLT_paths[0].size() : 0;
        Mat_VarSetStructFieldByName(calib_struct, "DLTpath", 0,
            createCellArray2DFromStrings("DLTpath", combined.calibration.DLT_paths, rows, cols));
        Mat_VarSetStructFieldByName(calib_struct, "DLTparameters", 0,
            createCellArray2DFromVectors("DLTparameters", combined.calibration.DLT_params, rows, cols));
        Mat_VarSetStructFieldByName(combined_struct, "calibration", 0, calib_struct);
    }
    
    // distortion sub-struct
    if (!combined.distortion.distortion_models.empty()) {
        std::vector<std::string> dist_fields = {"distortionModel", "distortionPath"};
        matvar_t* dist_struct = createStructVariable("distortion", dist_fields);
        size_t rows = combined.distortion.distortion_models.size();
        size_t cols = (rows > 0) ? combined.distortion.distortion_models[0].size() : 0;
        Mat_VarSetStructFieldByName(dist_struct, "distortionModel", 0,
            createCellArray2DFromStrings("distortionModel", combined.distortion.distortion_models, rows, cols));
        Mat_VarSetStructFieldByName(dist_struct, "distortionPath", 0,
            createCellArray2DFromStrings("distortionPath", combined.distortion.distortion_paths, rows, cols));
        Mat_VarSetStructFieldByName(combined_struct, "distortion", 0, dist_struct);
    }
    
    // AllPairsResults and DIC2Dinfo: write to file first, read back, attach to struct
    writeAllPairsResults(matfp, combined.AllPairsResults);
    writeDIC2Dinfo(matfp, combined.DIC2Dinfo);
    matvar_t* apr_var = Mat_VarRead(matfp, "AllPairsResults");
    if (apr_var) Mat_VarSetStructFieldByName(combined_struct, "AllPairsResults", 0, apr_var);
    matvar_t* dic2d_var = Mat_VarRead(matfp, "DIC2Dinfo");
    if (dic2d_var) Mat_VarSetStructFieldByName(combined_struct, "DIC2Dinfo", 0, dic2d_var);
    
    return combined_struct;
}

bool MatWriter::write3DCombinedResults(const std::string& filename,
                                       const DIC3Dcombined& combined,
                                       const std::string& struct_name) {
    mat_t* matfp = createMatFileHDF5(filename);
    if (!matfp) {
        std::cerr << "Failed to create file: " << filename << std::endl;
        return false;
    }
    
    matvar_t* combined_struct = buildCombinedStructFields(matfp, combined, struct_name);
    if (!combined_struct) {
        Mat_Close(matfp);
        return false;
    }
    
    Mat_VarWrite(matfp, combined_struct, MAT_COMPRESSION_NONE);
    Mat_VarFree(combined_struct);
    Mat_Close(matfp);
    std::cout << "Wrote " << struct_name << ": " << filename << std::endl;
    return true;
}

bool MatWriter::write3DPPresults(const std::string& filename,
                                const DIC3DPPresults& ppresults) {
    mat_t* matfp = createMatFileHDF5(filename);
    if (!matfp) {
        std::cerr << "Failed to create DIC3DPPresults file: " << filename << std::endl;
        return false;
    }
    
    // Build combined struct with extra fields for PP results
    std::vector<std::string> extra_fields = {"Deform", "FaceIsoInd", "deftype"};
    matvar_t* pp_struct = buildCombinedStructFields(matfp, ppresults, "DIC3DPPresults", extra_fields);
    if (!pp_struct) {
        Mat_Close(matfp);
        return false;
    }
    
    // Add Deform sub-struct inside the parent struct
    if (!ppresults.deform_full.frames.empty()) {
        std::cout << "Writing full Deform group with " << ppresults.deform_full.n_frames 
                  << " frames and " << ppresults.deform_full.n_faces << " faces..." << std::endl;
        matvar_t* deform_struct = buildDeformationStruct("Deform", ppresults.deform_full);
        if (deform_struct) {
            Mat_VarSetStructFieldByName(pp_struct, "Deform", 0, deform_struct);
        } else {
            std::cerr << "Warning: Failed to build Deform struct" << std::endl;
        }
    } else {
        std::cout << "Warning: No deformation data available, skipping Deform group" << std::endl;
    }
    
    // Add FaceIsoInd as cell array (MATLAB: nFrames×1 cell, each cell is 1×nFaces)
    if (!ppresults.FaceIsoInd.empty()) {
        size_t n_frames_iso = ppresults.FaceIsoInd.size();
        std::vector<size_t> cell_dims = {n_frames_iso, 1};
        matvar_t* iso_cell = Mat_VarCreate("FaceIsoInd", MAT_C_CELL, MAT_T_CELL, 2, cell_dims.data(), nullptr, 0);
        for (size_t i = 0; i < n_frames_iso; ++i) {
            const auto& iso = ppresults.FaceIsoInd[i];
            if (!iso.empty()) {
                std::vector<size_t> d = {1, iso.size()};
                Mat_VarSetCell(iso_cell, i, Mat_VarCreate("", MAT_C_DOUBLE, MAT_T_DOUBLE, 2, d.data(), (void*)iso.data(), 0));
            }
        }
        Mat_VarSetStructFieldByName(pp_struct, "FaceIsoInd", 0, iso_cell);
    }
    
    // Add deftype string inside the struct
    if (!ppresults.deftype.empty()) {
        size_t str_dims[2] = {1, ppresults.deftype.size()};
        matvar_t* deftype_var = Mat_VarCreate("deftype", MAT_C_CHAR, MAT_T_UTF8,
                                              2, str_dims, (void*)ppresults.deftype.c_str(), 0);
        if (deftype_var) {
            Mat_VarSetStructFieldByName(pp_struct, "deftype", 0, deftype_var);
        }
    }
    
    // Write the complete struct and close
    Mat_VarWrite(matfp, pp_struct, MAT_COMPRESSION_NONE);
    Mat_VarFree(pp_struct);
    Mat_Close(matfp);
    std::cout << "Wrote DIC3DPPresults: " << filename << std::endl;
    return true;
}

matvar_t* MatWriter::buildDeformationStruct(const std::string& group_name,
                                              const FrameDeformationResult& deform_data) {
    size_t n_frames = deform_data.n_frames;
    
    // Prepare data: vector<vector<T>> where outer = frames, inner = faces
    std::vector<std::vector<double>> Area_data(n_frames), Lamda1_data(n_frames), Lamda2_data(n_frames);
    std::vector<std::vector<double>> J_data(n_frames), Emgn_data(n_frames), emgn_data(n_frames);
    std::vector<std::vector<double>> Epc1_data(n_frames), Epc2_data(n_frames);
    std::vector<std::vector<double>> epc1_data(n_frames), epc2_data(n_frames);
    std::vector<std::vector<double>> EShearMax_data(n_frames), eShearMax_data(n_frames);
    std::vector<std::vector<double>> Eeq_data(n_frames), eeq_data(n_frames), Dnorm_data(n_frames);
    
    std::vector<std::vector<Eigen::Vector3d>> D1_data(n_frames), D2_data(n_frames), D3_data(n_frames);
    std::vector<std::vector<Eigen::Vector3d>> d1_data(n_frames), d2_data(n_frames), d3_data(n_frames);
    std::vector<std::vector<Eigen::Vector3d>> Drec1_data(n_frames), Drec2_data(n_frames);
    std::vector<std::vector<Eigen::Vector3d>> Epc1vec_data(n_frames), Epc2vec_data(n_frames);
    std::vector<std::vector<Eigen::Vector3d>> Epc1vecCur_data(n_frames), Epc2vecCur_data(n_frames);
    std::vector<std::vector<Eigen::Vector3d>> epc1vec_data(n_frames), epc2vec_data(n_frames);
    std::vector<std::vector<Eigen::Vector3d>> EShearMaxVec1_data(n_frames), EShearMaxVec2_data(n_frames);
    std::vector<std::vector<Eigen::Vector3d>> EShearMaxVecCur1_data(n_frames), EShearMaxVecCur2_data(n_frames);
    std::vector<std::vector<Eigen::Vector3d>> eShearMaxVec1_data(n_frames), eShearMaxVec2_data(n_frames);
    
    std::vector<std::vector<Eigen::Matrix3d>> Fmat_data(n_frames), Cmat_data(n_frames);
    std::vector<std::vector<Eigen::Matrix3d>> Emat_data(n_frames), emat_data(n_frames);
    
    for (size_t i = 0; i < n_frames && i < deform_data.frames.size(); ++i) {
        const auto& frame = deform_data.frames[i];
        Area_data[i] = frame.Area; Lamda1_data[i] = frame.Lamda1; Lamda2_data[i] = frame.Lamda2;
        J_data[i] = frame.J; Emgn_data[i] = frame.Emgn; emgn_data[i] = frame.emgn;
        Epc1_data[i] = frame.Epc1; Epc2_data[i] = frame.Epc2;
        epc1_data[i] = frame.epc1; epc2_data[i] = frame.epc2;
        EShearMax_data[i] = frame.EShearMax; eShearMax_data[i] = frame.eShearMax;
        Eeq_data[i] = frame.Eeq; eeq_data[i] = frame.eeq; Dnorm_data[i] = frame.Dnorm;
        D1_data[i] = frame.D1; D2_data[i] = frame.D2; D3_data[i] = frame.D3;
        d1_data[i] = frame.d1; d2_data[i] = frame.d2; d3_data[i] = frame.d3;
        Drec1_data[i] = frame.Drec1; Drec2_data[i] = frame.Drec2;
        Epc1vec_data[i] = frame.Epc1vec; Epc2vec_data[i] = frame.Epc2vec;
        Epc1vecCur_data[i] = frame.Epc1vecCur; Epc2vecCur_data[i] = frame.Epc2vecCur;
        epc1vec_data[i] = frame.epc1vec; epc2vec_data[i] = frame.epc2vec;
        EShearMaxVec1_data[i] = frame.EShearMaxVec1; EShearMaxVec2_data[i] = frame.EShearMaxVec2;
        EShearMaxVecCur1_data[i] = frame.EShearMaxVecCur1; EShearMaxVecCur2_data[i] = frame.EShearMaxVecCur2;
        eShearMaxVec1_data[i] = frame.eShearMaxVec1; eShearMaxVec2_data[i] = frame.eShearMaxVec2;
        Fmat_data[i] = frame.Fmat; Cmat_data[i] = frame.Cmat;
        Emat_data[i] = frame.Emat; emat_data[i] = frame.emat;
    }
    
    // Create cell arrays for all fields
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
    
    matvar_t* Fmat_cell = createCellArrayFromMatrices("Fmat", Fmat_data, n_frames);
    matvar_t* Cmat_cell = createCellArrayFromMatrices("Cmat", Cmat_data, n_frames);
    matvar_t* Emat_cell = createCellArrayFromMatrices("Emat", Emat_data, n_frames);
    matvar_t* emat_cell = createCellArrayFromMatrices("emat", emat_data, n_frames);
    
    // Create parent struct
    std::vector<std::string> deform_fields = {
        "Area", "Lamda1", "Lamda2", "J", "Emgn", "emgn",
        "Epc1", "Epc2", "epc1", "epc2", "EShearMax", "eShearMax",
        "Eeq", "eeq", "Dnorm", "D1", "D2", "D3", "d1", "d2", "d3",
        "Drec1", "Drec2", "Epc1vec", "Epc2vec", "Epc1vecCur", "Epc2vecCur",
        "epc1vec", "epc2vec", "EShearMaxVec1", "EShearMaxVec2",
        "EShearMaxVecCur1", "EShearMaxVecCur2", "eShearMaxVec1", "eShearMaxVec2",
        "Fmat", "Cmat", "Emat", "emat"
    };
    matvar_t* deform_struct = createStructVariable(group_name, deform_fields);
    
    Mat_VarSetStructFieldByName(deform_struct, "Area", 0, Area_cell);
    Mat_VarSetStructFieldByName(deform_struct, "Lamda1", 0, Lamda1_cell);
    Mat_VarSetStructFieldByName(deform_struct, "Lamda2", 0, Lamda2_cell);
    Mat_VarSetStructFieldByName(deform_struct, "J", 0, J_cell);
    Mat_VarSetStructFieldByName(deform_struct, "Emgn", 0, Emgn_cell);
    Mat_VarSetStructFieldByName(deform_struct, "emgn", 0, emgn_cell);
    Mat_VarSetStructFieldByName(deform_struct, "Epc1", 0, Epc1_cell);
    Mat_VarSetStructFieldByName(deform_struct, "Epc2", 0, Epc2_cell);
    Mat_VarSetStructFieldByName(deform_struct, "epc1", 0, epc1_cell);
    Mat_VarSetStructFieldByName(deform_struct, "epc2", 0, epc2_cell);
    Mat_VarSetStructFieldByName(deform_struct, "EShearMax", 0, EShearMax_cell);
    Mat_VarSetStructFieldByName(deform_struct, "eShearMax", 0, eShearMax_cell);
    Mat_VarSetStructFieldByName(deform_struct, "Eeq", 0, Eeq_cell);
    Mat_VarSetStructFieldByName(deform_struct, "eeq", 0, eeq_cell);
    Mat_VarSetStructFieldByName(deform_struct, "Dnorm", 0, Dnorm_cell);
    Mat_VarSetStructFieldByName(deform_struct, "D1", 0, D1_cell);
    Mat_VarSetStructFieldByName(deform_struct, "D2", 0, D2_cell);
    Mat_VarSetStructFieldByName(deform_struct, "D3", 0, D3_cell);
    Mat_VarSetStructFieldByName(deform_struct, "d1", 0, d1_cell);
    Mat_VarSetStructFieldByName(deform_struct, "d2", 0, d2_cell);
    Mat_VarSetStructFieldByName(deform_struct, "d3", 0, d3_cell);
    Mat_VarSetStructFieldByName(deform_struct, "Drec1", 0, Drec1_cell);
    Mat_VarSetStructFieldByName(deform_struct, "Drec2", 0, Drec2_cell);
    Mat_VarSetStructFieldByName(deform_struct, "Epc1vec", 0, Epc1vec_cell);
    Mat_VarSetStructFieldByName(deform_struct, "Epc2vec", 0, Epc2vec_cell);
    Mat_VarSetStructFieldByName(deform_struct, "Epc1vecCur", 0, Epc1vecCur_cell);
    Mat_VarSetStructFieldByName(deform_struct, "Epc2vecCur", 0, Epc2vecCur_cell);
    Mat_VarSetStructFieldByName(deform_struct, "epc1vec", 0, epc1vec_cell);
    Mat_VarSetStructFieldByName(deform_struct, "epc2vec", 0, epc2vec_cell);
    Mat_VarSetStructFieldByName(deform_struct, "EShearMaxVec1", 0, EShearMaxVec1_cell);
    Mat_VarSetStructFieldByName(deform_struct, "EShearMaxVec2", 0, EShearMaxVec2_cell);
    Mat_VarSetStructFieldByName(deform_struct, "EShearMaxVecCur1", 0, EShearMaxVecCur1_cell);
    Mat_VarSetStructFieldByName(deform_struct, "EShearMaxVecCur2", 0, EShearMaxVecCur2_cell);
    Mat_VarSetStructFieldByName(deform_struct, "eShearMaxVec1", 0, eShearMaxVec1_cell);
    Mat_VarSetStructFieldByName(deform_struct, "eShearMaxVec2", 0, eShearMaxVec2_cell);
    Mat_VarSetStructFieldByName(deform_struct, "Fmat", 0, Fmat_cell);
    Mat_VarSetStructFieldByName(deform_struct, "Cmat", 0, Cmat_cell);
    Mat_VarSetStructFieldByName(deform_struct, "Emat", 0, Emat_cell);
    Mat_VarSetStructFieldByName(deform_struct, "emat", 0, emat_cell);
    
    std::cout << "Built deformation struct '" << group_name << "' with " 
              << n_frames << " frames and " << deform_data.n_faces << " faces" << std::endl;
    return deform_struct;
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
    if (!matfp) {
        std::cerr << "Invalid MAT file pointer" << std::endl;
        return false;
    }
    
    std::cout << "Writing AllPairsResults with " << all_pairs.size() << " pairs..." << std::endl;
    
    if (all_pairs.empty()) {
        std::cout << "Warning: Empty AllPairsResults, skipping" << std::endl;
        return true;
    }
    
    // Create cell array (1 x n_pairs)
    size_t n_pairs = all_pairs.size();
    std::vector<size_t> cell_dims = {1, n_pairs};
    matvar_t* cell_array = Mat_VarCreate("AllPairsResults", MAT_C_CELL, MAT_T_CELL,
                                         2, cell_dims.data(), nullptr, 0);
    
    if (!cell_array) {
        std::cerr << "Failed to create AllPairsResults cell array" << std::endl;
        return false;
    }
    
    // Write each pair as a struct
    for (size_t i = 0; i < n_pairs; ++i) {
        const auto& pair = all_pairs[i];
        
        // Create struct for this pair
        std::vector<std::string> pair_fields = {
            "cameraPairInd", "calibration", "distortionModel", "distortionPath",
            "Faces", "FaceColors", "Points3D", "Disp", "FaceCentroids",
            "corrComb", "FaceCorrComb"
        };
        matvar_t* pair_struct = createStructVariable("pair", pair_fields);
        
        // Write cameraPairInd (MATLAB: 2×1 double)
        if (!pair.cameraPairInd.empty()) {
            std::vector<size_t> dims = {pair.cameraPairInd.size(), 1};
            std::vector<double> cam_pair_dbl(pair.cameraPairInd.size());
            for (size_t j = 0; j < pair.cameraPairInd.size(); ++j)
                cam_pair_dbl[j] = static_cast<double>(pair.cameraPairInd[j]);
            matvar_t* cam_var = Mat_VarCreate("cameraPairInd", MAT_C_DOUBLE, MAT_T_DOUBLE,
                                             2, dims.data(), cam_pair_dbl.data(), 0);
            Mat_VarSetStructFieldByName(pair_struct, "cameraPairInd", 0, cam_var);
        }
        
        // Write calibration struct
        std::vector<std::string> calib_fields = {"DLTpath", "DLTparameters"};
        matvar_t* calib_struct = createStructVariable("calibration", calib_fields);
        
        // DLTpath (cell 2x1)
        if (!pair.DLTpath.empty()) {
            std::vector<size_t> path_dims = {2, 1};
            matvar_t* path_cell = Mat_VarCreate("DLTpath", MAT_C_CELL, MAT_T_CELL,
                                               2, path_dims.data(), nullptr, 0);
            for (size_t j = 0; j < pair.DLTpath.size() && j < 2; ++j) {
                std::vector<size_t> str_dims = {1, pair.DLTpath[j].length()};
                matvar_t* str_var = Mat_VarCreate(nullptr, MAT_C_CHAR, MAT_T_UINT8,
                                                 2, str_dims.data(), (void*)pair.DLTpath[j].c_str(), 0);
                Mat_VarSetCell(path_cell, j, str_var);
            }
            Mat_VarSetStructFieldByName(calib_struct, "DLTpath", 0, path_cell);
        }
        
        // DLTparameters (cell 2x1, each cell is 1×11 double)
        if (!pair.DLTparameters.empty()) {
            std::vector<size_t> param_dims = {2, 1};
            matvar_t* param_cell = Mat_VarCreate("DLTparameters", MAT_C_CELL, MAT_T_CELL,
                                                2, param_dims.data(), nullptr, 0);
            for (size_t j = 0; j < pair.DLTparameters.size() && j < 2; ++j) {
                if (!pair.DLTparameters[j].empty()) {
                    std::vector<size_t> vec_dims = {1, pair.DLTparameters[j].size()};
                    matvar_t* vec_var = Mat_VarCreate(nullptr, MAT_C_DOUBLE, MAT_T_DOUBLE,
                                                     2, vec_dims.data(), (void*)pair.DLTparameters[j].data(), 0);
                    Mat_VarSetCell(param_cell, j, vec_var);
                }
            }
            Mat_VarSetStructFieldByName(calib_struct, "DLTparameters", 0, param_cell);
        }
        
        Mat_VarSetStructFieldByName(pair_struct, "calibration", 0, calib_struct);
        
        // Write distortionModel (cell 2x1)
        if (!pair.distortionModel.empty()) {
            std::vector<size_t> model_dims = {2, 1};
            matvar_t* model_cell = Mat_VarCreate("distortionModel", MAT_C_CELL, MAT_T_CELL,
                                                2, model_dims.data(), nullptr, 0);
            for (size_t j = 0; j < pair.distortionModel.size() && j < 2; ++j) {
                std::vector<size_t> str_dims = {1, pair.distortionModel[j].length()};
                matvar_t* str_var = Mat_VarCreate(nullptr, MAT_C_CHAR, MAT_T_UINT8,
                                                 2, str_dims.data(), (void*)pair.distortionModel[j].c_str(), 0);
                Mat_VarSetCell(model_cell, j, str_var);
            }
            Mat_VarSetStructFieldByName(pair_struct, "distortionModel", 0, model_cell);
        }
        
        // Write distortionPath (cell 2x1)
        if (!pair.distortionPath.empty()) {
            std::vector<size_t> path_dims = {2, 1};
            matvar_t* path_cell = Mat_VarCreate("distortionPath", MAT_C_CELL, MAT_T_CELL,
                                               2, path_dims.data(), nullptr, 0);
            for (size_t j = 0; j < pair.distortionPath.size() && j < 2; ++j) {
                std::vector<size_t> str_dims = {1, pair.distortionPath[j].length()};
                matvar_t* str_var = Mat_VarCreate(nullptr, MAT_C_CHAR, MAT_T_UINT8,
                                                 2, str_dims.data(), (void*)pair.distortionPath[j].c_str(), 0);
                Mat_VarSetCell(path_cell, j, str_var);
            }
            Mat_VarSetStructFieldByName(pair_struct, "distortionPath", 0, path_cell);
        }
        
        // Write Faces (3 x nFaces, convert 0-indexed C++ to 1-indexed MATLAB)
        if (!pair.Faces.empty()) {
            size_t nFaces = pair.Faces.size() / 3;
            std::vector<size_t> face_dims = {3, nFaces};
            std::vector<double> faces_double(pair.Faces.size());
            for (size_t i = 0; i < pair.Faces.size(); ++i) {
                faces_double[i] = static_cast<double>(pair.Faces[i] + 1);
            }
            matvar_t* faces_var = Mat_VarCreate("Faces", MAT_C_DOUBLE, MAT_T_DOUBLE,
                                               2, face_dims.data(), faces_double.data(), 0);
            Mat_VarSetStructFieldByName(pair_struct, "Faces", 0, faces_var);
        }
        
        // Write FaceColors (1 x nFaces)
        if (!pair.FaceColors.empty()) {
            std::vector<size_t> fc_dims = {1, pair.FaceColors.size()};
            matvar_t* fc_var = Mat_VarCreate("FaceColors", MAT_C_DOUBLE, MAT_T_DOUBLE,
                                            2, fc_dims.data(), (void*)pair.FaceColors.data(), 0);
            Mat_VarSetStructFieldByName(pair_struct, "FaceColors", 0, fc_var);
        }
        
        // Write Points3D (MATLAB: 1×nImages cell, each cell is 3×nPts)
        if (!pair.Points3D.empty()) {
            std::vector<size_t> pts_dims = {1, pair.Points3D.size()};
            matvar_t* pts_cell = Mat_VarCreate("Points3D", MAT_C_CELL, MAT_T_CELL,
                                              2, pts_dims.data(), nullptr, 0);
            for (size_t frame = 0; frame < pair.Points3D.size(); ++frame) {
                const auto& pts = pair.Points3D[frame];
                size_t nPts = pts.x.size();
                std::vector<size_t> pt_dims = {3, nPts};
                std::vector<double> pts_data(nPts * 3);
                for (size_t p = 0; p < nPts; ++p) {
                    pts_data[p * 3 + 0] = pts.x[p];
                    pts_data[p * 3 + 1] = pts.y[p];
                    pts_data[p * 3 + 2] = pts.z[p];
                }
                matvar_t* pt_var = Mat_VarCreate(nullptr, MAT_C_DOUBLE, MAT_T_DOUBLE,
                                                2, pt_dims.data(), pts_data.data(), 0);
                Mat_VarSetCell(pts_cell, frame, pt_var);
            }
            Mat_VarSetStructFieldByName(pair_struct, "Points3D", 0, pts_cell);
        }
        
        // Write Disp struct
        std::vector<std::string> disp_fields = {"DispVec", "DispMgn"};
        matvar_t* disp_struct = createStructVariable("Disp", disp_fields);
        
        // DispVec (MATLAB: 1×nImages cell, each cell is 3×nPts)
        if (!pair.Disp.DispVec.empty()) {
            std::vector<size_t> dv_dims = {1, pair.Disp.DispVec.size()};
            matvar_t* dv_cell = Mat_VarCreate("DispVec", MAT_C_CELL, MAT_T_CELL,
                                             2, dv_dims.data(), nullptr, 0);
            for (size_t frame = 0; frame < pair.Disp.DispVec.size(); ++frame) {
                const auto& vec = pair.Disp.DispVec[frame];
                if (!vec.empty()) {
                    size_t nPts = vec.size() / 3;
                    std::vector<size_t> vec_dims = {3, nPts};
                    matvar_t* vec_var = Mat_VarCreate(nullptr, MAT_C_DOUBLE, MAT_T_DOUBLE,
                                                     2, vec_dims.data(), (void*)vec.data(), 0);
                    Mat_VarSetCell(dv_cell, frame, vec_var);
                }
            }
            Mat_VarSetStructFieldByName(disp_struct, "DispVec", 0, dv_cell);
        }
        
        // DispMgn (MATLAB: 1×nImages cell, each cell is 1×nPts)
        if (!pair.Disp.DispMgn.empty()) {
            std::vector<size_t> dm_dims = {1, pair.Disp.DispMgn.size()};
            matvar_t* dm_cell = Mat_VarCreate("DispMgn", MAT_C_CELL, MAT_T_CELL,
                                             2, dm_dims.data(), nullptr, 0);
            for (size_t frame = 0; frame < pair.Disp.DispMgn.size(); ++frame) {
                const auto& mgn = pair.Disp.DispMgn[frame];
                if (!mgn.empty()) {
                    std::vector<size_t> mgn_dims = {1, mgn.size()};
                    matvar_t* mgn_var = Mat_VarCreate(nullptr, MAT_C_DOUBLE, MAT_T_DOUBLE,
                                                     2, mgn_dims.data(), (void*)mgn.data(), 0);
                    Mat_VarSetCell(dm_cell, frame, mgn_var);
                }
            }
            Mat_VarSetStructFieldByName(disp_struct, "DispMgn", 0, dm_cell);
        }
        
        Mat_VarSetStructFieldByName(pair_struct, "Disp", 0, disp_struct);
        
        // Write FaceCentroids (MATLAB: 1×nImages cell, each cell is 3×nFaces)
        if (!pair.FaceCentroids.empty()) {
            std::vector<size_t> fc_dims = {1, pair.FaceCentroids.size()};
            matvar_t* fc_cell = Mat_VarCreate("FaceCentroids", MAT_C_CELL, MAT_T_CELL,
                                             2, fc_dims.data(), nullptr, 0);
            for (size_t frame = 0; frame < pair.FaceCentroids.size(); ++frame) {
                const auto& centroids = pair.FaceCentroids[frame];
                if (!centroids.empty()) {
                    size_t nFaces = centroids.size() / 3;
                    std::vector<size_t> cent_dims = {3, nFaces};
                    matvar_t* cent_var = Mat_VarCreate(nullptr, MAT_C_DOUBLE, MAT_T_DOUBLE,
                                                      2, cent_dims.data(), (void*)centroids.data(), 0);
                    Mat_VarSetCell(fc_cell, frame, cent_var);
                }
            }
            Mat_VarSetStructFieldByName(pair_struct, "FaceCentroids", 0, fc_cell);
        }
        
        // Write corrComb (MATLAB: 1×nImages cell, each cell is 1×nPoints)
        if (!pair.corrComb.empty()) {
            std::vector<size_t> cc_dims = {1, pair.corrComb.size()};
            matvar_t* cc_cell = Mat_VarCreate("corrComb", MAT_C_CELL, MAT_T_CELL,
                                             2, cc_dims.data(), nullptr, 0);
            for (size_t frame = 0; frame < pair.corrComb.size(); ++frame) {
                const auto& corr = pair.corrComb[frame];
                if (!corr.empty()) {
                    std::vector<size_t> corr_dims = {1, corr.size()};
                    matvar_t* corr_var = Mat_VarCreate(nullptr, MAT_C_DOUBLE, MAT_T_DOUBLE,
                                                      2, corr_dims.data(), (void*)corr.data(), 0);
                    Mat_VarSetCell(cc_cell, frame, corr_var);
                }
            }
            Mat_VarSetStructFieldByName(pair_struct, "corrComb", 0, cc_cell);
        }
        
        // Write FaceCorrComb (MATLAB: 1×nImages cell, each cell is 1×nFaces)
        if (!pair.FaceCorrComb.empty()) {
            std::vector<size_t> fcc_dims = {1, pair.FaceCorrComb.size()};
            matvar_t* fcc_cell = Mat_VarCreate("FaceCorrComb", MAT_C_CELL, MAT_T_CELL,
                                              2, fcc_dims.data(), nullptr, 0);
            for (size_t frame = 0; frame < pair.FaceCorrComb.size(); ++frame) {
                const auto& face_corr = pair.FaceCorrComb[frame];
                if (!face_corr.empty()) {
                    std::vector<size_t> fc_dims = {1, face_corr.size()};
                    matvar_t* fc_var = Mat_VarCreate(nullptr, MAT_C_DOUBLE, MAT_T_DOUBLE,
                                                    2, fc_dims.data(), (void*)face_corr.data(), 0);
                    Mat_VarSetCell(fcc_cell, frame, fc_var);
                }
            }
            Mat_VarSetStructFieldByName(pair_struct, "FaceCorrComb", 0, fcc_cell);
        }
        
        // Set this pair struct into the cell array
        Mat_VarSetCell(cell_array, i, pair_struct);
    }
    
    // Write to file
    Mat_VarWrite(matfp, cell_array, MAT_COMPRESSION_NONE);
    Mat_VarFree(cell_array);
    
    std::cout << "✓ AllPairsResults written successfully (" << n_pairs << " pairs)" << std::endl;
    return true;
}

bool MatWriter::writeDIC2Dinfo(mat_t* matfp,
                              const std::vector<DIC2DPairResults>& dic2d_info) {
    if (!matfp) {
        std::cerr << "Invalid MAT file pointer" << std::endl;
        return false;
    }
    
    std::cout << "Writing DIC2Dinfo with " << dic2d_info.size() << " entries..." << std::endl;
    
    if (dic2d_info.empty()) {
        std::cout << "Warning: Empty DIC2Dinfo, skipping" << std::endl;
        return true;
    }
    
    // Create cell array (n_entries x 1)
    size_t n_entries = dic2d_info.size();
    std::vector<size_t> array_dims = {n_entries, 1};
    matvar_t* obj_array = Mat_VarCreate("DIC2Dinfo", MAT_C_CELL, MAT_T_CELL,
                                        2, array_dims.data(), nullptr, 0);
    
    if (!obj_array) {
        std::cerr << "Failed to create DIC2Dinfo cell array" << std::endl;
        return false;
    }
    
    // Write each DIC2DPairResults as a struct
    for (size_t i = 0; i < n_entries; ++i) {
        const auto& dic2d = dic2d_info[i];
        
        // Create struct for this entry
        std::vector<std::string> dic2d_fields = {
            "nCamRef", "nCamDef", "nImages", "pairOrder", "pairForced", "ROImask", "ncorrInfo",
            "Points", "CorCoeffVec", "Faces", "FaceColors"
        };
        matvar_t* dic2d_struct = createStructVariable("DIC2D", dic2d_fields);
        
        // Write scalar fields (MATLAB stores as 1×1 double)
        double ncamref_dbl = static_cast<double>(dic2d.nCamRef);
        matvar_t* ncamref_var = Mat_VarCreate("nCamRef", MAT_C_DOUBLE, MAT_T_DOUBLE,
                                             2, (size_t[]){1,1}, &ncamref_dbl, 0);
        Mat_VarSetStructFieldByName(dic2d_struct, "nCamRef", 0, ncamref_var);
        
        double ncamdef_dbl = static_cast<double>(dic2d.nCamDef);
        matvar_t* ncamdef_var = Mat_VarCreate("nCamDef", MAT_C_DOUBLE, MAT_T_DOUBLE,
                                             2, (size_t[]){1,1}, &ncamdef_dbl, 0);
        Mat_VarSetStructFieldByName(dic2d_struct, "nCamDef", 0, ncamdef_var);
        
        double nimages_dbl = static_cast<double>(dic2d.nImages);
        matvar_t* nimages_var = Mat_VarCreate("nImages", MAT_C_DOUBLE, MAT_T_DOUBLE,
                                             2, (size_t[]){1,1}, &nimages_dbl, 0);
        Mat_VarSetStructFieldByName(dic2d_struct, "nImages", 0, nimages_var);

        if (!dic2d.pairOrder.empty()) {
            std::vector<double> pair_order(dic2d.pairOrder.begin(), dic2d.pairOrder.end());
            std::vector<size_t> pair_dims = {1, pair_order.size()};
            matvar_t* pair_order_var = Mat_VarCreate("pairOrder", MAT_C_DOUBLE, MAT_T_DOUBLE,
                                                    2, pair_dims.data(), pair_order.data(), 0);
            Mat_VarSetStructFieldByName(dic2d_struct, "pairOrder", 0, pair_order_var);
        }

        double pair_forced_dbl = dic2d.pairForced ? 1.0 : 0.0;
        matvar_t* pair_forced_var = Mat_VarCreate("pairForced", MAT_C_DOUBLE, MAT_T_DOUBLE,
                                                  2, (size_t[]){1,1}, &pair_forced_dbl, 0);
        Mat_VarSetStructFieldByName(dic2d_struct, "pairForced", 0, pair_forced_var);
        
        // Write ROImask if available
        if (!dic2d.ROImask.empty()) {
            writeMatVariable(matfp, "temp_roimask", dic2d.ROImask);
            matvar_t* roi_var = Mat_VarRead(matfp, "temp_roimask");
            Mat_VarSetStructFieldByName(dic2d_struct, "ROImask", 0, roi_var);
        }
        
        // Write ncorrInfo struct (reuse existing logic from writeDIC2DPairResults)
        // For now, create minimal ncorrInfo with basic fields
        std::vector<std::string> ncorr_fields = {
            "cutoff_corrcoef", "cutoff_diffnorm", "cutoff_iteration",
            "imgcorr", "lenscoef", "pixtounits", "radius", "spacing",
            "stepanalysis", "subsettrunc", "total_threads", "type", "units"
        };
        matvar_t* ncorr_struct = createStructVariable("ncorrInfo", ncorr_fields);
        
        // Add basic ncorrInfo fields from dic2d.ncorrInfo
        if (!dic2d.ncorrInfo.cutoff_corrcoef.empty()) {
            std::vector<size_t> dims = {1, dic2d.ncorrInfo.cutoff_corrcoef.size()};
            matvar_t* cutoff_var = Mat_VarCreate("cutoff_corrcoef", MAT_C_DOUBLE, MAT_T_DOUBLE,
                                                2, dims.data(), (void*)dic2d.ncorrInfo.cutoff_corrcoef.data(), 0);
            Mat_VarSetStructFieldByName(ncorr_struct, "cutoff_corrcoef", 0, cutoff_var);
        }
        
        // Add stepanalysis
        std::vector<std::string> stepanalysis_fields = {"enabled", "type", "auto", "step"};
        matvar_t* stepanalysis_struct = createStructVariable("stepanalysis", stepanalysis_fields);
        
        double enabled_dbl = dic2d.ncorrInfo.stepanalysis.enabled ? 1.0 : 0.0;
        matvar_t* enabled_var = Mat_VarCreate("enabled", MAT_C_DOUBLE, MAT_T_DOUBLE,
                                             2, (size_t[]){1,1}, &enabled_dbl, 0);
        Mat_VarSetStructFieldByName(stepanalysis_struct, "enabled", 0, enabled_var);
        
        if (!dic2d.ncorrInfo.stepanalysis.type.empty()) {
            std::vector<size_t> dims = {1, dic2d.ncorrInfo.stepanalysis.type.length()};
            matvar_t* type_var = Mat_VarCreate("type", MAT_C_CHAR, MAT_T_UINT8,
                                              2, dims.data(), (void*)dic2d.ncorrInfo.stepanalysis.type.c_str(), 0);
            Mat_VarSetStructFieldByName(stepanalysis_struct, "type", 0, type_var);
        }
        
        double auto_dbl = dic2d.ncorrInfo.stepanalysis.auto_update ? 1.0 : 0.0;
        matvar_t* auto_var = Mat_VarCreate("auto", MAT_C_DOUBLE, MAT_T_DOUBLE,
                                          2, (size_t[]){1,1}, &auto_dbl, 0);
        Mat_VarSetStructFieldByName(stepanalysis_struct, "auto", 0, auto_var);
        
        double step_dbl = static_cast<double>(dic2d.ncorrInfo.stepanalysis.step);
        matvar_t* step_var = Mat_VarCreate("step", MAT_C_DOUBLE, MAT_T_DOUBLE,
                                          2, (size_t[]){1,1}, &step_dbl, 0);
        Mat_VarSetStructFieldByName(stepanalysis_struct, "step", 0, step_var);
        
        Mat_VarSetStructFieldByName(ncorr_struct, "stepanalysis", 0, stepanalysis_struct);
        
        Mat_VarSetStructFieldByName(dic2d_struct, "ncorrInfo", 0, ncorr_struct);
        
        // Write Points (MATLAB: 1×nFrames cell, each cell is 2×nPts)
        if (!dic2d.Points.empty()) {
            std::vector<size_t> pts_dims = {1, dic2d.Points.size()};
            matvar_t* pts_cell = Mat_VarCreate("Points", MAT_C_CELL, MAT_T_CELL,
                                              2, pts_dims.data(), nullptr, 0);
            for (size_t frame = 0; frame < dic2d.Points.size(); ++frame) {
                const auto& pts = dic2d.Points[frame];
                size_t nPts = pts.x.size();
                if (nPts > 0) {
                    std::vector<size_t> pt_dims = {2, nPts};
                    std::vector<double> pts_data(nPts * 2);
                    for (size_t p = 0; p < nPts; ++p) {
                        pts_data[p * 2 + 0] = pts.x[p];
                        pts_data[p * 2 + 1] = pts.y[p];
                    }
                    matvar_t* pt_var = Mat_VarCreate(nullptr, MAT_C_DOUBLE, MAT_T_DOUBLE,
                                                    2, pt_dims.data(), pts_data.data(), 0);
                    Mat_VarSetCell(pts_cell, frame, pt_var);
                }
            }
            Mat_VarSetStructFieldByName(dic2d_struct, "Points", 0, pts_cell);
        }
        
        // Write CorCoeffVec (MATLAB: 1×nFrames cell, each cell is 1×nPts)
        if (!dic2d.CorCoeffVec.empty()) {
            std::vector<size_t> cc_dims = {1, dic2d.CorCoeffVec.size()};
            matvar_t* cc_cell = Mat_VarCreate("CorCoeffVec", MAT_C_CELL, MAT_T_CELL,
                                             2, cc_dims.data(), nullptr, 0);
            for (size_t frame = 0; frame < dic2d.CorCoeffVec.size(); ++frame) {
                const auto& corr = dic2d.CorCoeffVec[frame];
                if (!corr.empty()) {
                    std::vector<size_t> corr_dims = {1, corr.size()};
                    matvar_t* corr_var = Mat_VarCreate(nullptr, MAT_C_DOUBLE, MAT_T_DOUBLE,
                                                      2, corr_dims.data(), (void*)corr.data(), 0);
                    Mat_VarSetCell(cc_cell, frame, corr_var);
                }
            }
            Mat_VarSetStructFieldByName(dic2d_struct, "CorCoeffVec", 0, cc_cell);
        }
        
        // Write Faces (convert 0-indexed C++ to 1-indexed MATLAB)
        if (!dic2d.Faces.empty()) {
            size_t nFaces = dic2d.Faces.size() / 3;
            std::vector<size_t> face_dims = {3, nFaces};
            std::vector<double> faces_double(dic2d.Faces.size());
            for (size_t fi = 0; fi < dic2d.Faces.size(); ++fi) {
                faces_double[fi] = static_cast<double>(dic2d.Faces[fi] + 1);
            }
            matvar_t* faces_var = Mat_VarCreate("Faces", MAT_C_DOUBLE, MAT_T_DOUBLE,
                                               2, face_dims.data(), faces_double.data(), 0);
            Mat_VarSetStructFieldByName(dic2d_struct, "Faces", 0, faces_var);
        }
        
        // Write FaceColors
        if (!dic2d.FaceColors.empty()) {
            std::vector<size_t> fc_dims = {1, dic2d.FaceColors.size()};
            matvar_t* fc_var = Mat_VarCreate("FaceColors", MAT_C_DOUBLE, MAT_T_DOUBLE,
                                            2, fc_dims.data(), (void*)dic2d.FaceColors.data(), 0);
            Mat_VarSetStructFieldByName(dic2d_struct, "FaceColors", 0, fc_var);
        }
        
        // Set this struct into the cell array
        Mat_VarSetCell(obj_array, i, dic2d_struct);
    }
    
    // Write to file
    Mat_VarWrite(matfp, obj_array, MAT_COMPRESSION_NONE);
    Mat_VarFree(obj_array);
    
    std::cout << "✓ DIC2Dinfo written successfully (" << n_entries << " entries)" << std::endl;
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
        
        // Create double array for this frame (MATLAB row vector: 1×N)
        std::vector<size_t> dims = {1, n_elements};
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
        
        // Create 3×N array (MATLAB column-major: interleaved xyz)
        std::vector<size_t> dims = {3, n_vectors};
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
    
    // Fill each cell with N×3×3 3D array (MATLAB convention)
    for (size_t i = 0; i < n_frames && i < data.size(); ++i) {
        const auto& frame_data = data[i];
        size_t n_matrices = frame_data.size();
        
        if (n_matrices == 0) {
            Mat_VarSetCell(cell_array, i, nullptr);
            continue;
        }
        
        // Convert vector<Matrix3d> to MATLAB [N,3,3] column-major 3D array
        // MATLAB indexing: A(i, row, col) = data[i + N*row + N*3*col]
        std::vector<double> flat_data(n_matrices * 9);
        for (size_t j = 0; j < n_matrices; ++j) {
            for (int col = 0; col < 3; ++col) {
                for (int row = 0; row < 3; ++row) {
                    flat_data[j + n_matrices * row + n_matrices * 3 * col] = frame_data[j](row, col);
                }
            }
        }
        
        // Create N×3×3 array
        std::vector<size_t> dims = {n_matrices, 3, 3};
        matvar_t* cell_data = Mat_VarCreate(nullptr, MAT_C_DOUBLE, MAT_T_DOUBLE,
                                           3, dims.data(), flat_data.data(), 0);
        
        Mat_VarSetCell(cell_array, i, cell_data);
    }
    
    return cell_array;
}

matvar_t* MatWriter::createCellArray2DFromStrings(
    const std::string& name,
    const std::vector<std::vector<std::string>>& data,
    size_t rows,
    size_t cols) {
    
    // Create 2D cell array (rows x cols)
    std::vector<size_t> cell_dims = {rows, cols};
    matvar_t* cell_array = Mat_VarCreate(name.c_str(), MAT_C_CELL, MAT_T_CELL,
                                         2, cell_dims.data(), nullptr, 0);
    
    if (!cell_array) {
        std::cerr << "Failed to create 2D cell array: " << name << std::endl;
        return nullptr;
    }
    
    // Fill each cell with string
    for (size_t i = 0; i < rows && i < data.size(); ++i) {
        for (size_t j = 0; j < cols && j < data[i].size(); ++j) {
            const std::string& str = data[i][j];
            
            if (str.empty()) {
                // Empty cell
                Mat_VarSetCell(cell_array, i * cols + j, nullptr);
                continue;
            }
            
            // Create string variable
            std::vector<size_t> dims = {1, str.length()};
            matvar_t* str_var = Mat_VarCreate(nullptr, MAT_C_CHAR, MAT_T_UINT8,
                                             2, dims.data(), (void*)str.c_str(), 0);
            
            Mat_VarSetCell(cell_array, i * cols + j, str_var);
        }
    }
    
    return cell_array;
}

matvar_t* MatWriter::createCellArray2DFromVectors(
    const std::string& name,
    const std::vector<std::vector<std::vector<double>>>& data,
    size_t rows,
    size_t cols) {
    
    // Create 2D cell array (rows x cols)
    std::vector<size_t> cell_dims = {rows, cols};
    matvar_t* cell_array = Mat_VarCreate(name.c_str(), MAT_C_CELL, MAT_T_CELL,
                                         2, cell_dims.data(), nullptr, 0);
    
    if (!cell_array) {
        std::cerr << "Failed to create 2D cell array: " << name << std::endl;
        return nullptr;
    }
    
    // Fill each cell with double vector
    for (size_t i = 0; i < rows && i < data.size(); ++i) {
        for (size_t j = 0; j < cols && j < data[i].size(); ++j) {
            const auto& vec = data[i][j];
            
            if (vec.empty()) {
                // Empty cell
                Mat_VarSetCell(cell_array, i * cols + j, nullptr);
                continue;
            }
            
            // Create double array (MATLAB: 1×N row vector)
            std::vector<size_t> dims = {1, vec.size()};
            matvar_t* vec_var = Mat_VarCreate(nullptr, MAT_C_DOUBLE, MAT_T_DOUBLE,
                                             2, dims.data(), (void*)vec.data(), 0);
            
            Mat_VarSetCell(cell_array, i * cols + j, vec_var);
        }
    }
    
    return cell_array;
}

} // namespace cppxdic
