/**
 * MAT File Writer for CPPXDIC
 * Implementation of MAT file writing in xDIC format
 * Produces MATLAB v7.3 (HDF5) compatible output
 */

#include "mat_writer.h"
#include <iostream>
#include <cstring>
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
    
    // Create MAT file (v7.3 HDF5 format for xDIC compatibility)
    mat_t* matfp = createMatFileHDF5(filename);
    if (!matfp) {
        std::cerr << "Failed to create MAT file: " << filename << std::endl;
        return false;
    }
    
    // Create reference_save struct
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
    
    // Create current_save struct
    matvar_t* current_save = createStructVariable("current_save", ref_fields);
    writeMatVariable(matfp, "cur_gs_temp", cur_img);
    matvar_t* cur_gs = Mat_VarRead(matfp, "cur_gs_temp");
    addFieldToStruct(current_save, "gs", cur_gs, 0);
    
    matvar_t* cur_roi_struct = createStructVariable("roi", roi_fields);
    writeMatVariable(matfp, "cur_roi_temp", cur_roi);
    matvar_t* cur_roi_mask = Mat_VarRead(matfp, "cur_roi_temp");
    addFieldToStruct(cur_roi_struct, "mask", cur_roi_mask, 0);
    addFieldToStruct(current_save, "roi", cur_roi_struct, 0);
    
    // Create data_dic_save struct
    std::vector<std::string> data_fields = {"dispinfo", "displacements"};
    matvar_t* data_dic_save = createStructVariable("data_dic_save", data_fields);
    
    matvar_t* dispinfo_var = formatDispInfo(dispinfo);
    matvar_t* displacements_var = formatDisplacements(dic_output);
    
    addFieldToStruct(data_dic_save, "dispinfo", dispinfo_var, 0);
    addFieldToStruct(data_dic_save, "displacements", displacements_var, 0);
    
    // Write all structs to file
    Mat_VarWrite(matfp, reference_save, MAT_COMPRESSION_NONE);
    Mat_VarWrite(matfp, current_save, MAT_COMPRESSION_NONE);
    Mat_VarWrite(matfp, data_dic_save, MAT_COMPRESSION_NONE);
    
    Mat_VarFree(reference_save);
    Mat_VarFree(current_save);
    Mat_VarFree(data_dic_save);
    
    Mat_Close(matfp);
    
    std::cout << "Wrote MATCHING file: " << filename << std::endl;
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
    
    // Create input struct
    std::vector<std::string> input_fields = {"radius", "spacing", "subregion_type", "interp_type"};
    matvar_t* input_struct = createStructVariable("input", input_fields);
    
    // Create output struct with displacement fields
    std::vector<std::string> output_fields = {"disps", "perspective_type", "units", "units_per_pixel"};
    matvar_t* output_struct = createStructVariable("output", output_fields);
    
    // Write structs to file
    Mat_VarWrite(matfp, input_struct, MAT_COMPRESSION_NONE);
    Mat_VarWrite(matfp, output_struct, MAT_COMPRESSION_NONE);
    
    Mat_VarFree(input_struct);
    Mat_VarFree(output_struct);
    Mat_Close(matfp);
    
    std::cout << "Wrote DIC result file: " << filename << std::endl;
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
    // Create dispinfo struct
    std::vector<std::string> field_names = {
        "cutoff_corrcoef", "cutoff_diffnorm", "cutoff_iteration",
        "radius", "spacing", "subsettrunc", "total_threads", "type", "units"
    };
    
    matvar_t* dispinfo = createStructVariable("dispinfo", field_names);
    if (!dispinfo) return nullptr;
    
    // Helper to write scalar field
    auto writeField = [&](const std::string& field_name, double value) {
        size_t dims[2] = {1, 1};
        double* data = new double[1];
        data[0] = value;
        matvar_t* field_var = Mat_VarCreate(field_name.c_str(), MAT_C_DOUBLE, MAT_T_DOUBLE, 
                                           2, dims, data, MAT_F_DONT_COPY_DATA);
        if (field_var) {
            Mat_VarSetStructFieldByName(dispinfo, field_name.c_str(), 0, field_var);
        }
    };
    
    // Populate numeric fields from params map
    for (const auto& p : params) {
        if (p.first == "type" || p.first == "units") continue;  // Skip string fields
        writeField(p.first, p.second);
    }
    
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
    
    // Write Displacement data
    std::vector<std::string> disp_fields = {"DispVec", "DispMgn"};
    matvar_t* disp_struct = createStructVariable("Disp", disp_fields);
    Mat_VarWrite(matfp, disp_struct, MAT_COMPRESSION_NONE);
    Mat_VarFree(disp_struct);
    
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
    
    // Write Deformation structure
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

} // namespace cppxdic
