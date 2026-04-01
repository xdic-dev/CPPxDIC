/**
 * Parameter structures for CPPXDIC
 * Encapsulates parameters to avoid passing many arguments between functions
 * Equivalent to parameter structures in Matlab stepD_2DDIC.m
 */

#ifndef PARAMETERS_H
#define PARAMETERS_H

#include <string>
#include <vector>

namespace cppxdic {

/**
 * Base parameters structure
 * Contains fundamental paths and settings for DIC analysis
 * Equivalent to base_parameters struct in Matlab
 */
struct BaseParameters {
    // Paths
    std::string baseDataPath;
    std::string baseResultPath;
    std::string outputPath;
    
    // Trial/Subject information
    std::string subject;
    std::string material;
    std::string trial;
    int stereopair;
    std::string phase;
    std::string reftrial;
    
    // File paths
    std::string roifile;
    std::string matchingfile;
    std::string seedfile;
    
    // Frame settings
    int jump;
    int idxstart_set;
    int idxend_set;
    
    // Processing settings
    int limit_grayscale;
    
    // Camera numbers (derived from stereopair)
    int cam_1;
    int cam_2;
    
    // Constructor with defaults
    BaseParameters() : 
        stereopair(1), jump(1), 
        idxstart_set(0), idxend_set(0),
        limit_grayscale(70),
        cam_1(1), cam_2(2) {}
};

/**
 * Step analysis parameters structure
 * Contains high strain analysis settings
 */
struct StepAnalysisParams {
    bool enabled;           // high_strain_analysis
    std::string type;       // seed_propagation type
    bool auto_update;       // auto_ref_change
    int step;              // step_ref_change
    
    StepAnalysisParams() : 
        enabled(true), type("seed"), 
        auto_update(true), step(10) {}
};

/**
 * Step parameters structure for DIC analysis
 * Contains DIC-specific parameters like radius, spacing, cutoff
 * Equivalent to step1_parameters, step2_parameters, step1_2_parameters in Matlab
 */
struct StepParameters {
    // Analysis settings
    std::string type;                  // analysis_direction (e.g., "regular")
    int radius;                        // subset_radius_ncorr
    int spacing;                       // subset_spacing
    double cutoff_diffnorm;           // cutoff threshold
    int cutoff_iteration;             // number_iteration_solver
    int total_threads;                // number of threads
    
    // Step analysis parameters
    StepAnalysisParams stepanalysis_params;
    
    // Initial seed point (pixel world coordinates)
    std::vector<int> initial_seed;    // [x, y] in pixels
    
    // Constructor with defaults
    StepParameters() : 
        type("regular"),
        radius(40),
        spacing(10),
        cutoff_diffnorm(1e-5),
        cutoff_iteration(100),
        total_threads(4) {}
};

/**
 * Seed point structure
 * Stores seed points in both pixel world (pw) and subset world (sw) coordinates
 */
struct SeedPoint {
    std::vector<int> pw;  // pixel world coordinates [x, y]
    std::vector<int> sw;  // subset world coordinates [x, y]
    
    SeedPoint() : pw(2, 0), sw(2, 0) {}
    SeedPoint(int px, int py) : pw{px, py}, sw(2, 0) {}
};

/**
 * Protocol information structure
 * Contains trial condition information from protocol MAT file
 */
struct ProtocolInfo {
    std::vector<std::string> dircond;   // Direction conditions
    std::vector<int> nfcond;            // Force conditions (N)
    std::vector<int> spdcond;           // Speed conditions
    std::vector<int> repcond;           // Repetition conditions
    std::vector<double> spddxlcond;     // Speed DXL conditions
    
    // Helper to get pair order based on direction
    void getPairOrder(const std::string& dir, std::vector<int>& pairOrder, bool& pairForced) const {
        if (dir == "Ubnf") {
            pairOrder = {2, 1};
            pairForced = true;
        } else if (dir == "Rbnf") {
            pairOrder = {1, 2};
            pairForced = true;
        } else {
            pairOrder = {1, 2};
            pairForced = false;
        }
    }
};

/**
 * DIC constants
 * Global constants used throughout the DIC analysis
 */
struct DICConstants {
    static constexpr int TRUE_FPS = 50;
    static constexpr int LIMIT_GRAYSCALE_DEFAULT = 70;
    static constexpr int LIMIT_GRAYSCALE_S8_PLUS = 100;
    
    // DIC default parameters
    static constexpr int SUBSET_RADIUS_TRACKING = 40;
    static constexpr int SUBSET_RADIUS_MATCHING = 60;
    static constexpr int SUBSET_SPACING = 10;
    static constexpr double CUTOFF_TRACKING = 1e-5;
    static constexpr double CUTOFF_MATCHING = 1e-5;
    static constexpr int NUMBER_ITERATION_SOLVER = 100;
    static constexpr int NUMBER_THREADS = 4;
};

} // namespace cppxdic

#endif // PARAMETERS_H
