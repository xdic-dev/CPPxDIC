/**
 * CPPXDIC - C++ equivalent of Matlab xDIC
 * Main entry point for FINGERTIP 3D RECONSTRUCTION using DIC
 * 
 * This is the C++ port of the Matlab xDIC library using the ncorr C++ library
 */

#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include "config.h"
#include "dic_analysis.h"
#include "utils.h"

int main(int argc, char* argv[]) {
    std::cout << "FINGERTIP 3D RECONSTRUCTION using DIC" << std::endl;
    std::cout << "--------------------------------------" << std::endl;
    std::cout << "DIC analysis for the fingertip" << std::endl;
    
    try {
        // Load configurations
        Config config;
        config.loadGlobalParams();
        config.loadDicParams();
        config.updateVariables();
        
        // Print parameters
        std::cout << "Parameters:" << std::endl;
        std::cout << "-----------" << std::endl;
        std::cout << "Subject: " << config.subject_id 
                  << ", Phase: " << config.phase_id 
                  << ", Material: " << config.material 
                  << ", Stereo Pairs: " << config.num_pair << std::endl;
        std::cout << "Reference trial number: " << config.ref_trial_id << std::endl;
        std::cout << "Frame: " << config.idx_frame_start 
                  << " to " << config.idx_frame_end 
                  << ", jump= " << config.frame_jump << std::endl;
        std::cout << "Show visualization: " << config.showvisu 
                  << ", Debug mode: " << config.debug_mode 
                  << ", Automatic process: " << config.automatic_process << std::endl;
        std::cout << std::endl;
        
        // Checking
        std::cout << "Checking the data and protocol..." << std::endl;
        if (!Utils::dicCheck(config)) {
            std::cerr << "Data and protocol check failed!" << std::endl;
            return 1;
        }
        
        // Call analysis function
        DicAnalysis dicAnalysis(config);
        bool success = dicAnalysis.run();
        
        if (success) {
            std::cout << "Analysis completed successfully!" << std::endl;
        } else {
            std::cerr << "Analysis failed!" << std::endl;
            return 1;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    std::cout << "End of script" << std::endl;
    return 0;
}
