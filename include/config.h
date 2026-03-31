/**
 * Configuration class for CPPXDIC
 * Handles loading and managing parameters equivalent to Matlab global_param.m and dic_param.m
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <string>
#include <vector>

/**
 * Step-level DIC parameters (tracking/matching/combined)
 * Loaded from dic_params.txt step_d_*, step_e_*, step_f_* keys
 */
struct StepConfig {
    std::string analysis_type = "regular";
    int radius = 40;
    int spacing = 10;
    double cutoff_diffnorm = 1e-5;
    int cutoff_iteration = 100;
    int total_threads = 1;
    bool high_strain_enabled = true;
    std::string seed_type = "seed";
    bool auto_update = true;
    int step_ref_change = 10;
    std::vector<int> initial_seed;  // empty = auto
};

class Config {
public:
    // Global parameters (from global_param.m)
    std::vector<std::string> frictional_conditions = {"glass", "coating", "coating_oil"};
    int num_pair = 2;                    // Number of stereopairs
    // Camera pairs: per-pair (cam_first, cam_second) mapping
    // Default matches MATLAB import_raw_vid: pair1=(1,2), pair2=(4,3)
    std::vector<std::pair<int,int>> camera_pairs = {{1,2}, {4,3}};
    double robot_sample_freq = 1000.0;   // robot sampling frequency (Hz)
    double vid_sample_freq = 50.0;       // image sampling frequency (Hz)
    
    // Path definitions
    std::string base_path = "/Users/jaoga/devlab/MultiDIC";
    std::string data_path;               // location of the video files and protocol files
    std::string dic_path;                // location of the output data from DIC
    
    // Processing flags
    bool im_filter_mode = false;
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
    int idx_frame_start = 10;
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
    
    // Output format control (legacy flags removed — use data_format instead)
    
    // Units and subregion
    double units_per_pixel = 0.2;        // e.g., mm per pixel
    int subregion_radius = 20;           // default subset radius (pixels)
    int limit_grayscale = 70;            // Grayscale limit threshold
    
    // Step-level DIC parameters (loaded from dic_params.txt)
    StepConfig step_d;                   // Step D: initial tracking
    StepConfig step_e = []{              // Step E: matching (larger radius by default)
        StepConfig c;
        c.radius = 60;                  // MATLAB: subset_radius_ncorr_matching = 60
        return c;
    }();
    StepConfig step_f;                   // Step F: combined/final
    
    // Step E specific parameters (3D reconstruction)
    bool step_e_distortion_removal = false;  // Remove distortion from 2D points (MATLAB default: false)
    bool step_d_replacebadcorr = false;      // Replace bad correlation subsets (MATLAB step2_dic_finish: active, but slow)
    
    // Step F specific parameters (deformation analysis)
    bool step_f_temporal_filtering = true;   // Enable temporal filtering of displacements
    double step_f_freq_filt = 10.0;          // Low-pass cutoff frequency (Hz) for temporal filter
    bool step_f_compute_rbm = false;         // Compute rigid body motion (RBM) and ARBM deformation
    
    // Data format for pipeline I/O: "mat", "bin", or "json"
    std::string data_format = "mat";
    
    // NCorr-specific parameters (loaded from ncorr_params.txt)
    int ncorr_scalefactor = 3;           // Scale factor
    std::string ncorr_interp = "quintic_bspline_precompute";  // Interpolation method
    std::string ncorr_subregion = "circle";                    // Subregion shape
    std::string ncorr_dic_config = "no_update";                // DIC config mode
    double ncorr_cutoff_corrcoef = 10.0;                       // Correlation cutoff
    std::string ncorr_roi_update_mode = "none";                // ROI update mode
    std::string ncorr_accumulation_mode = "none";              // Accumulation mode
    bool ncorr_save_disps_steps = false;                        // Save intermediate disps
    bool ncorr_perspective_interp = false;                      // Perspective interpolation
    std::string ncorr_units = "mm";                             // Units string
    bool ncorr_seeds_are_optimized = true;                      // Optimized seeds
    double ncorr_cutoff_max_diffnorm = 1e-5;                   // Max diff norm cutoff
    double ncorr_cutoff_max_corrcoef = 10.0;                   // Max corr coef cutoff
    int ncorr_threads = 4;                                      // Number of threads
    
    // Visualization: VTK export options
    std::string vtk_format = "ascii";    // VTK format: ascii|binary
    bool vtk_include_scalars = true;     // Include scalar data in VTK
    bool vtk_include_vectors = true;     // Include vector data in VTK
    
    // Visualization: PLY export options
    std::string ply_format = "ascii";    // PLY format: ascii|binary
    bool ply_include_colors = true;      // Include colors in PLY
    
    // Visualization: CSV export options
    std::string csv_delimiter = ",";     // CSV delimiter
    bool csv_include_header = true;      // Include header in CSV
    
    // Visualization: Video options
    bool generate_videos = true;        // Generate videos
    int video_fps = 10;                  // Video FPS
    std::string video_codec = "MJPG";    // Video codec
    int video_quality = 90;              // Video quality (0-100)
    double video_alpha = 1.0;            // Video transparency
    
    // Visualization: Colormap options
    std::string colormap = "jet";        // Colormap name
    std::string colormap_range_mode = "auto"; // Range mode: auto|manual
    double colormap_min = 0.0;           // Manual colormap min
    double colormap_max = 1.0;           // Manual colormap max
    int colormap_levels = 256;           // Number of color levels
    
    // Visualization: Statistics options
    bool generate_summary_stats = true;  // Generate summary statistics
    std::string stats_format = "txt";    // Stats format: txt|csv|json
    bool stats_per_frame = true;         // Per-frame statistics
    bool stats_spatial = true;           // Spatial statistics
    bool stats_temporal = true;          // Temporal statistics
    
    // Visualization: Advanced options
    double mesh_decimation = 1.0;        // Mesh decimation factor (1.0 = no decimation)
    bool mesh_smoothing = false;         // Enable mesh smoothing
    int mesh_smoothing_iterations = 10;  // Smoothing iterations
    bool show_axes = true;               // Show axes in visualization
    bool show_colorbar = true;           // Show colorbar
    bool show_grid = false;              // Show grid
    std::string background_color = "white"; // Background color
    
    // Derived parameters
    std::string material;                // Material name from frictional_conditions
    
    // Methods
    Config();                            // Constructor to set defaults
    void updateVariables();              // Derive material name etc.
    
    // Parameter file loading methods
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
