/**
 * Test program to convert ncorr .bin files to .mat format
 * Supports both ncorr DIC_analysis_output and DIC3Dcombined formats
 * Uses the existing reader and writer implementations
 */

#include "dic_structures.h"
#include "mat_writer.h"
#include <ncorr.h>
#include <matio.h>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <string>

using namespace cppxdic;

bool convertNcorrDICOutputToMat(const std::string& bin_path, const std::string& mat_path) {
    std::cout << "Converting ncorr::DIC_analysis_output format...\n\n";
    
    // Load DIC output
    ncorr::DIC_analysis_output dic_output = ncorr::DIC_analysis_output::load(bin_path);
    
    std::cout << "Loaded data summary:\n";
    std::cout << "  Frames: " << dic_output.disps.size() << "\n";
    std::cout << "  Perspective: " << (dic_output.perspective_type == ncorr::PERSPECTIVE::LAGRANGIAN ? "Lagrangian" : "Eulerian") << "\n";
    std::cout << "  Units: " << dic_output.units << "\n";
    std::cout << "  Units per pixel: " << dic_output.units_per_pixel << "\n";
    
    if (!dic_output.disps.empty()) {
        const auto& u_array = dic_output.disps[0].get_u().get_array();
        std::cout << "  Field dimensions: " << u_array.height() << " x " << u_array.width() << "\n";
    }
    
    // Create MAT file
    std::cout << "\nWriting to MAT file...\n";
    mat_t* matfp = Mat_CreateVer(mat_path.c_str(), nullptr, MAT_FT_MAT73);
    if (!matfp) {
        std::cerr << "Failed to create MAT file\n";
        return false;
    }
    
    // Write metadata
    std::string persp_str = (dic_output.perspective_type == ncorr::PERSPECTIVE::LAGRANGIAN) ? "Lagrangian" : "Eulerian";
    size_t dims_str[2] = {1, persp_str.length()};
    matvar_t* persp_var = Mat_VarCreate("perspective", MAT_C_CHAR, MAT_T_UTF8, 2, dims_str, (void*)persp_str.c_str(), 0);
    Mat_VarWrite(matfp, persp_var, MAT_COMPRESSION_NONE);
    Mat_VarFree(persp_var);
    
    size_t dims_units[2] = {1, dic_output.units.length()};
    matvar_t* units_var = Mat_VarCreate("units", MAT_C_CHAR, MAT_T_UTF8, 2, dims_units, (void*)dic_output.units.c_str(), 0);
    Mat_VarWrite(matfp, units_var, MAT_COMPRESSION_NONE);
    Mat_VarFree(units_var);
    
    size_t dims_scalar[2] = {1, 1};
    matvar_t* upp_var = Mat_VarCreate("units_per_pixel", MAT_C_DOUBLE, MAT_T_DOUBLE, 2, dims_scalar, &dic_output.units_per_pixel, 0);
    Mat_VarWrite(matfp, upp_var, MAT_COMPRESSION_NONE);
    Mat_VarFree(upp_var);
    
    // Write displacement data as cell arrays
    size_t n_frames = dic_output.disps.size();
    size_t cell_dims[2] = {1, n_frames};
    
    matvar_t* u_cell = Mat_VarCreate("u", MAT_C_CELL, MAT_T_CELL, 2, cell_dims, nullptr, 0);
    matvar_t* v_cell = Mat_VarCreate("v", MAT_C_CELL, MAT_T_CELL, 2, cell_dims, nullptr, 0);
    matvar_t* cc_cell = Mat_VarCreate("cc", MAT_C_CELL, MAT_T_CELL, 2, cell_dims, nullptr, 0);
    
    for (size_t i = 0; i < n_frames; ++i) {
        const auto& disp = dic_output.disps[i];
        const auto& u_array = disp.get_u().get_array();
        const auto& v_array = disp.get_v().get_array();
        const auto& cc_array = disp.get_cc().get_array();
        const auto& roi_mask = disp.get_roi().get_mask();
        
        size_t height = u_array.height();
        size_t width = u_array.width();
        size_t array_dims[2] = {height, width};
        
        // Copy data arrays
        std::vector<double> u_data(height * width);
        std::vector<double> v_data(height * width);
        std::vector<double> cc_data(height * width);
        
        for (size_t y = 0; y < height; ++y) {
            for (size_t x = 0; x < width; ++x) {
                size_t idx = y * width + x;
                if (roi_mask(y, x)) {
                    u_data[idx] = u_array(y, x);
                    v_data[idx] = v_array(y, x);
                    cc_data[idx] = cc_array(y, x);
                } else {
                    u_data[idx] = std::numeric_limits<double>::quiet_NaN();
                    v_data[idx] = std::numeric_limits<double>::quiet_NaN();
                    cc_data[idx] = std::numeric_limits<double>::quiet_NaN();
                }
            }
        }
        
        matvar_t* u_var = Mat_VarCreate("", MAT_C_DOUBLE, MAT_T_DOUBLE, 2, array_dims, u_data.data(), 0);
        matvar_t* v_var = Mat_VarCreate("", MAT_C_DOUBLE, MAT_T_DOUBLE, 2, array_dims, v_data.data(), 0);
        matvar_t* cc_var = Mat_VarCreate("", MAT_C_DOUBLE, MAT_T_DOUBLE, 2, array_dims, cc_data.data(), 0);
        
        Mat_VarSetCell(u_cell, i, u_var);
        Mat_VarSetCell(v_cell, i, v_var);
        Mat_VarSetCell(cc_cell, i, cc_var);
    }
    
    Mat_VarWrite(matfp, u_cell, MAT_COMPRESSION_NONE);
    Mat_VarWrite(matfp, v_cell, MAT_COMPRESSION_NONE);
    Mat_VarWrite(matfp, cc_cell, MAT_COMPRESSION_NONE);
    
    Mat_VarFree(u_cell);
    Mat_VarFree(v_cell);
    Mat_VarFree(cc_cell);
    
    Mat_Close(matfp);
    return true;
}

bool convertDIC3DcombinedToMat(const std::string& bin_path, const std::string& mat_path) {
    std::cout << "Converting DIC3Dcombined format...\n\n";
    
    // Load the binary file
    DIC3Dcombined data = DIC3Dcombined::loadBinary(bin_path);
    
    // Print basic info
    std::cout << "Loaded data summary:\n";
    std::cout << "  Frames: " << data.Points3D.size() << "\n";
    if (!data.Points3D.empty()) {
        std::cout << "  Points per frame: " << data.Points3D[0].x.size() << "\n";
    }
    std::cout << "  Faces: " << (data.Faces.size() / 3) << "\n";
    std::cout << "  Pairs: " << data.pairIndices.size() << "\n";
    
    // Write to MAT file
    std::cout << "\nWriting to MAT file...\n";
    return MatWriter::write3DCombinedResults(mat_path, data);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <path_to_bin_file> [output_mat_file]" << std::endl;
        std::cerr << "If output_mat_file is not specified, it will use the same name and location as the input." << std::endl;
        std::cerr << "\nSupported formats:" << std::endl;
        std::cerr << "  - ncorr*.mat.bin (ncorr::DIC_analysis_output)" << std::endl;
        std::cerr << "  - DIC3Dcombined*.bin (DIC3Dcombined results)" << std::endl;
        std::cerr << "\nExample 1: " << argv[0] << " ncorr1.mat.bin" << std::endl;
        std::cerr << "           (creates ncorr1.mat)" << std::endl;
        std::cerr << "\nExample 2: " << argv[0] << " DIC3Dcombined.bin output.mat" << std::endl;
        return 1;
    }
    
    std::string bin_path = argv[1];
    std::string mat_path;
    
    if (argc >= 3) {
        mat_path = argv[2];
    } else {
        // Replace .bin extension with .mat
        std::filesystem::path p(bin_path);
        p.replace_extension(".mat");
        mat_path = p.string();
    }
    
    std::cout << "========================================\n";
    std::cout << "NCORR Binary to MAT Converter\n";
    std::cout << "========================================\n";
    std::cout << "Input:  " << bin_path << "\n";
    std::cout << "Output: " << mat_path << "\n\n";
    
    try {
        // Check if input file exists
        if (!std::filesystem::exists(bin_path)) {
            std::cerr << "Error: Input file does not exist: " << bin_path << std::endl;
            return 1;
        }
        
        // Detect format by checking magic number
        std::ifstream ifs(bin_path, std::ios::binary);
        if (!ifs.is_open()) {
            std::cerr << "Error: Cannot open file: " << bin_path << std::endl;
            return 1;
        }
        
        uint32_t magic = 0;
        ifs.read(reinterpret_cast<char*>(&magic), sizeof(uint32_t));
        ifs.close();
        
        bool success = false;
        
        // DIC3Dcombined files have magic number 0x44494333 ("DIC3")
        if (magic == 0x44494333) {
            success = convertDIC3DcombinedToMat(bin_path, mat_path);
        } else {
            // Try ncorr format
            try {
                success = convertNcorrDICOutputToMat(bin_path, mat_path);
            } catch (const std::exception& e) {
                std::cerr << "Failed to convert as ncorr format: " << e.what() << "\n\n";
                std::cerr << "File may be corrupted or in an unsupported format.\n";
                return 1;
            }
        }
        
        if (success) {
            std::cout << "\n========================================\n";
            std::cout << "Conversion successful!\n";
            std::cout << "========================================\n";
            std::cout << "MAT file saved to: " << mat_path << "\n";
            
            // Get file sizes
            auto bin_size = std::filesystem::file_size(bin_path);
            auto mat_size = std::filesystem::file_size(mat_path);
            
            std::cout << "\nFile sizes:\n";
            std::cout << "  Binary: " << (bin_size / 1024.0 / 1024.0) << " MB\n";
            std::cout << "  MAT:    " << (mat_size / 1024.0 / 1024.0) << " MB\n";
            
            return 0;
        } else {
            std::cerr << "\nError: Failed to write MAT file\n";
            return 1;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
