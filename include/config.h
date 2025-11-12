/**
 * Configuration class for CPPXDIC
 * Handles loading and managing parameters equivalent to Matlab global_param.m and dic_param.m
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <string>
#include <vector>

class Config {
public:
    // Global parameters (from global_param.m)
    std::vector<std::string> frictional_conditions = {"glass", "coating", "coating_oil"};
    int num_pair = 2;                    // Number of stereopairs
    double robot_sample_freq = 1000.0;   // robot sampling frequency (Hz)
    double vid_sample_freq = 50.0;       // image sampling frequency (Hz)
    
    // Path definitions
    std::string base_path = "/Users/jaoga/devlab/MultiDIC";
    std::string data_path;               // location of the video files and protocol files
    std::string dic_path;                // location of the output data from DIC
    
    // Processing flags
    bool automatic_process = true;       // automatic processing flag
    bool parallel_processing = true;     // parallel processing flag
    
    // DIC parameters (from dic_param.m)
    std::string subject_id = "S09";      // Subject identifier
    std::string phase_id = "loading";    // Phase identifier
    int material_id = 2;                 // Material identifier
    std::vector<int> nfcond_set = {5};   // Number of conditions
    std::vector<double> spddxlcond_set = {0.04, 0.08}; // Speed conditions
    
    // Calibration settings
    std::string calib_folder_set = "2";
    
    // Reference trial number
    int ref_trial_id = 5;
    
    // Frame settings
    int idx_frame_start = 1;
    int idx_frame_end = 150;
    int frame_jump = 1;
    
    // Visualization settings
    bool showvisu = false;               // Boolean for visualization
    bool debug_mode = false;             // Debug mode flag
    
    // Units and subregion (from dic_param.m or config)
    double units_per_pixel = 0.2;        // e.g., mm per pixel
    int subregion_radius = 20;           // default subset radius (pixels)
    
    // Derived parameters
    std::string material;                // Material name from frictional_conditions
    
    // Methods
    void loadGlobalParams();
    void loadDicParams();
    void updateVariables();
    
private:
    void setDefaultPaths();
};

#endif // CONFIG_H
