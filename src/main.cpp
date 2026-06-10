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
#include <getopt.h>
#include "config.h"
#include "dic_analysis.h"
#include "utils.h"

// Print usage information
void print_usage(const char* prog_name) {
    std::cout << "Usage: " << prog_name << " [OPTIONS]\n\n"
              << "FINGERTIP 3D RECONSTRUCTION using DIC\n\n"
              << "OPTIONS:\n"
              << "  -s, --subject <id>         Override subject ID (e.g., S09)\n"
              << "  -r, --reftrial <num>       Override reference trial number\n"
              << "  -d, --dic-params <file>    DIC parameters file (default: dic_params.txt)\n"
              << "  -n, --ncorr-params <file>  NCorr parameters file (default: ncorr_params.txt)\n"
              << "  -v, --viz-params <file>    Visualization parameters file (default: visualization_params.txt)\n"
              << "  -h, --help                 Show this help message\n\n"
              << "EXAMPLES:\n"
              << "  " << prog_name << " --subject S10 --reftrial 3\n"
              << "  " << prog_name << " -s S08 -r 5 -d custom_dic.txt\n"
              << std::endl;
}

int main(int argc, char* argv[]) {
    std::cout << "FINGERTIP 3D RECONSTRUCTION using DIC" << std::endl;
    std::cout << "--------------------------------------" << std::endl;
    std::cout << "DIC analysis for the fingertip" << std::endl;
    std::cout << std::endl;
    
    // Command-line argument parsing
    std::string subject_override = "";
    int reftrial_override = -1;
    std::string dic_params_file = "dic_params.txt";
    std::string ncorr_params_file = "ncorr_params.txt";
    std::string viz_params_file = "visualization_params.txt";
    
    static struct option long_options[] = {
        {"subject",      required_argument, 0, 's'},
        {"reftrial",     required_argument, 0, 'r'},
        {"dic-params",   required_argument, 0, 'd'},
        {"ncorr-params", required_argument, 0, 'n'},
        {"viz-params",   required_argument, 0, 'v'},
        {"help",         no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };
    
    int opt;
    int option_index = 0;
    while ((opt = getopt_long(argc, argv, "s:r:d:n:v:h", long_options, &option_index)) != -1) {
        switch (opt) {
            case 's':
                subject_override = optarg;
                break;
            case 'r':
                reftrial_override = std::stoi(optarg);
                break;
            case 'd':
                dic_params_file = optarg;
                break;
            case 'n':
                ncorr_params_file = optarg;
                break;
            case 'v':
                viz_params_file = optarg;
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }
    
    try {
        // Load configurations with hierarchy:
        // 1. Load default values (from class initialization)
        Config config;
        
        // 2. Load DIC params file if it exists
        std::cout << "Loading configuration files..." << std::endl;
        std::cout << "------------------------------" << std::endl;
        config.loadFromDicParamsFile(dic_params_file);
        
        // 3. Override with NCorr params if it exists
        config.loadFromNcorrParamsFile(ncorr_params_file);
        
        // 4. Load visualization params if it exists
        config.loadFromVisualizationParamsFile(viz_params_file);
        
        // 5. Apply command-line overrides (highest priority)
        if (!subject_override.empty()) {
            config.overrideSubject(subject_override);
        }
        if (reftrial_override >= 0) {
            config.overrideRefTrial(reftrial_override);
        }
        
        // Update derived variables
        config.updateVariables();
        std::cout << std::endl;
        
        // Print final parameters
        std::cout << "Final Configuration:" << std::endl;
        std::cout << "--------------------" << std::endl;
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
        std::cout << "base_path: [" << config.base_path << "]" << std::endl;
        std::cout << "data_path: [" << config.data_path << "]" << std::endl;
        std::cout << "dic_path:  [" << config.dic_path  << "]" << std::endl;
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
