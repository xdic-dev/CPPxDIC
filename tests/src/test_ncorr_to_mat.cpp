/**
 * Test program to convert ncorr .bin files to .mat format
 * Uses the existing reader and writer implementations
 */

#include "dic_structures.h"
#include "mat_writer.h"
#include <iostream>
#include <filesystem>
#include <string>

using namespace cppxdic;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <path_to_bin_file> [output_mat_file]" << std::endl;
        std::cerr << "If output_mat_file is not specified, it will use the same name and location as the input." << std::endl;
        std::cerr << "\nExample 1: " << argv[0] << " DIC3Dcombined_2Pairs_stitched.bin" << std::endl;
        std::cerr << "           (creates DIC3Dcombined_2Pairs_stitched.mat)" << std::endl;
        std::cerr << "\nExample 2: " << argv[0] << " input.bin output.mat" << std::endl;
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
        
        // Load the binary file
        std::cout << "Loading binary file...\n";
        DIC3Dcombined data = DIC3Dcombined::loadBinary(bin_path);
        
        // Print basic info
        std::cout << "\nLoaded data summary:\n";
        std::cout << "  Frames: " << data.Points3D.size() << "\n";
        if (!data.Points3D.empty()) {
            std::cout << "  Points per frame: " << data.Points3D[0].x.size() << "\n";
        }
        std::cout << "  Faces: " << (data.Faces.size() / 3) << "\n";
        std::cout << "  Pairs: " << data.pairIndices.size() << "\n";
        
        // Write to MAT file
        std::cout << "\nWriting to MAT file...\n";
        bool success = MatWriter::write3DCombinedResults(mat_path, data);
        
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
