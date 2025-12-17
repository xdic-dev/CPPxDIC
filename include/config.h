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
    bool im_filter_mode = false;
    bool automatic_process = true;       // automatic processing flag
    bool parallel_processing = false;     // parallel processing flag
    
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
    
    // Visualization settings (equivalent to MATLAB optStructPlot)
    bool showvisu = false;               // Boolean for visualization
    bool debug_mode = true;             // Debug mode flag
    
    // Plot map settings
    bool mapLogic = true;                // Enable 3D map plotting
    std::vector<std::string> plotopt = {"Epc1", "Epc2"};  // Face measurement plot options
    std::string deftype = "both";        // Derivative order: 'cum'|'rate'|'both'
    std::string viewplot = "below";      // Initial plot perspective: 'lateralR'|'below'|'lateralL'|'front'
    bool contactAreaLogic = false;       // Contact area delimitation
    bool gapLogic = true;                // Suppress stitched pair boundary
    double maxCorrCoeff = 10.0;          // Max admissible correlation coefficient
    std::string format = "small";        // Plot format: 'small'|'normal'
    
    // Filtering settings
    bool smoothTimeLogic = true;         // Smooth time of the maps
    double filterFreq = 5.0;             // Cut-off frequency (Hz)
    bool smoothSpaceLogic = true;        // Smooth space of the maps
    int smoothPar_n = 30;                // Space filtering parameter 1
    double smoothPar_sigma = 2.0;        // Space filtering parameter 2
    
    // Additional plot settings
    bool showRobotLogic = true;          // Plot kinematic/dynamic data
    bool showImgLogic = false;           // Plot image
    int camNbr = 0;                      // Camera number (0 or specific camera)
    bool tracesLogic = false;            // Plot traces (deprecated)
    
    // Plot appearance settings
    double FaceAlpha = 1.0;              // Face transparency (0-1)
    std::string lineColor = "none";      // Line color: 'none'|'k'|'b', etc.
    double quiverScaleFactor = 20.0;     // Arrow scale factor for vector plots
    
    // Output/Export settings
    std::string export_format = "vtk";   // Export format: 'vtk'|'ply'|'csv'
    bool export_each_frame = false;      // Export each frame separately
    std::vector<int> export_frame_list;  // Specific frames to export (empty = all)
    
    // File version and naming
    std::string fileversion = "v2";      // Deformation file version
    
    // Output format control
    bool generate_mat_files = true;     // Generate MATLAB .mat files (default: false, true if debug_mode)
    bool cleanup_cache_bins = false;     // Delete cached .bin files after use (default: false)
    
    // Units and subregion (from dic_param.m or config)
    double units_per_pixel = 0.2;        // e.g., mm per pixel
    int subregion_radius = 20;           // default subset radius (pixels)
    
    // Derived parameters
    std::string material;                // Material name from frictional_conditions
    
    // Methods
    Config();                            // Constructor to set defaults
    void loadGlobalParams();
    void loadDicParams();
    void updateVariables();
    
    // New parameter file loading methods
    bool loadFromDicParamsFile(const std::string& filepath = "dic_params.txt");
    bool loadFromNcorrParamsFile(const std::string& filepath = "ncorr_params.txt");
    bool loadFromVisualizationParamsFile(const std::string& filepath = "visualization_params.txt");
    
    // Command-line override methods
    void overrideSubject(const std::string& subject);
    void overrideRefTrial(int trial);
    
private:
    void setDefaultPaths();
    std::string parseConfigValue(const std::string& line, const std::string& key);
    std::vector<std::string> parseStringList(const std::string& value);
    std::vector<int> parseIntList(const std::string& value);
    std::vector<double> parseDoubleList(const std::string& value);
    bool parseBool(const std::string& value);
};

#endif // CONFIG_H
