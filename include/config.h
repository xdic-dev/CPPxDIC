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
    int total_threads = 6;
    bool high_strain_enabled = true;
    std::string seed_type = "seed";
    bool auto_update = true;
    int step_ref_change = 10;
    std::vector<int> initial_seed; // empty = auto
};

class Config {
public:
    // Global parameters (from global_param.m)
    std::vector<std::string> frictional_conditions = {"glass", "coating", "coating_oil"};
    int num_pair = 2; // Number of stereopairs
    // Camera pairs: per-pair (cam_first, cam_second) mapping
    // Default matches MATLAB import_raw_vid: pair1=(1,2), pair2=(4,3)
    std::vector<std::pair<int, int>> camera_pairs = {{1, 2}, {4, 3}};
    // Mirrored (MNG) rig settings, from theGlobalSettings_MNG.m. Only used when the
    // executable is built with -DXDIC_MODE=mirrored.
    int mirrored_num_pair = 7;                        // Npair: 8 views -> 7 overlapping stereopairs
    std::vector<int> mirrored_cam_order = {2, 1, 4, 3}; // cam_order: physical camera per view slot
    double robot_sample_freq = 1000.0; // robot sampling frequency (Hz)
    double vid_sample_freq = 50.0;     // image sampling frequency (Hz)

    // Path definitions
    std::string base_path = "/Users/jaoga/devlab/MultiDIC";
    std::string data_path; // location of the video files and protocol files
    std::string dic_path;  // location of the output data from DIC

    // Processing flags
    bool im_filter_mode = true;
    // Grayscale saturation (satur.m: clip pixels above limit_grayscale) applied to
    // every imported frame before ROI/matching/tracking. Set false for videos that
    // were already filtered/normalised upstream (e.g. MNG rigs), where clipping
    // would flatten the speckle contrast and make ncorr diverge.
    bool im_saturation_mode = true;
    // When true, idx_frame_start/idx_frame_end/frame_jump are authoritative and the
    // protocol-derived per-phase window (loading/slide/relax from the protocol .mat)
    // is NOT applied. Default false keeps the historical protocol-driven behaviour.
    bool force_frame_window = false;
    bool automatic_process = true;   // automatic processing flag
    bool parallel_processing = true; // parallel processing flag

    // DIC parameters (from dic_param.m)
    std::string subject_id = "S09";                    // Subject identifier
    std::string phase_id = "loading";                  // Phase identifier
    int material_id = 2;                               // Material identifier
    std::vector<int> nfcond_set = {5};                 // Number of conditions
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
    bool showvisu = false;   // Boolean for visualization
    bool debug_mode = false; // Debug mode flag

    // Logging settings (consumed by main() to configure cppxdic::log)
    std::string log_level = ""; // "" = unset; trace|debug|info|warn|error|off (console threshold)
    std::string log_file = "";  // "" = no log file; path to write a full-detail log

    // Plot map settings
    bool mapLogic = true;                                // Enable 3D map plotting
    std::vector<std::string> plotopt = {"Epc1", "Epc2"}; // Face measurement plot options
    std::string deftype = "both";                        // Derivative order: 'cum'|'rate'|'both'
    std::string viewplot =
        "below"; // Initial plot perspective: 'lateralR'|'below'|'lateralL'|'front'
    bool contactAreaLogic = false; // Contact area delimitation
    bool gapLogic = true;          // Suppress stitched pair boundary
    double maxCorrCoeff = 10.0;    // Max admissible correlation coefficient
    std::string format = "small";  // Plot format: 'small'|'normal'

    // Filtering settings
    bool smoothTimeLogic = true;  // Smooth time of the maps
    double filterFreq = 5.0;      // Cut-off frequency (Hz)
    bool smoothSpaceLogic = true; // Smooth space of the maps
    int smoothPar_n = 30;         // Space filtering parameter 1
    double smoothPar_sigma = 2.0; // Space filtering parameter 2

    // Additional plot settings
    bool showRobotLogic = true; // Plot kinematic/dynamic data
    bool showImgLogic = false;  // Plot image
    int camNbr = 0;             // Camera number (0 or specific camera)
    bool tracesLogic = false;   // Plot traces (deprecated)

    // Plot appearance settings
    double FaceAlpha = 1.0;          // Face transparency (0-1)
    std::string lineColor = "none";  // Line color: 'none'|'k'|'b', etc.
    double quiverScaleFactor = 20.0; // Arrow scale factor for vector plots

    // Output/Export settings
    std::string export_format = "vtk";  // Export format: 'vtk'|'ply'|'csv'
    bool export_each_frame = false;     // Export each frame separately
    std::vector<int> export_frame_list; // Specific frames to export (empty = all)

    // File version and naming
    std::string fileversion = "v2"; // Deformation file version

    // Output format control (legacy flags removed — use data_format instead)

    // Units and subregion
    double units_per_pixel = 0.2; // e.g., mm per pixel
    int subregion_radius = 20;    // default subset radius (pixels)
    // Grayscale saturation limit. 0 (default) = automatic: 70 for subjects whose
    // id number is < 8 (S01..S07), 100 otherwise (legacy MATLAB rule); any positive
    // value is used as-is regardless of the subject id.
    int limit_grayscale = 0;

    // Step-level DIC parameters (loaded from dic_params.txt)
    StepConfig step_d;       // Step D: initial tracking
    StepConfig step_e = [] { // Step E: matching (larger radius by default)
        StepConfig c;
        c.radius = 60; // MATLAB: subset_radius_ncorr_matching = 60
        return c;
    }();
    StepConfig step_f; // Step F: combined/final

    // Step E specific parameters (3D reconstruction)
    // Step E stitching of the per-pair 3D surfaces: "geometric" (MATLAB-faithful overlap
    // removal + boundary zipping; O(iterations x boundary x faces), can take many hours
    // on multi-pair rigs with large overlaps) or "simple" (plain append of all pairs,
    // overlaps kept). Only used when every pair of the trial was reconstructed.
    std::string step_e_stitch_mode = "geometric";
    bool step_e_distortion_removal =
        false; // Remove distortion from 2D points (MATLAB default: false)
    bool step_d_replacebadcorr =
        true; // Replace bad correlation subsets (MATLAB step2_dic_finish: active, but slow)

    // Step F specific parameters (deformation analysis)
    bool step_f_temporal_filtering = true; // Enable temporal filtering of displacements
    double step_f_freq_filt = 10.0;        // Low-pass cutoff frequency (Hz) for temporal filter
    bool step_f_compute_rbm = false;       // Compute rigid body motion (RBM) and ARBM deformation

    // Data format for pipeline I/O: "mat", "bin", or "json"
    std::string data_format = "mat";

    // NCorr-specific parameters (loaded from ncorr_params.txt)
    int ncorr_scalefactor = 3;                               // Scale factor
    std::string ncorr_interp = "quintic_bspline_precompute"; // Interpolation method
    std::string ncorr_subregion = "circle";                  // Subregion shape
    std::string ncorr_dic_config = "no_update";              // DIC config mode
    bool ncorr_no_update = true;  // Step-D tracking preset: true = NO_UPDATE (fixed ref within a
                                  // segment, the long-standing default); false = KEEP_MOST_POINTS
                                  // (with cutoff_corrcoef=0.5 -> MATLAB-style correlation-based
                                  // reference updates)
    int ncorr_fixed_step_ref = 0;  // >0: force a reference change every N frames (MATLAB ncorr
                                   // step analysis, step_ref_change semantics); 0 = off
    double ncorr_cutoff_corrcoef = 10.0;                     // Correlation cutoff
    std::string ncorr_roi_update_mode = "none";              // ROI update mode
    std::string ncorr_accumulation_mode = "none";            // Accumulation mode
    bool ncorr_save_disps_steps = false;                     // Save intermediate disps
    bool ncorr_perspective_interp = false;  // Eulerian perspective-change interpolation:
                                            // false = bicubic CUBIC_KEYS (historical workaround for
                                            // the FFT-bcoef border bias, kept as default);
                                            // true = biquintic B-spline (MATLAB ncorr behavior; safe
                                            // since the recursive bcoef filter fix)
    std::string ncorr_units = "mm";                          // Units string
    // Seed-quality gates (ncorr_params.txt). A seed whose optimised solution exceeds
    // either gate is rejected -> "could not seed any current image". Defaults match
    // CppNCorr's own defaults; before these were wired through, the engine defaults
    // (0.1 / 0.5) always applied whatever the config said.
    bool ncorr_seeds_are_optimized = false;                  // seeds already optimised (skip seed optimisation)
    double ncorr_cutoff_max_diffnorm = 0.1;                  // max diffnorm accepted for a seed
    double ncorr_cutoff_max_corrcoef = 0.5;                  // max corrcoef accepted for a TRACKING seed
    // Looser gate for inter-view MATCHING seeds (cam1->cam2 / view1->view2 at the
    // reference frame): different perspectives correlate worse than consecutive
    // frames (MNG mirrored rig pair 1: 0.51, rejected by the 0.5 tracking gate).
    double ncorr_matching_cutoff_max_corrcoef = 1.0;
    int ncorr_threads = 4;                                   // Number of threads
    bool ncorr_use_exact_matlab =
        false; // Use exact_matlab_DIC_analysis_* (mirrors MATLAB ncorr_alg_addanalysis chain
               // composition) instead of matlab_DIC_analysis_* (which has a chain-induced jump at
               // the first segment boundary).

    // DIC engine backend: "cuncorr" runs the cuNCorr engine (CUDA if a device is present,
    // else its CPU backend — numerically identical); "ncorr" uses the legacy CppNCorr path.
    // cuNCorr is a different algorithm from CppNCorr, so switching changes the DIC numbers.
    std::string dic_engine = "cuncorr";
    int cuncorr_seed_search = 15;  // cuNCorr coarse seed search radius in pixels

    // Visualization: VTK export options
    std::string vtk_format = "ascii"; // VTK format: ascii|binary
    bool vtk_include_scalars = true;  // Include scalar data in VTK
    bool vtk_include_vectors = true;  // Include vector data in VTK

    // Visualization: PLY export options
    std::string ply_format = "ascii"; // PLY format: ascii|binary
    bool ply_include_colors = true;   // Include colors in PLY

    // Visualization: CSV export options
    std::string csv_delimiter = ","; // CSV delimiter
    bool csv_include_header = true;  // Include header in CSV

    // Visualization: Video options
    bool generate_videos = false;     // Generate videos
    int video_fps = 10;               // Video FPS
    std::string video_codec = "MJPG"; // Video codec
    int video_quality = 90;           // Video quality (0-100)
    double video_alpha = 1.0;         // Video transparency

    // Visualization: Colormap options
    std::string colormap = "jet";             // Colormap name
    std::string colormap_range_mode = "auto"; // Range mode: auto|manual
    double colormap_min = 0.0;                // Manual colormap min
    double colormap_max = 1.0;                // Manual colormap max
    int colormap_levels = 256;                // Number of color levels

    // Visualization: Statistics options
    bool generate_summary_stats = true; // Generate summary statistics
    std::string stats_format = "txt";   // Stats format: txt|csv|json
    bool stats_per_frame = true;        // Per-frame statistics
    bool stats_spatial = true;          // Spatial statistics
    bool stats_temporal = true;         // Temporal statistics

    // Visualization: Advanced options
    double mesh_decimation = 1.0;           // Mesh decimation factor (1.0 = no decimation)
    bool mesh_smoothing = false;            // Enable mesh smoothing
    int mesh_smoothing_iterations = 10;     // Smoothing iterations
    bool show_axes = true;                  // Show axes in visualization
    bool show_colorbar = true;              // Show colorbar
    bool show_grid = false;                 // Show grid
    std::string background_color = "white"; // Background color

    // Derived parameters
    std::string material; // Material name from frictional_conditions

    // Methods
    Config();               // Constructor to set defaults
    void updateVariables(); // Derive material name etc.

    // Parameter file loading methods
    bool loadFromDicParamsFile(const std::string& filepath = "dic_params.txt");
    bool loadFromNcorrParamsFile(const std::string& filepath = "ncorr_params.txt");
    bool loadFromVisualizationParamsFile(const std::string& filepath = "visualization_params.txt");

    /**
     * @brief Load a single unified config file (the middle tier of the override chain).
     *
     * Implements the config-file tier of the three-tier override chain
     * (CLI args > config file > compiled defaults). The file uses the same INI-style
     * `key = value` format as dic_params.txt / ncorr_params.txt / visualization_params.txt
     * (see config/default.cfg). All DIC, NCorr and visualization keys may appear in one file;
     * unknown keys are ignored. Values override the compiled defaults; any field absent from
     * the file keeps its compiled default.
     *
     * @param filepath Path to the unified config file (e.g. config/default.cfg).
     * @return true if the file existed and was read, false otherwise.
     */
    bool loadFromConfigFile(const std::string& filepath = "config/default.cfg");

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
